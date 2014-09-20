
#include <war_error_handling.h>
#include <log/WarLog.h>

#include "HttpRequest.h"


using namespace war;
using namespace std;

std::ostream& operator << (std::ostream &out, const vUbercool::http::HttpRequest& v)
{
    return out << "{ "
        << v.GetHttpVersionName()
        << ' '
        << v.GetMethodName()
        << " host: " << log::Esc(v.host)
        << " port: " << v.port
        << " uri: " << log::Esc(v.uri)
        << " }";
}

namespace vUbercool {
namespace http {

const std::string& HttpRequest::GetMethodName() const
{
    static const vector<string> method_names = { "GET", "OPTIONS",
        "HEAD", "POST", "PUT", "DELETE", "TRACE", "CONNECT", "UNKNOWN"};

    WAR_ASSERT(static_cast<size_t>(method) >= 0
        && static_cast<size_t>(method) < method_names.size());

    return method_names[static_cast<size_t>(method)];
}

const std::string& HttpRequest::GetHttpVersionName() const
{
    static const vector<string> version_names = {
        "HTTP/1.0", "HTTP/1.1", "UNKNOWN" };

    WAR_ASSERT(static_cast<size_t>(version) >= 0
        && static_cast<size_t>(version) < version.size());

    return version_names[static_cast<size_t>(version)];
}

}} // namespaces
