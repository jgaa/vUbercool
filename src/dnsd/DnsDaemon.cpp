
#include <atomic>
#include <unordered_map>

#include "vUbercool.h"
#include "Message.h"
#include "Zone.h"
#include "Statistics.h"

#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/regex.hpp>
#include <boost/property_tree/ptree.hpp>

#include <warlib/WarLog.h>
#include <warlib/error_handling.h>
#include <warlib/boost_ptree_helper.h>


using namespace war;
using namespace std;
using boost::asio::ip::tcp;
using boost::asio::ip::udp;

int to_int(const udp::socket& sck) {
    return static_cast<int>(const_cast<udp::socket&>(sck).native_handle());
}

std::ostream& operator << (std::ostream& out, const udp::socket& v) {
    return out << "{ socket " << to_int(v) << " }";
}

std::ostream& operator << (std::ostream& out, const std::vector<boost::string_ref>& v)
{
    bool virgin = true;
    for(const auto s : v) {

        if (virgin)
            virgin = false;
        else
            out << '/';

        out << s;
    }

    return out;
}


namespace vUbercool {
namespace dns {
namespace impl {

thread_local Statistics thd_stats_dns_;

class DnsDaemonImpl : public DnsDaemon
{
public:
    struct InvalidQuery : public war::ExceptionBase {};
    struct Refused : public war::ExceptionBase {};
    struct UnimplementedQuery : public war::ExceptionBase {};
    struct UnknownDomain : public war::ExceptionBase {};
    struct UnknownSubDomain : public war::ExceptionBase {};
    struct NoSoaRecord : public war::ExceptionBase {};
    struct Truncated : public war::ExceptionBase {};
    using buffer_t = std::vector<char>;
    using zones_t = vector<const Zone *>;

    DnsDaemonImpl(war::Threadpool& ioThreadpool,
                  VhostManager& vhostManager)
    : io_threadpool_(ioThreadpool), vhost_manager_(vhostManager)
    {
        LOG_DEBUG_FN << "Creating dnsd object";
    }


    void StartReceivingUdpAt(std::string host, std::string port) override {
        auto & pipeline = io_threadpool_.GetPipeline(0);
        io_service_t& ios = pipeline.GetIoService();

        LOG_DEBUG_FN << "Preparing to listen to: " <<
        log::Esc(host) << " on UDP port " << log::Esc(port);

        udp::resolver resolver(ios);
        auto endpoint = resolver.resolve({host, port});
        udp::resolver::iterator end;
        for(; endpoint != end; ++endpoint) {
            udp::endpoint ep = endpoint->endpoint();
            LOG_NOTICE_FN << "Adding dnsd endpoint: " << ep;

            auto& dns_pipeline = io_threadpool_.GetAnyPipeline();

            // Start receiving incoming requestss
            boost::asio::spawn(
                dns_pipeline.GetIoService(), [ep,&dns_pipeline,&ios,this]
                (boost::asio::yield_context yield) {

                // Assign the sockets round-robin to pipelines
                udp::socket socket(dns_pipeline.GetIoService());
                socket.open(ep.protocol());
                socket.set_option(boost::asio::socket_base::reuse_address(true));
                socket.bind(ep);
                buffer_t query_buffer(max_query_buffer_);
                boost::system::error_code ec;
                buffer_t reply_buffer(max_reply_buffer_);
                udp::endpoint sender_endpoint;
                ScheduleDnsHousekeeping(dns_pipeline);

                // Loop over incoming requests
                for (;socket.is_open();) {
                    const size_t bytes_received = socket.async_receive_from(
                        boost::asio::buffer(query_buffer),
                        sender_endpoint,
                        yield);

                    thd_stats_dns_.bytes_received += bytes_received;
                    RequestStats request_stats(thd_stats_dns_);

                    if (!ec) {
                        ++num_incoming_connections_;
                        LOG_TRACE2_FN << "Incoming Query: "
                            << socket << ' '
                            << socket.local_endpoint()
                            << " <-- "
                            << sender_endpoint
                            << " of " << bytes_received << "  bytes.";

                        try {

                            ProcesssQuerey(&query_buffer[0],
                                           bytes_received,
                                           reply_buffer,
                                           request_stats);

                        } catch(const UnknownDomain&) {
                            // Do not reply
                            continue;
                        } WAR_CATCH_ERROR;

                        if (socket.is_open() && !reply_buffer.empty()) {
                            LOG_TRACE3_FN << "Replying " << reply_buffer.size()
                                << " bytes to " << socket;

                            socket.async_send_to(
                                boost::asio::buffer(reply_buffer),
                                sender_endpoint,
                                yield);

                            if (!ec) {
                                request_stats.state = RequestStats::State::SUCESS;
                                thd_stats_dns_.bytes_sent += reply_buffer.size();
                            }
                        }
                    } else { // error
                        LOG_DEBUG_FN << "Receive error " << ec << " from socket " << socket;
                        ++num_failed_incoming_connections_;
                    }
                }
            });
        } // for endpoints
    }

    VhostManager& GetVhostManager() { return vhost_manager_; }

    /*! Setup zones from a recursive boost property tree structure. */
    void SetupZones(const boost::property_tree::ptree& configuration) {
        auto root_zones = AddZones(configuration);

        while(!root_zones.empty()) {
             zone_mgr_.AddRootZone(move(root_zones.back()));
             root_zones.pop_back();
        }
    }

private:
    // This is run individually for each thread processing DNS queries
    void ScheduleDnsHousekeeping(Pipeline& pipeline)
    {
        pipeline.PostWithTimer(task_t{[this, &pipeline]() {
            UpdateStats();
            ScheduleDnsHousekeeping(pipeline);
        }, "Periodic DNS connection-housekeeping"},
        housekeeping_interval_in_seconds_ * 1000);
    }

    void UpdateStats() {

        StatisticsManager::GetManager().AddStat(
            StatisticsManager::StatType::DNS, thd_stats_dns_);

        thd_stats_dns_.Reset();
    }

    void ProcesssQuerey(const char *queryBuffer,
                        size_t queryLen,
                        buffer_t& replyBuffer,
                        RequestStats& requestStats)
    {
        replyBuffer.resize(0);
        replyBuffer.reserve(max_reply_buffer_);
        MessageHeader header(queryBuffer, queryLen);

        try {
            if (header.GetQr() != 0) {
                WAR_THROW_T(InvalidQuery, "Not a question");
            }

            LOG_TRACE3_FN << "Processing request " << header.GetId();

            switch(header.GetOpcode()) {
                case 0: // Standard query
                    ProcessQuestions(queryBuffer, queryLen, replyBuffer, header);
                    break;
                case 1: // Inverse query
                case 2: // Server status request
                default:
                    CreateErrorReply(MessageHeader::Rcode::NOT_IMPLEMENTED,
                                     header, replyBuffer);
            }
        } catch(const InvalidQuery& ex) {
            LOG_DEBUG_FN << "Caught exception: " << ex;
            CreateErrorReply(MessageHeader::Rcode::FORMAT_ERROR,
                             header, replyBuffer);
            requestStats.state = RequestStats::State::BAD;
            return;
        } catch(const UnknownDomain& ex) {
            /* This exception indicate that we are not authorative for this
             * domain, so we will not reply.
             */
            LOG_TRACE3_FN << "Caught exception for unknown domain: " << ex;
            ++thd_stats_dns_.unknown_names;
            throw;
        } catch(const UnknownSubDomain& ex) {
            LOG_TRACE3_FN << "Caught exception for unknown sub-domain: " << ex;
            // Unknown subdomain for a zone we own. Reply with error.
            CreateErrorReply(MessageHeader::Rcode::NAME_ERROR,
                             header, replyBuffer);
            ++thd_stats_dns_.unknown_names;
            return;
        } catch(const NoSoaRecord& ex) {
            LOG_DEBUG_FN << "Caught exception: " << ex;
            CreateErrorReply(MessageHeader::Rcode::SERVER_FAILURE,
                             header, replyBuffer);
            return;
        } catch(const Refused& ex) {
            LOG_DEBUG_FN << "Caught exception: " << ex;
            CreateErrorReply(MessageHeader::Rcode::REFUSED,
                                header, replyBuffer);
            return;
        } catch(const LabelHeader::NoLabelsException& ex) {
            LOG_DEBUG_FN << "Caught exception: " << ex;
            CreateErrorReply(MessageHeader::Rcode::FORMAT_ERROR,
                             header, replyBuffer);
            requestStats.state = RequestStats::State::BAD;
            return;
        } catch(const LabelHeader::IllegalLabelException& ex) {
            LOG_DEBUG_FN << "Caught exception: " << ex;
            CreateErrorReply(MessageHeader::Rcode::FORMAT_ERROR,
                             header, replyBuffer);
            requestStats.state = RequestStats::State::BAD;
            return;
        } catch(const LabelHeader::IllegalPointereException& ex) {
            LOG_DEBUG_FN << "Caught exception: " << ex;
            CreateErrorReply(MessageHeader::Rcode::FORMAT_ERROR,
                             header, replyBuffer);
            requestStats.state = RequestStats::State::BAD;
            return;
        } catch(const war::ExceptionBase& ex) {
            LOG_DEBUG_FN << "Caught exception: " << ex;
            CreateErrorReply(MessageHeader::Rcode::SERVER_FAILURE,
                             header, replyBuffer);
            return;
        } catch(const boost::exception& ex) {
            LOG_DEBUG_FN << "Caught exception: " << ex;
            CreateErrorReply(MessageHeader::Rcode::SERVER_FAILURE,
                             header, replyBuffer);
            return;
        } catch(const std::exception& ex) {
            LOG_DEBUG_FN << "Caught exception: " << ex;
            CreateErrorReply(MessageHeader::Rcode::SERVER_FAILURE,
                             header, replyBuffer);
            return;
        }

    }

    void ProcessQuestions(const char *queryBuffer,
                         size_t queryLen,
                         buffer_t& replyBuffer,
                         const MessageHeader& header) {
        size_t bytes_left = queryLen - header.GetSize();
        LabelHeader::labels_t pointers;

        bool truncated = false; // If we run out of buffer space
        bool authorative = true;
        uint16_t num_questions {0};
        uint16_t num_answers {0};
        uint16_t num_ns {0};
        uint16_t num_opt {0};

        AnswerBase::existing_labels_t existing_labels;
        zones_t authorative_zones;
        zones_t ns_zones;

        // Reserve buffer-space for the message-header
        MessageHeader reply_hdr(header);
        replyBuffer.resize(reply_hdr.GetSize());

        LOG_TRACE3_FN << "Header: " << header.GetQdCount()
            << " questions ";

        /*! Since we use DNS header compression, we need to know the
         * offsets of the name-labels in the reply buffer. That means
         * that we have to copy all the question's into the buffer
         * before we start adding answers.
         * We therefore do a two-pass loop over the questions; first
         * to parse and copy them, and then to process them.
         */

        std::vector<Question> questions;
        for(uint16_t i = 0; i < header.GetQdCount(); ++i) {
            if (bytes_left == 0) {
                WAR_THROW_T(InvalidQuery, "Buffer underrun preparing next query");
            }

            const char *p = queryBuffer + (queryLen - bytes_left);

            questions.emplace_back<Question>(
                {p, bytes_left, pointers, queryBuffer,
                    static_cast<uint16_t>(replyBuffer.size())});
            Question& question = questions.back();

            const auto qclass = question.GetQclass();
            if (qclass != 1 && qclass != 255) {
                WAR_THROW_T(Refused, "Unsupported Qclass");
            }

            // Copy the question to the reply
            const size_t new_len = replyBuffer.size() + question.GetSize();
            if (new_len < max_reply_buffer_) {
                // Copy the original question unmodified
                const size_t len =  question.GetSize();
                std::copy(p, p + len, back_inserter(replyBuffer));
                ++num_questions;
            } else {
                WAR_THROW_T(InvalidQuery, "Too many or too thick questions!");
            }
        }

        // Process the questions and append the answers to the reply-buffer
        // until we are done or the buffer overflows (in which case the latest
        // answer will be rolled back and the truncated flag set).
        try {
            for(const Question& question : questions) {

                LOG_TRACE2_FN << "Request ID " << header.GetId()
                    << " asks about type " << question.GetQtype()
                    << " class " << question.GetQclass()
                    << " regarding " << log::Esc(question.GetDomainName());

                ProcessQuestion(question, replyBuffer, authorative, num_answers,
                                existing_labels, authorative_zones, ns_zones);
            }

            // Add NS references in the AUTH section
            for(const Zone *z : authorative_zones) {
                ProcessNsAuthZone(*z, num_ns, replyBuffer, existing_labels, ns_zones);
            }

            // Add IP addresse for NS servers we have named, and have data about
            for(const Zone *z : ns_zones) {
                ProcessNsZone(*z, num_opt, replyBuffer, existing_labels);
            }

        } catch (const Truncated& ex) {
            LOG_WARN_FN << "Reply is truncated! " << ex;
            truncated = true;
        }

        // Write out the message-header
        WAR_ASSERT(replyBuffer.size() > reply_hdr.GetSize());
        reply_hdr.SetTc(truncated);
        reply_hdr.SetQr(true);
        reply_hdr.SetRa(false);
        reply_hdr.SetAa(authorative);
        reply_hdr.SetQdCount(num_questions);
        reply_hdr.SetAnCount(num_answers);
        reply_hdr.SetNsCount(num_ns);
        reply_hdr.SetArCount(num_opt);
        reply_hdr.Write(&replyBuffer[0]);
    }

    void ProcessQuestion(const Question& question, buffer_t& replyBuffer,
                         bool& authorative, uint16_t& numAnswers,
                         AnswerBase::existing_labels_t& existingLabels,
                         zones_t& authorativeZones, zones_t& nsZones)
    {
        bool is_authorative_during_lookup {false};
        const Zone *zone = zone_mgr_.Lookup(question.GetLabels(),
                                            &is_authorative_during_lookup);
        if (!zone) {
            LOG_TRACE1_FN << "Failed to lookup " << log::Esc(question.GetDomainName());

            if (is_authorative_during_lookup) {
                WAR_THROW_T(UnknownSubDomain, "Unknown sub-domain");
            }

            WAR_THROW_T(UnknownDomain, "Unknown domain");
        }

        // If any of the questions regards a zone where we are not authorative,
        // we do not set the authorative flag in the message header.
        const Zone *soa_zone = GetSoaZone(zone);
        if (!soa_zone || !soa_zone->authorative)
            authorative = false;

        const auto qtype = question.GetQtype();
        if (qtype == 1 /* A */ || qtype == 255) {
            if (!zone->cname.empty()) {
                // This is a CNAME node
                WAR_ASSERT(zone->a.empty());
                std::string full_cname = StoreCname(zone, question, replyBuffer,
                                                    numAnswers, existingLabels);

                if (qtype == 1) {
                    // Add the A records for the cname if available
                    zone = zone_mgr_.Lookup(ToFraments(full_cname));
                    if (zone) {
                        for(const auto& ip : zone->a) {
                            RdataA a_answer(full_cname, ip, existingLabels);
                            Store(a_answer, replyBuffer, numAnswers);
                        }
                    }
                }
            } else {
                for(const auto& ip : zone->a) {
                    RdataA a_answer(question.GetOffset(), ip, existingLabels);
                    Store(a_answer, replyBuffer, numAnswers);
                }
            }
        } else if (qtype == 5 /* CNAME */) {
            if (!zone->cname.empty()) {
                StoreCname(zone, question, replyBuffer, numAnswers, existingLabels);
            }
        } else if (qtype == 6 /* SOA */) {
            if (!soa_zone) {
                WAR_THROW_T(NoSoaRecord, "No SOA record found");
            }
            RdataSoa soa_answer(question.GetOffset(), *soa_zone, existingLabels);
            Store(soa_answer, replyBuffer, numAnswers);
            AddZone(authorativeZones, soa_zone);
        }
        else if (qtype == 2 /* NS */) {
            AddZone(authorativeZones, soa_zone);
        }
        else if (qtype == 15 /* MX */) {
            for(const auto mx : zone->mx) {
                const std::string fdqn = mx.GetFdqn();

                RdataMx mx_answer(question.GetOffset(), fdqn, mx.priority, existingLabels);
                Store(mx_answer, replyBuffer, numAnswers);

                AddZone(nsZones, zone_mgr_.Lookup(ToFraments(fdqn)));
            }
        }
        if (qtype == 255) {
            AddZone(authorativeZones, soa_zone);
        }
    }

    void AddZone(zones_t& authorativeZones, const Zone *zone)
    {
        if (!zone)
            return; // Not an error

        if (std::find(authorativeZones.begin(), authorativeZones.end(), zone)
            == authorativeZones.end()) {
            authorativeZones.push_back(zone);
        }
    }

    /*! Store the zone in the AUTH section. */
    void ProcessNsAuthZone(const Zone& zone, uint16_t& numAuth,
                           buffer_t& replyBuffer,
                           AnswerBase::existing_labels_t& existingLabels,
                           zones_t& nsZones)
    {
        // Store Name Servers
        for(const auto ns : zone.ns) {
            const string nameserver_fdqn = ns.GetFdqn();
            RdataNs auth_ns(zone.GetDomainName(), nameserver_fdqn, existingLabels);
            Store(auth_ns, replyBuffer, numAuth);

            AddZone(nsZones, zone_mgr_.Lookup(ToFraments(nameserver_fdqn)));
        }
    }

    /*! Store the zone in the AUTH section. */
    void ProcessNsZone(const Zone& zone, uint16_t& numOpt,
                           buffer_t& replyBuffer,
                           AnswerBase::existing_labels_t& existingLabels)
    {
        for(const auto& ip : zone.a) {
            RdataA a_answer(zone.GetDomainName(), ip, existingLabels);
            Store(a_answer, replyBuffer, numOpt);
        }

    }

    std::string StoreCname(const Zone* zone, const Question& question,
                           buffer_t& replyBuffer, uint16_t& numAnswers,
                           AnswerBase::existing_labels_t& existingLabels)
    {
        WAR_ASSERT(!zone->cname.empty());

        std::string full_cname = zone->cname;
        if (full_cname.back() != '.') {
            if (zone->parent) {
                full_cname += '.';
                full_cname += zone->parent->GetDomainName();
            } else {
                LOG_WARN_FN << "No parent zone in CNAME scope :"
                << log::Esc(zone->GetDomainName());
            }
        }

        RdataCname cname_answer(question.GetOffset(), full_cname, existingLabels);
        Store(cname_answer, replyBuffer, numAnswers);

        return full_cname;
    }

    const Zone *GetSoaZone(const Zone* zone) const
    {
        const Zone *z { nullptr };
        for(z = zone; z && !z->soa; z = zone->parent)
            ;

        return z;
    }

    void Store(AnswerBase& a, buffer_t& buffer, uint16_t& numAnswers)
    {
        a.Write(buffer);
        if (buffer.size() > max_reply_buffer_) {
            a.Revert(buffer);
            WAR_THROW_T(Truncated, "Truncated reply");
        }
        ++numAnswers;
    }

    void CreateErrorReply(MessageHeader::Rcode errCode,
                          const MessageHeader& hdr,
                          buffer_t& replyBuffer)
    {
        MessageHeader reply_hdr(hdr);
        replyBuffer.resize(reply_hdr.GetSize());
        reply_hdr.SetQr(true);
        reply_hdr.SetRcode(errCode);
        reply_hdr.ResetAllCounters();
        reply_hdr.Write(&replyBuffer[0]);

        LOG_DEBUG_FN << "Query with ID " << hdr.GetId()
            << " failed with error " << static_cast<int>(errCode);
    }

    ZoneMgr::zones_t AddZones(const boost::property_tree::ptree& pt, Zone *parent = nullptr)
    {
        ZoneMgr::zones_t zones;

        for(auto it = pt.begin(); it != pt.end(); ++it) {
            std::string name = it->first;
            if (!name.empty() && (name[0] == '@')) {
                name.erase(name.begin());
                // We have a zone.
                zones.push_back(move(AddZone(name, it->second, parent)));
            }
        }

        return zones;
    }

    std::unique_ptr<Zone> AddZone(const std::string& name,
                                  const boost::property_tree::ptree& pt,
                                  Zone *parent)
    {
        bool authorative = pt.get<bool>("authorative", false);
        std::unique_ptr<Zone> zone{ new Zone{name, parent, authorative } };

        if (auto soa = pt.get_child_optional("soa")) {
            zone->soa.reset(new Zone::soa_t(zone.get()));
            zone->soa->rname = soa->get<string>("rname", zone->soa->rname);
            zone->soa->serial = soa->get<uint32_t>("rname", zone->soa->serial);
            zone->soa->refresh = soa->get<uint32_t>("rname", zone->soa->refresh);
            zone->soa->retry = soa->get<uint32_t>("rname", zone->soa->retry);
            zone->soa->expire = soa->get<uint32_t>("rname", zone->soa->expire);
            zone->soa->minimum = soa->get<uint32_t>("rname", zone->soa->minimum);
        }

        if (auto ns = pt.get_child_optional("ns")) {
            for(auto it = ns->begin() ; it != ns->end(); ++it) {
                zone->ns.emplace_back(Zone::ns_t{it->first, zone.get()});
            }
        }

        if (auto mx = pt.get_child_optional("mx")) {
            for(auto it = mx->begin() ; it != mx->end(); ++it) {
                zone->mx.emplace_back(Zone::mx_t{it->first,
                    it->second.get_value<uint16_t>(), zone.get()});
            }
        }

        if (auto cname = pt.get_optional<string>("cname")) {
            zone->cname = *cname;
        }

        if (auto a = pt.get_child_optional("a")) {
            for(auto it = a->begin() ; it != a->end(); ++it) {
                zone->a.push_back(boost::asio::ip::address_v4::from_string(it->first));
            }
        }


        zone->children = move(AddZones(pt, zone.get()));


        return zone;
    }

    Threadpool& io_threadpool_;
    VhostManager& vhost_manager_;
    atomic<uint64_t> num_incoming_connections_;
    atomic<uint64_t> num_failed_incoming_connections_;
    static const size_t max_query_buffer_ = 512;
    static const size_t max_reply_buffer_ = 512;
    ZoneMgr zone_mgr_;
    const unsigned housekeeping_interval_in_seconds_{5};
};

} // impl
} // dns

DnsDaemon::ptr_t DnsDaemon::Create(Threadpool& ioThreadpool,
                                   VhostManager& vhostManager,
                                   const boost::property_tree::ptree& configuration) {

    auto dns = make_shared<dns::impl::DnsDaemonImpl>(ioThreadpool, vhostManager);

    dns->SetupZones(configuration);

    return dns;
}

} // vUbercool
