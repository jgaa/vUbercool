#include "Hosts.h"
#include "../httpd/HttpRequest.h"
#include <warlib/WarLog.h>
#include <warlib/error_handling.h>

using namespace war;
using namespace std;


namespace vUbercool {
namespace impl {

using namespace http;

#if WITH_PAGE_COUNTER_SYNCRONIZATION
    DefaultHost::mutex_array_t DefaultHost::mutex_;
#endif

DefaultHost::DefaultHost(unsigned int id, PackedHostData& data, const Config& config)
: id_(id), data_(data), config_(config)
{
}

void DefaultHost::ProcessRequest(http::HttpRequest& request,
                    HttpReply& reply) {
    static const string crlf{"\r\n"};
    static const string root{"/"};

    if ((request.method == HttpRequest::Method::GET)
        || (request.method == HttpRequest::Method::HEAD)) {

        /* Just for fun, allow any url-arguments as long as the path is root..
         */
        const auto pos = request.uri.find("?");
        boost::string_ref real_path(&request.uri[0],
            pos == request.uri.npos ? request.uri.size() : pos);

        if (real_path != root) {
            WAR_THROW_T(ExceptionInvalidResource, "Not root");
        }

        // TODO: Use const static strings where possible.
        reply.Out() << R"(<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML 2.0//EN">)" << crlf
            << R"(<html><head>)" << crlf
            << R"(<title>Welcome dear spider</title>)"
            << R"(<!-- site id# )" << id_ << R"( -->)" << crlf
            << R"(</head><body>)" << crlf
            << R"(<h1>This page is for you</h1>)" << crlf
            << R"(<p>Please consume this jolly content with pride.</p>)" << crlf
            << R"(<p>This fantastic, distinct site has had )" << IncHitCount() << R"( page-hits so far.</p>)" << crlf
            << (request.do_not_track ? "<p>(I am not tracking you!)</p>" : "")
            << R"(<hr>)" << crlf
            << R"(<address>vUbercool/)"
            << static_cast<int>(ApplicationVersion::MAJOR)
            << '.'
            << static_cast<int>(ApplicationVersion::MINOR)
            << R"( Very High Performance HTTP/Web Application Server</address>)" << crlf
            << R"(</body></html>)" << crlf
            << crlf;
    } else {
        WAR_THROW_T(ExceptionUnsupportedMethod, request.GetMethodName());
    }
}

}}
