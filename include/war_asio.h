#pragma once

#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>

namespace war {

using io_service_t = boost::asio::io_service;
//typedef boost::asio::io_service * io_service_t;
//using io_service_t = boost::asio::io_service;
//typedef std::shared_ptr<boost::asio::io_service> io_service_t;
typedef std::shared_ptr<boost::asio::deadline_timer> timer_t;
typedef std::shared_ptr<boost::asio::ip::tcp::resolver> resolver_t;

}

class ostream;
ostream& operator << (ostream& o, const boost::asio::ip::tcp::socket& v);

