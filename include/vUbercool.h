#pragma once

#include <unordered_map>
#include <memory>

#include <boost/utility/string_ref.hpp>
#include <boost/concept_check.hpp>
#include <boost/property_tree/ptree_fwd.hpp>

#include <war_asio.h>
#include <tasks/WarThreadpool.h>

/*! vUbercool - High performance web application server
 *
 * \namespace vUbercool Interfaces for the vUbercool project.
 */

namespace vUbercool {

// Forward declarations
class VhostManager;
namespace http {
    struct HttpRequest;
} // http


enum class ApplicationVersion
{
    MAJOR = 0,
    MINOR = 10
};

class DnsDaemon
{
public:
    using ptr_t = std::shared_ptr<DnsDaemon>;

    DnsDaemon() = default;
    virtual ~DnsDaemon() = default;
    DnsDaemon(const DnsDaemon&) = delete;
    DnsDaemon& operator = (const DnsDaemon&) = delete;

    /*! Start to processincoming UDP queries at this address */
    virtual void StartReceivingUdpAt(std::string host, std::string port) = 0;

    /*! Fabric */
    static ptr_t Create(war::Threadpool& ioThreadpool,
                        VhostManager& vhostManager,
                        const boost::property_tree::ptree& configuration);
};

struct HttpConfig
{
    HttpConfig() {};

    using site_id_t = uint32_t;

    using name_id_map_t = std::unordered_map<std::string /* name */,
        site_id_t /* Site ID */>;
    using name_name_map_t = std::unordered_map<std::string /* key-name */,
        std::string /* value-name */>;

    /*! The fixed number of sites that can be addressed */
    site_id_t num_sites {0};

    /*! If true, the storage for the sites will be recreated and rest
     *
     * This will set all page-counters to 0
     */
    bool recreate_mmap_file {false};

    /*! Aliases, so that a name like "blog" can be resolved to a Site ID */
    name_id_map_t site_aliases;

    /*! Traditional HTTP Redirects */
    name_name_map_t http_redirects;

    /*! The root-path for the webserver on storage */
    std::string root_path;

    /*! The path to the big mmap file for site persistant data */
    std::string data_mmap_file;

    /*! Allow dflate compression in replies */
    bool allow_deflate {false};

    /*! Allow gzip compression in replies */
    bool allow_gzip {false};

    /*! Time in seconds before a connection times out */
    unsigned connection_timeout {3};

    /*! ; Max concurrent connections.
     *
     * If exeeded, the oldest connection will be closed.
     * 0 means no enforcement.
     */
    unsigned max_connections {0};
};

class HttpReply
{
public:
    HttpReply() = default;
    HttpReply(const HttpReply&) = delete;
    HttpReply& operator = (const HttpReply&) = delete;
    virtual ~HttpReply() = default;

    /*! Data to be sent in the reply body;
     */
    virtual std::ostream& Out() = 0;

    /*! Send the reply to the server */
    virtual void Reply(int code, const http::HttpRequest& req) = 0;

    /*! Error
     *
     * Cancel whatever that's in the output buffer and send
     * the error in stead.
     */
    virtual void ReplyWithError(int code, const std::string& content,
                                const http::HttpRequest& req) = 0;

    /*! Reset state between requests */
    virtual void Reset() = 0;

    /*! Check if the connection shall be closed after the reply is sent */
    virtual bool CloseOnReply() const = 0;

    /*! Prepare the buffers according to the (new) request status
     *
     * Typically sets up the body-buffer for the preffered
     * compression method
     */
    virtual void Prepare(const http::HttpRequest& req,
        const HttpConfig& config) = 0;

    /*! Get the time of the request.
     *
     * This is the time that is reported in the HTTP reply
     */
    virtual std::time_t GetTime() const = 0;
};


class HttpDaemon
{
public:
    using ptr_t = std::shared_ptr<HttpDaemon>;

    HttpDaemon() = default;
    virtual ~HttpDaemon() = default;
    HttpDaemon(const HttpDaemon&) = delete;
    HttpDaemon& operator = (const HttpDaemon&) = delete;

    /*! Start to listen for incoming calls at this address */
    virtual void StartAcceptingAt(std::string host, std::string port) = 0;

    /*! Fabric */
    static ptr_t Create(war::Threadpool& ioThreadpool,
                        VhostManager& vhostManager);
};


class Host
{
public:
    struct Config
    {
        Config(const std::string& rootPath)
            : root_path(rootPath)
            {}

        const std::string& root_path;
    };

    using ptr_t = std::unique_ptr<Host>;
    struct Exception : public war::ExceptionBase {};
    struct ExceptionUnsupportedMethod : public Exception {};
    struct ExceptionInvalidResource : public Exception {};

    Host() = default;
    Host(const Host&) = delete;
    Host& operator = (const Host&) = delete;

    virtual void ProcessRequest(http::HttpRequest& request,
                        HttpReply& reply) = 0;


    virtual ~Host() = default;

    virtual const Config& GetConfig() const = 0;
};

class VhostManager
{
public:
    using ptr_t = std::unique_ptr<VhostManager>;

    struct Exception : public war::ExceptionBase {};
    struct ExceptionUnknownHost : public Exception {};

    struct ExceptionRedirect : public Exception
    {
        ExceptionRedirect() = delete;
        ExceptionRedirect(const ExceptionRedirect&) = default;
        ExceptionRedirect(const std::string& redirect)
            : redirect_to { redirect } {}

        const std::string redirect_to;
    };


    VhostManager() = default;
    virtual ~VhostManager() = default;
    VhostManager& operator = (const VhostManager&) = delete;

    /*! Get a virtual host from it's name.
     *
     * \param name The name exact matching (icase) the
     *      Host: header content in the client request.
     *      An empty string will return the default host.
     *
     * \exception ExceptionUnknownHost if the host is unknown
     */
    virtual Host::ptr_t GetHost(const std::string& name) = 0;

    virtual const HttpConfig& GetConfig() const = 0;

    static ptr_t Create(war::Threadpool& tp, HttpConfig& config);
};

} // namespace


