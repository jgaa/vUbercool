
#pragma once

//#define BOOST_SPIRIT_DEBUG

#include <boost/spirit/include/lex_lexertl.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/qi_kleene.hpp>
#include <boost/spirit/include/qi_plus.hpp>
#include <boost/spirit/include/lex_tokenize_and_parse.hpp>
#include <boost/spirit/include/lex_primitives.hpp>
#include <boost/spirit/include/lex_char_token_def.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_statement.hpp>
#include <boost/spirit/include/phoenix_container.hpp>
#include <boost/spirit/include/phoenix.hpp>
#include <boost/utility/string_ref.hpp>

#include "HttpRequest.h"
#include "war_error_handling.h"
#include "log/WarLog.h"

namespace vUbercool {
namespace http {


enum {
    TOK_CRLF = boost::spirit::lex::min_token_id + 10,
    TOK_STRING
};

template <typename Lexer>
struct HeaderLexer : boost::spirit::lex::lexer<Lexer>
{
    HeaderLexer()
    {
        using boost::spirit::lex::_state;
        using namespace boost::spirit::lex;

        this->self("INITIAL") =
              m_get
            | m_options
            | m_head
            | m_post
            | m_put
            | m_delete
            | m_trace
            | m_connect
            | m_any
            | m_space       [_state = "URI"]
            ;

        this->self("URI") =
              uri
            | uri_space     [_state = "HTTP_VERSION" ]
            ;

        this->self("HTTP_VERSION") =
              http_1_0
            | http_1_1
            | http_crlf_     [_state = "HEADER_NAME" ]
            ;

        this->self("HEADER_NAME") =
              crlf // End of HTTP request header
            | h_host
            | h_dnt         [_state = "HEADER_DNT"]
            | h_accept_encoding [_state = "ACCEPT_ENCODING"]
            | h_any
            | column        [_state = "HEADER_CONTENT"]
            ;

        this->self("HEADER_CONTENT") =
              lws
            | space
            | str
            | hdr_crlf      [_state = "HEADER_NAME"]
            ;

        this->self("HEADER_DNT") =
              dnt_space
            | dnt_on
            | dnt_off
            | dnt_crlf      [_state = "HEADER_NAME" ]
            ;
         this->self("ACCEPT_ENCODING") =
              accept_encoding_space
            | char_(',')
            | accept_encoding_gzip
            | accept_encoding_deflate
            | accept_encoding_crlf      [_state = "HEADER_NAME" ]
            | accept_encoding_qvalue
            | accept_encoding_other
            ;
/*
        LOG_DEBUG << "Tokens:\n"
            << "h_accept_encoding: " << h_accept_encoding.id() << "\n"
            << "accept_encoding_space: " << accept_encoding_space.id() << "\n"
            << "accept_encoding_crlf: " << accept_encoding_crlf.id() << "\n"
            << "accept_encoding_gzip: " << accept_encoding_gzip.id() << "\n"
            << "accept_encoding_deflate: " << accept_encoding_deflate.id() << "\n"
            << "accept_encoding_other: " << accept_encoding_other << "\n"
            ;*/
    }

    boost::spirit::lex::token_def<>
        space{" |\t", ' '},
        dnt_space{" |\t", ' '},
        crlf{"\r\n", TOK_CRLF},
        qvalue{";q=\\d\\.\\d"},
        hdr_crlf{"\r\n", TOK_CRLF},
        dnt_crlf{"\r\n", TOK_CRLF},
        lws{"\r\n ", ' '},
        column{':'},
        m_get{"(?i:GET)",},
        m_options{"(?i:OPTIONS)"},
        m_head{"(?i:HEAD)"},
        m_post{"(?i:POST)"},
        m_put{"(?i:PUT)"},
        m_delete{"(?i:DELETE)"},
        m_trace{"(?i:TRACE)"},
        m_connect{"(?i:CONNECT)"},
        m_any{"\\S+"},
        http_1_0{"(?i:HTTP\\/1\\.0)"},
        http_1_1{"(?i:HTTP\\/1\\.1)"},
        h_host{"(?i:Host)"},
        h_dnt{"(?i:Dnt:)"},
        h_accept_encoding{"(?i:Accept-Encoding:)"},
        h_any{"[\\w\\-]+"},
        m_space{' ', ' '},
        uri_space{' ', ' '},
        http_crlf_{"\r\n", TOK_CRLF},
        c_crlf_{"\r\n", TOK_CRLF},
        dnt_on{"1"},
        dnt_off{"0"},
        accept_encoding_space{dnt_space},
        accept_encoding_crlf{crlf},
        accept_encoding_gzip{"(?i:gzip)"},
        accept_encoding_deflate{"(?i:deflate)"},
        accept_encoding_other{"[a-zA-Z]"},
        accept_encoding_qvalue{qvalue}
        ;

    boost::spirit::lex::token_def<std::string>
        uri{"\\S+"},
        str{"\\S+", TOK_STRING}
        ;
};

template <typename Iterator>
struct RequestGrammar : boost::spirit::qi::grammar<Iterator>
{
    template <typename TokenDef>
    RequestGrammar(TokenDef const& tok)
    : RequestGrammar::base_type(start_)
    {
        using namespace boost::spirit::qi;
        using namespace boost::spirit::qi::labels;

        start_ = request_ >> *header_ >> crlf_;

        request_ =
               method_
            >> ws_
            >> tok.uri      [boost::phoenix::ref(req_.uri) = _1]
            >> ws_
            >> http_version_
            >> crlf_
            ;

        method_ =
              tok.m_get     [boost::phoenix::ref(req_.method) = HttpRequest::Method::GET]
            | tok.m_options [boost::phoenix::ref(req_.method) = HttpRequest::Method::OPTIONS]
            | tok.m_head    [boost::phoenix::ref(req_.method) = HttpRequest::Method::HEAD]
            | tok.m_post    [boost::phoenix::ref(req_.method) = HttpRequest::Method::POST]
            | tok.m_put     [boost::phoenix::ref(req_.method) = HttpRequest::Method::PUT]
            | tok.m_delete  [boost::phoenix::ref(req_.method) = HttpRequest::Method::DELETE]
            | tok.m_trace   [boost::phoenix::ref(req_.method) = HttpRequest::Method::TRACE]
            | tok.m_connect [boost::phoenix::ref(req_.method) = HttpRequest::Method::CONNECT]
            | tok.m_any
            ;

        http_version_ =
              tok.http_1_0  [boost::phoenix::ref(req_.version) = HttpRequest::Version::HTTP_1_0]
            | tok.http_1_1  [boost::phoenix::ref(req_.version) = HttpRequest::Version::HTTP_1_1]
            ;

        header_ =
              host_hdr_
            | dnt_hdr_
            | accept_encoding_hdr_
            | any_hdr_
            ;

        host_hdr_ = tok.h_host >> ':' >> *ws_ >>
              tok.str [boost::phoenix::ref(req_.host) = _1] >> hv_crlf_;
        dnt_hdr_ = tok.h_dnt >> *ws_ >> dnt_value_ >> hv_crlf_;

        accept_encoding_hdr_ = tok.h_accept_encoding >> *(*ws_
            >> accept_encoding_value_ >> *tok.accept_encoding_qvalue >> *char_(','))
            >> hv_crlf_;

        any_hdr_ = tok.h_any >> ':' >>  +(*ws_ >> str_) >> hv_crlf_;

        dnt_value_ =
              tok.dnt_on [boost::phoenix::ref(req_.do_not_track) = true ]
            | tok.dnt_off;

        accept_encoding_value_ =
              tok.accept_encoding_gzip    [boost::phoenix::ref(req_.accept_encoding_gzip) = true ]
            | tok.accept_encoding_deflate [boost::phoenix::ref(req_.accept_encoding_deflate) = true ]
            | tok.accept_encoding_other
            ;

        str_ = token(TOK_STRING);
        crlf_ = token(TOK_CRLF);
        hv_crlf_ = *ws_ >> crlf_;
        ws_ = char_(' ');

        start_.name("start_");
        request_.name("request_");
        header_.name("header_");
        ws_.name("ws_");
        crlf_.name("crlf_");
        any_hdr_.name("any_hdr_");
        host_hdr_.name("host_hdr_");
        str_.name("str_");

#ifdef BOOST_SPIRIT_DEBUG
        BOOST_SPIRIT_DEBUG_NODE(host_hdr_);
        BOOST_SPIRIT_DEBUG_NODE(any_hdr_);
        BOOST_SPIRIT_DEBUG_NODE(start_);
        BOOST_SPIRIT_DEBUG_NODE(request_);
        BOOST_SPIRIT_DEBUG_NODE(ws_);
        BOOST_SPIRIT_DEBUG_NODE(header_);
        BOOST_SPIRIT_DEBUG_NODE(str_);
        BOOST_SPIRIT_DEBUG_NODE(crlf_);
        BOOST_SPIRIT_DEBUG_NODE(accept_encoding_hdr_);
        BOOST_SPIRIT_DEBUG_NODE(accept_encoding_value_);
#endif
    }

    void Reset() {
        req_.Reset();
    }

    // Expect std::move from the returned reference
    HttpRequest &GetReq() { return req_; }

private:
    boost::spirit::qi::rule<Iterator> start_;
    boost::spirit::qi::rule<Iterator> request_;
    boost::spirit::qi::rule<Iterator> header_;
    boost::spirit::qi::rule<Iterator> ws_;
    boost::spirit::qi::rule<Iterator> crlf_;
    boost::spirit::qi::rule<Iterator> hv_crlf_;
    boost::spirit::qi::rule<Iterator> any_hdr_;
    boost::spirit::qi::rule<Iterator> host_hdr_;
    boost::spirit::qi::rule<Iterator> dnt_hdr_;
    boost::spirit::qi::rule<Iterator> method_;
    boost::spirit::qi::rule<Iterator> http_version_;
    boost::spirit::qi::rule<Iterator> str_;
    boost::spirit::qi::rule<Iterator> dnt_value_;
    boost::spirit::qi::rule<Iterator> accept_encoding_hdr_;
    boost::spirit::qi::rule<Iterator> accept_encoding_value_;
    HttpRequest req_;
};

template <typename Iterator>
class HttpRequestParser
{
    using token_type = boost::spirit::lex::lexertl::token<>;
    using lexer_type = boost::spirit::lex::lexertl::actor_lexer<token_type>;
    using iterator_type = HeaderLexer<lexer_type>::iterator_type;

public:
    HttpRequestParser()
    : request_grammer_(header_lexer_)
    {}

    HttpRequestParser(const HttpRequestParser&) = delete;
    HttpRequestParser& operator = (const HttpRequestParser&) = delete;

    bool Parse(Iterator begin, const Iterator end, HttpRequest& req)
    {
        request_grammer_.Reset();
        const Iterator orig_start = begin;
        const bool success = boost::spirit::lex::tokenize_and_parse(
            begin, end, header_lexer_, request_grammer_);

        if (success) {
            req = std::move(request_grammer_.GetReq());
            req.request_length = begin - orig_start;
        }

        return success;
    }


    bool DebugLexer(Iterator begin, Iterator end, std::ostream& out)
    {
        return boost::spirit::lex::tokenize(
            begin, end, header_lexer_,
            [&out](const token_type token) {
                out << "Token: " << token << "\n";
                return true;
            });
    }

private:
    HeaderLexer<lexer_type> header_lexer_;
    RequestGrammar<iterator_type> request_grammer_;
};


} // namespace http
} // namespace vUbercool


