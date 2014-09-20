#pragma once


#include <string>
#include <cinttypes>
#include <boost/utility/string_ref.hpp>

namespace vUbercool {
namespace http {


struct HttpRequest
{
    enum class Version {
        HTTP_1_0,
        HTTP_1_1,
        UNKNOWN
    } version = Version::UNKNOWN;

    enum class Method {
        GET,
        OPTIONS,
        HEAD,
        POST,
        PUT,
        DELETE,
        TRACE,
        CONNECT,
        UNKNOWN
    } method = Method::UNKNOWN;

    //boost::string_ref uri_;

    std::uint16_t port {0};
    bool do_not_track {false};
    uint32_t request_length {0};
    std::string uri;
    std::string host;
    bool accept_encoding_deflate {false};
    bool accept_encoding_gzip {false};

    void Reset() {
        version = Version::UNKNOWN;
        method = Method::UNKNOWN;
        port = 0;
        do_not_track = false;
        request_length = 0;
        uri.clear();
        host.clear();
        accept_encoding_deflate = false;
        accept_encoding_gzip = false;
    }

    /* Fake reset in order to measure the performance
     * without using the http request parser at all.
     */
    void FakeReset() {
        version = Version::HTTP_1_1;
        method = Method::GET;
        port = 0;
        do_not_track = false;
        request_length = 0;
        uri = "/";
        host = "1.onebillionsites.com:8080";
    }

    void operator = (HttpRequest&& v)
    {
        version = v.version;
        method = v.method;
        port = v.port;
        do_not_track = v.do_not_track;
        request_length = v.request_length;
        uri = std::move(v.uri);
        host = std::move(v.host);
        accept_encoding_deflate = v.accept_encoding_deflate;
        accept_encoding_gzip = v.accept_encoding_gzip;
    }

    bool CloseOnReply() const {
        return version == Version::HTTP_1_0;
    }

    const std::string& GetMethodName() const;
    const std::string& GetHttpVersionName() const;
};

}} // namespaces

std::ostream& operator << (std::ostream &out, const vUbercool::http::HttpRequest& v);

