
#include <atomic>
#include <iomanip>
#include <ctime>
#include <deque>
#include <unordered_map>

#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/regex.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/io/detail/quoted_manip.hpp>

#include "vUbercool.h"
#include "Statistics.h"
#include "Hosts.h"
#include "HttpRequest.h"
#include <warlib/WarLog.h>
#include <warlib/error_handling.h>

// Switch to ease the measurement of the performance impact by using
// boost::iostreams
#define USE_BOOST_IOSTREAM 1


using namespace war;
using namespace std;
using boost::asio::ip::tcp;

int to_int(const tcp::socket& sck) {
    return static_cast<int>(const_cast<tcp::socket&>(sck).native_handle());
}

std::ostream& operator << (std::ostream& out, const tcp::socket& v) {
    return out << "{ socket " << to_int(v) << " }";
}

namespace vUbercool {
namespace http {
namespace impl {

thread_local Statistics thd_http_stats_;

using conn_key_t = uint64_t;

template<typename containerT, typename iteratorT>
bool is_iterator_in(const containerT& container, const iteratorT it)
{
    for(auto i = container.cbegin(); i != container.cend(); ++i) {
        if (it == i)
            return true;
    }

    return false;
}

bool ParseHttpRequest(const char *begin, const char *end, HttpRequest& req);

class CurrentReply : public HttpReply
{
public:
    CurrentReply() = default;

    std::ostream& Out() override { return body_; }

    void Reply(int code, const HttpRequest& req) {

        close_on_reply_ = req.CloseOnReply();

        static const std::string ok{"OK"};

        DoReply(code, ok, req);
    }

    void ReplyWithError(int code, const string& content,
                        const http::HttpRequest& req) override {
        close_on_reply_ = true; // Always close after an error condition.

        // Kill any existing content
        const HttpConfig default_config;
        Prepare(req, default_config);

        // TODO: Make a nice HTTP body with the message
        body_ << content;

        DoReply(code, content, req);
    }

    // Reset the stream buffers (not the buffer buffers used for IO)
    void Reset() override {
        static const string empty;

        ResetStreamLayers();
        header_.str(empty);
        header_.clear();
#if USE_BOOST_IOSTREAM
        body_.reset();
#else
        body_.str(empty);
        body_.clear();
#endif
        body_str_.clear();
        header_str_.clear();
        compression_type_ = nullptr;
        bytes_in_reply_ = 0;
    }

    void ResetStreamLayers() {
#if USE_BOOST_IOSTREAM
         while(stream_layers_ > 0) {
            body_.pop();
            --stream_layers_;
        }
#endif
    }

    void Prepare(const http::HttpRequest& req,
                 const HttpConfig& config) override {
        Reset();
        now_ = std::time(nullptr);

#if USE_BOOST_IOSTREAM
        if (req.accept_encoding_gzip && config.allow_gzip) {
             body_.push(boost::iostreams::gzip_compressor());
            ++stream_layers_;
            compression_type_ = "gzip";
        } else if (req.accept_encoding_deflate && config.allow_deflate) {
            body_.push(boost::iostreams::zlib_compressor());
            ++stream_layers_;
            compression_type_ = "deflate";
        }

        LOG_TRACE2_FN << "Using compression: " << (compression_type_ ? compression_type_ : "none");

        body_.push(boost::iostreams::back_inserter(body_str_));
        ++stream_layers_;
#endif
    }

    std::vector<boost::asio::const_buffer>& GetBuffers() {
        return buffers_;
    }

    bool CloseOnReply() const override {
        return close_on_reply_;
    }

    std::time_t GetTime() const override { return now_; }

    size_t GetBytes() const {
        return bytes_in_reply_;
    }

private:
    void DoReply(int code, const std::string& msg, const http::HttpRequest& req) {
        static const std::string crlf{"\r\n"};
        thread_local std::string date;
        thread_local time_t last_time;

#if USE_BOOST_IOSTREAM
        // Flush whatever we have in the _body stream.
        body_.strict_sync();
        ResetStreamLayers();
#else
        body_str_ = move(body_.str());
#endif
        if (now_ != last_time) {
            tm my_tm{};
#ifdef _MSC_VER
            gmtime_s(&my_tm, &now_);
#else
            gmtime_r(&now_, &my_tm);
#endif
            char buf[36]{};
            strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &my_tm);
            date = buf;
            //date = std::put_time(&my_tm, "%a, %d %b %Y %H:%M:%S GMT");
        }

        header_ << req.GetHttpVersionName()
            << ' ' << code << ' ' << msg << crlf
            << date << crlf
            << "Server: vUbercool/"
            << static_cast<int>(ApplicationVersion::MAJOR)
            << '.'
            << static_cast<int>(ApplicationVersion::MINOR)
            << crlf
            << "Content-Length: " << (body_str_.size()) << crlf
            << "Content-Type: text/html; charset=utf-8\r\n";
        if (compression_type_) {
            header_ << "Content-Encoding: " << compression_type_ << crlf;
        }
        if (close_on_reply_) {
            header_ << "Connection: Close\r\n";
        } else {
            header_ << "Connection: Keep-Alive\r\n"
                << "Keep-Alive: timeout=5, max=100\r\n";
        }
        header_ << crlf;


        header_str_ = std::move(header_.str());

        buffers_.clear();
        {
            const size_t bytes = header_str_.size();
            buffers_.push_back(boost::asio::const_buffer(&header_str_[0], bytes));
            bytes_in_reply_ += bytes;
        }

        if (req.method != http::HttpRequest::Method::HEAD) {
            const size_t bytes = body_str_.size();
            buffers_.push_back(boost::asio::const_buffer(&body_str_[0], bytes));
            bytes_in_reply_ += bytes;
            LOG_TRACE4_FN << " Raw Server reply: {" << header_str_ << body_str_ << "}";
        } else {
            LOG_TRACE4_FN << " Raw Server reply: {" << header_str_ << "}";
        }
    }

    ostringstream header_;
#if USE_BOOST_IOSTREAM
    boost::iostreams::filtering_ostream body_;
    int stream_layers_ {0};
#else
    std::stringstream body_;
#endif

    std::string header_str_;
    std::string body_str_;
    bool close_on_reply_{false};

    std::vector<boost::asio::const_buffer> buffers_;
    const char *compression_type_ = nullptr;
    time_t now_ {0};
    size_t bytes_in_reply_ {0};
};

class HttpDaemonImpl : public HttpDaemon
{
    class Connection;

    // Per-thread connections
    class ConnectionMgr
    {
    public:
        using connection_t = unique_ptr<Connection>;

        struct ConnectionEntry
        {
            ConnectionEntry(const conn_key_t keyVal, connection_t&& conn)
            : key(keyVal), expire{time(nullptr) + 3}, connection(move(conn)) {}

            ConnectionEntry(ConnectionEntry&& v)
            : key{v.key}, expire{v.expire}, connection(move(v.connection))
            {}

            ConnectionEntry& operator = (ConnectionEntry&& v) {
                key = v.key;
                expire = v.expire;
                connection = move(v.connection);
                return *this;
            }

            conn_key_t key {0}; // for fast searching
            time_t expire {0};
            connection_t connection;
        };

        using connections_t = deque<ConnectionEntry>;

        ConnectionMgr(Pipeline& pipeline, HttpDaemonImpl& daemon)
        : pipeline_(pipeline), daemon_(daemon),
        max_connections_{daemon.GetVhostManager().GetConfig().max_connections}
        {
            ScheduleHttpHousekeeping();
        }

        Connection& CreateConnection(tcp::socket&& sck) {

            const auto key = GetNextKey();

            LOG_TRACE1_FN << "Adding socket " << to_int(sck)
                << " to conection-pool " << pipeline_.GetId()
                << " with key #" << key;

            WAR_ASSERT(connections_.find(sckid) == connections_.end());
            connection_t new_conn{new Connection(*this, move(sck), key)};

            Connection& cn = *new_conn;
            connections_.emplace_back(ConnectionEntry{key, move(new_conn)});

            if (max_connections_ && (connections_.size() > max_connections_)) {
                LOG_WARN_FN << "Reached max " << max_connections_
                    << " connections for " << pipeline_.GetId()
                    << ". Will drop the oldest connection.";

                Kill(connections_.begin(), "Too many open connections");
            }

            return cn;
        }

        connections_t::iterator Lookup(const int key, connections_t& list) {

            return std::find_if(list.begin(), list.end(),
                                   [key](const ConnectionEntry& ce) {
                return ce.key == key;
            });
        }

        void Remove(const int key) {
            {
                auto my_entry = Lookup(key, connections_);

                if (my_entry != connections_.end()) {

                    LOG_TRACE2_FN << "Removing socket " << key <<
                        " from conection-pool " << pipeline_.GetId();

                    WAR_ASSERT(is_iterator_in(connections_, my_entry));
                    connections_.erase(my_entry);
                    return;
                }
            }

            // Try the dead sockets list
            {
                LOG_TRACE3_FN << "Searching the dead connections list";
                auto dead_entry = Lookup(key, dead_connections_);

                if (dead_entry != dead_connections_.end()) {
                    LOG_TRACE2_FN << "Removing socket " << key <<
                        " from dead connections pool " << pipeline_.GetId();

                    WAR_ASSERT(is_iterator_in(dead_connections_, dead_entry));
                    dead_connections_.erase(dead_entry);
                    return;
                }
            }

            LOG_WARN_FN << "Failed to lookup socket #" << key;
        }

        void SetEndOfLife(const conn_key_t key, const time_t eol) {
            auto my_entry = Lookup(key, connections_);
            if (my_entry != connections_.end()) {
                my_entry->expire = eol;
                  LOG_TRACE1_FN << "Setting EOL for socket " << key <<
                    " in conection-pool " << pipeline_.GetId();
                return;
            }
            LOG_WARN_FN << "Failed to locate socket " << key;
        }

        Pipeline& GetPipeline() const { return pipeline_; }

        HttpDaemonImpl& GetDaemon() { return daemon_; }

        /*! Get a key (unique id) for the new connection
         *
         * It's not a problem if this counter wraps around,
         * as it is unlikely that any connections will live
         * long enough to cause aa duplicate
         */
        conn_key_t GetNextKey() {
            static atomic<conn_key_t> next_key;

            return ++next_key;
        }

        /*! Statistics for this thread */
        Statistics stats;

    private:
        Pipeline& pipeline_;
        connections_t connections_;
        connections_t dead_connections_;
        HttpDaemonImpl& daemon_;
        const unsigned max_connections_;
        const unsigned housekeeping_interval_in_seconds_{2};

        void ScheduleHttpHousekeeping()
        {
            pipeline_.PostWithTimer(task_t{[this]() {
                RemoveExpieredConnections();
                UpdateStats();
                ScheduleHttpHousekeeping();
            }, "Periodic HTTP connection-housekeeping"},
            housekeeping_interval_in_seconds_ * 1000);
        }

        void UpdateStats() {

            StatisticsManager::GetManager().AddStat(
                StatisticsManager::StatType::HTTP, thd_http_stats_);

            thd_http_stats_.Reset();
        }

        void RemoveExpieredConnections() {
            // Time out old connections

            if (connections_.empty())
                return;

            LOG_TRACE1_FN << "Expiering old connections";

            std::vector<conn_key_t> expiered;
            const auto now = time(nullptr);

            for(const auto& c : connections_) {
                if (c.expire && (c.expire < now)) {
                    expiered.push_back(c.key);
                }
            }

            thd_http_stats_.timeout_dropped_connections += expiered.size();
            RemoveConnections(expiered);
        }

        void RemoveConnections(const std::vector<conn_key_t>& keys) {
            for(const auto key : keys) {
                auto it = Lookup(key, connections_);
                if (it != connections_.end()) {
                    ++thd_http_stats_.overflow_dropped_connections;
                    Kill(it, "Expiered connection");
                }
            }
        }

        void Kill(connections_t::iterator it, const char *explanation) {

            WAR_ASSERT(explanation != nullptr);
            WAR_ASSERT(is_iterator_in(connections_, it));

            LOG_TRACE1_FN << "Killing connection #" << it->key
                << " in pipeline " << pipeline_.GetId()
                << ". " << explanation;

            it->connection->CloseSocket();

            dead_connections_.push_back(move(*it));
            connections_.erase(it);

            WAR_ASSERT(dead_connections_.back().connection
                && "The connection pointer must not be null");

        }
    };

    class Connection
    {
    public:
        Connection(ConnectionMgr& mgr, tcp::socket&& sck, conn_key_t key)
        : socket_{ move(sck) }, mgr_( mgr ),
        timer_(mgr.GetPipeline().GetIoService()),
        key_(key), connection_timeout_{mgr_.GetDaemon().GetVhostManager().GetConfig().connection_timeout}
        {
        }

        void ProcessRequests(boost::asio::yield_context yield) {
            unsigned request_bytes{0};
            bool need_mode_data = true;

            // Make sure Close() is called when we go out of scope
            struct cleanup
            {
                cleanup(Connection& c)
                : connection_{c}, thread_id_{std::this_thread::get_id()} {}

                ~cleanup() {
                   /* I observed that some coroutines are unwinded using the main-
                    * thread after the worker-thread is terminated. This caused a
                    * segfault during shutdown under load as data was partially
                    * deleted (the socket_ was freed' and in some cases also the
                    * thread specific connection manager.
                    *
                    * We therefore must check that we are executing under the
                    * correct thread.
                    *
                    * I don't know if this is a bug or "feature" of asio.
                    */
                    if (thread_id_ != std::this_thread::get_id()) {
                        LOG_DEBUG_FN << "Executing under the wrong thread! Will do nothing.";
                        return;
                    }

                    try {
                        connection_.Close();
                    } WAR_CATCH_ERROR;
                }

            private:
                Connection& connection_;
                const std::thread::id thread_id_;
            } my_cleaner(*this);

            while(socket_.is_open()) {
                try {
                    boost::system::error_code ec;
                    if (need_mode_data) {
                        char *curr_buf = request_buffer_.begin() + request_bytes;
                        const std::size_t bytes = socket_.async_read_some(
                            boost::asio::buffer(curr_buf, request_buffer_.end() - curr_buf), yield[ec]);

                        if (ec) {
                            if (socket_.is_open()) {
                                socket_.close();
                                LOG_DEBUG_FN << "The client closed the connection #" << key_;
                            } else {
                                LOG_DEBUG_FN << "I closed the connection #" << key_;
                            }
                            return;
                        }

                        request_bytes += bytes;
                        thd_http_stats_.bytes_received += bytes;
                    }

                    RequestStats request_stats(thd_http_stats_);

                    need_mode_data = true; // Normally we do...
                    Host::ptr_t host;
                    auto& vhost_mgr =  mgr_.GetDaemon().GetVhostManager();
                    switch(ParseRequest(request_buffer_.begin(), request_bytes)) {
                        case ParseResult::OK:

                           /* Normal processing of a HTTP request.
                            *
                            * Data is written to memory-buffers in current_reply_
                            * and then written asyncrounesly to the socket.
                            *
                            * After the network-write has returned, we close the connection
                            * if the HTTP protocol in use is 1.0, or if the reply was
                            * an error.
                            *
                            * Unexpected exceptions will close the connection without
                            * notifying the user-agent.
                            *
                            * TODO: Deal with HTTP Request headers for non-persistant
                            *       connections.
                            */

                            host = vhost_mgr.GetHost(current_req_.host);

                            try {
                                current_reply_.Prepare(current_req_, vhost_mgr.GetConfig());
                                host->ProcessRequest(current_req_, current_reply_);
                                current_reply_.Reply(200, current_req_);
                                request_stats.state = RequestStats::State::SUCESS;

                            } catch(const Host::ExceptionInvalidResource&) {
                                current_reply_.ReplyWithError(
                                    404, "Not Found", current_req_);
                            } catch(const Host::ExceptionUnsupportedMethod&) {
                                current_reply_.ReplyWithError(
                                    405, "Method not allowed", current_req_);
                            }

                            if (connection_timeout_) {
                                mgr_.SetEndOfLife(key_,
                                                  current_reply_.GetTime() +
                                                  connection_timeout_);
                            }

                            boost::asio::async_write(socket_, current_reply_.GetBuffers(), yield[ec]);

                            if (ec) {
                                if (socket_.is_open()) {
                                    socket_.close();
                                    LOG_DEBUG_FN << "The client closed the connection "
                                        << "while I was replying. Key #" << key_;
                                } else {
                                    LOG_DEBUG_FN << "I closed closed the connection "
                                        << "while I was replying. Key #" << key_;
                                }
                                return;
                            }

                            thd_http_stats_.bytes_sent += current_reply_.GetBytes();

                            if (!socket_.is_open()) {
                                LOG_WARN_FN << "Unexpected closed socket in connection #"
                                    << key_;
                                return;
                            }

                            if (current_reply_.CloseOnReply()) {
                                CloseSocket(); // Close connection
                            } else {
                                // Prepare the input buffer for the next request
                                LOG_TRACE2_FN << "We have consumed "
                                    << current_req_.request_length
                                    << " bytes of our "
                                    << request_bytes
                                    << " bytes incoming data buffer.";

                                if (request_bytes == current_req_.request_length) {
                                    // We consumed the entired received buffer.
                                    // Just re-use it.
                                    request_bytes = 0;
                                } else {
                                    WAR_ASSERT(bytes_parsed < request_bytes);
                                    /* We consumed only parts of the received buffer.
                                    * We need to move memory and prepare
                                    * a new request to complete it.
                                    *
                                    * We also need to deal with the possibility that
                                    * we have received several interleaved requests.
                                    */
                                    // TODO: Add unit tests for this!
                                    memmove(&request_buffer_[0],
                                            &request_buffer_[current_req_.request_length],
                                            request_bytes -  current_req_.request_length);

                                    request_bytes -=  current_req_.request_length;
                                    need_mode_data = false; // Try to parse the data we have
                                }
                            }
                            break;
                        case ParseResult::BAD:
                            request_stats.state = RequestStats::State::BAD;
                            current_reply_.ReplyWithError(400, "Bad Request!", current_req_);
                            boost::asio::async_write(socket_, current_reply_.GetBuffers(), yield);
                            CloseSocket();
                            break;
                        case ParseResult::INCOMPLETE:
                            // Do nothing - we will try to fetch more data
                            request_stats.state = RequestStats::State::IGNORE;
                            break;
                        default:
                            WAR_ASSERT(false && "Invalid ParseResult");
                            CloseSocket();
                    } // switch

                } catch(const war::ExceptionBase& ex) {
                    LOG_ERROR_FN << "Caught exception " << typeid(ex).name() << ": " << ex;
                    CloseSocket(); // End the connection
                    break;
                } catch(const boost::exception& ex) {
                    LOG_ERROR_FN << "Caught boost exception " << typeid(ex).name() << ": " << ex;
                    CloseSocket(); // End the connection
                    break;
                } catch(const std::exception& ex) {
                    LOG_ERROR_FN << "Caught standad exception " << typeid(ex).name() << ": " << ex;

                    CloseSocket(); // End the connection
                    break;
                }
            } // while open
        }

        void Close() {
            if (closed_)
                return;

            closed_ = true;
            LOG_TRACE2_FN << "Terminating socket " << socket_ << " key# " << key_;
            CloseSocket();
            mgr_.Remove(key_);
        }

        void CloseSocket() {
            LOG_TRACE2_FN << "Closing socket " << socket_ << " key# " << key_;
            socket_.close();
        }

    private:
        tcp::socket socket_;
        ConnectionMgr& mgr_;
        boost::asio::steady_timer timer_;

        // the max size we will accept for a HTTP request header
        std::array<char, 1024 * 8> request_buffer_;
        HttpRequest current_req_;
        CurrentReply current_reply_;
        conn_key_t const key_;
        const unsigned connection_timeout_ {0};
        bool closed_ {false};

        enum class ParseResult {
            OK,
            INCOMPLETE,
            BAD
        };

        ParseResult ParseRequest(const char *buffer, size_t bytes) {

            const boost::string_ref my_buffer(buffer, bytes);

            LOG_TRACE1_FN << "Request: " << log::Esc(my_buffer);

            const bool success = ParseHttpRequest(buffer, buffer + bytes, current_req_);

            if (success) {
                LOG_TRACE2_FN << "Successfully parsed the request: " << current_req_
                    << " for connection " << socket_ << " key#" << key_;

                return ParseResult::OK;
            }

            if (buffer + bytes >= (request_buffer_.end() - 1))
                return ParseResult::BAD;

            // Check if we have end of header marker (the parser does not
            // give us this information at the moment I think - need unit tests).
            static const boost::string_ref eoh_marker("\r\n\r\n");
            if (my_buffer.find(eoh_marker) != my_buffer.npos) {
                LOG_WARN_FN << "Failed to parse the request "
                    << log::Esc(my_buffer)
                    << " for connection " << socket_ << " key#" << key_;

                return ParseResult::BAD;
            }

            return ParseResult::INCOMPLETE;
        }
    };

public:
    HttpDaemonImpl(war::Threadpool& ioThreadpool,
                    VhostManager& vhostManager)
    : io_threadpool_(ioThreadpool), vhost_manager_(vhostManager)
    {
        LOG_DEBUG_FN << "Creating httpd object";

        connection_mgr_.reserve(ioThreadpool.GetNumThreads());
        for(size_t i = 0; i < ioThreadpool.GetNumThreads(); ++i) {
            unique_ptr<ConnectionMgr> cm {new ConnectionMgr(ioThreadpool.GetPipeline(i), *this) };
            connection_mgr_.push_back(move(cm));
        }
    }

    void StartAcceptingAt(std::string host, std::string port) {
        auto & pipeline = io_threadpool_.GetAnyPipeline();
        io_service_t& ios = pipeline.GetIoService();

        LOG_DEBUG_FN << "Preparing to listen to: " <<
            log::Esc(host) << " on port " << log::Esc(port);

        tcp::resolver resolver(ios);
        auto endpoint = resolver.resolve({host, port});
        tcp::resolver::iterator end;
        for(; endpoint != end; ++endpoint) {
            tcp::endpoint ep = endpoint->endpoint();
            LOG_NOTICE_FN << "Adding http endpoint: " << ep;

            // Prepare and accept incoming calls in a courutine
            boost::asio::spawn(ios, [ep,&pipeline,&ios,this]
                (boost::asio::yield_context yield) {
                tcp::acceptor acceptor(ios);
                acceptor.open(ep.protocol());
                acceptor.set_option(tcp::acceptor::reuse_address(true));
                acceptor.bind(ep);
                acceptor.listen();

                // Loop over incoming connections
                for (;acceptor.is_open();)
                {
                    boost::system::error_code ec;

                    // Assign the sockets round-robin to pipelines
                    // This is under the assumption that the socket
                    // can have another pipeline than the acceptor!
                    auto & sck_pipeline = io_threadpool_.GetAnyPipeline();
                    tcp::socket socket(sck_pipeline.GetIoService());

                    // Async wait for accept
                    acceptor.async_accept(socket, yield[ec]);
                    if (!ec) {
                        ++thd_http_stats_.connections;
                        LOG_DEBUG << "Incoming connection: "
                            << socket << ' '
                            << socket.local_endpoint()
                            << " <-- "
                            << socket.remote_endpoint();

                        //!! Ugly work-around of problem I had moving the socket
                        //!! trough bind.
                        std::shared_ptr<tcp::socket> socket_ptr { make_shared<tcp::socket>(move(socket)) };

                        // Break out of the coroutine (if necesary)
                        sck_pipeline.Dispatch({bind(&HttpDaemonImpl::OnNewConnection, this,
                                                   ref(sck_pipeline),
                                                   socket_ptr),
                                                   "OnNewConnection"});

                    } else {
                        ++thd_http_stats_.failed_connections;
                        LOG_WARN_FN << "Accept error: " << ec;
                    }
                }
            });
        } // for endpoints
    }

    VhostManager& GetVhostManager() { return vhost_manager_; }

private:
     void OnNewConnection(Pipeline& pipeline, const std::shared_ptr<tcp::socket>& socketPtr) {

        WAR_ASSERT((pipeline.GetId() >= 0) && (pipeline.GetId() < connection_mgr_.size()));
        ConnectionMgr& cm = *connection_mgr_[pipeline.GetId()];
        WAR_ASSERT(cm.GetPipeline().GetId() == pipeline.GetId());

        // Initialize a http session
        Connection& conn = cm.CreateConnection(move(move(*socketPtr)));

        boost::asio::spawn(pipeline.GetIoService(),
                        bind(&Connection::ProcessRequests, &conn,
                        std::placeholders::_1));
    }


private:
    Threadpool& io_threadpool_;
    VhostManager& vhost_manager_;
    std::vector<unique_ptr<ConnectionMgr>> connection_mgr_;;
};


} // namespace impl
} // namespace http

HttpDaemon::ptr_t HttpDaemon::Create(war::Threadpool& ioThreadpool,
                                       VhostManager& vhostManager) {

    return make_shared<http::impl::HttpDaemonImpl>(ioThreadpool, vhostManager);
}

}; // namespace vUbercool

