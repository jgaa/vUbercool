/*
 * In order to reduce compile-time, I've instatiated
 * the Http Request Paser in this separate file.
 *
 * It takes "forever" to compile the boost::spirit based
 * lexer and parser, so this allows me to change the
 * http server implementation without having to wait
 * for boost::spirit.
 *
 * In the current implementation, we assume the entire
 * HTTP request to be in the input buffer. So there
 * are no async operations during the parse operation.
 * This allows us to use thread local storage to cache the
 * parser, something that avoids cpu-intensive parser
 * instatiation for each request.
 */

#define WITHOUT_HTTP_REQUEST_PARSER 0

#include "HttpRequestParser.h"

namespace vUbercool {
namespace http {
namespace impl {

bool ParseHttpRequest(const char *begin, const char *end, HttpRequest& req)
{

#if WITHOUT_HTTP_REQUEST_PARSER
    req.FakeReset();
    const boost::string_ref buf(begin, end - begin);
    req.request_length = buf.find("\r\n\r\n") + 4;

    return true;
#else

    thread_local HttpRequestParser<const char *> parser;

//     {
//         std::ostringstream out;
//         parser.DebugLexer(begin, end, out);
//         LOG_DEBUG << "Lexer:" << out.str();
//     }

    const bool success = parser.Parse(begin, end, req);

    return success;
#endif
}

}}}//namespaces

