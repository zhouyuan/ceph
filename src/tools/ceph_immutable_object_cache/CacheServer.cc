// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "common/debug.h"
#include "common/ceph_context.h"
#include "CacheServer.h"
#include "CacheControllerSocketCommon.h"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_rbd_cache
#undef dout_prefix
#define dout_prefix *_dout << "rbd::cache::CacheControllerSocket: " << this << " " \
                           << __func__ << ": "


using boost::asio::local::stream_protocol;

namespace ceph {
namespace cache {

Session::Session(uint64_t session_id, boost::asio::io_service& io_service, ProcessMsg processmsg, CephContext* cct)
    : m_session_id(session_id), m_dm_socket(io_service), process_msg(processmsg), cct(cct)
    {}

Session::~Session(){}

stream_protocol::socket& Session::socket() {
  return m_dm_socket;
}

void Session::start() {
  if(true) {
    serial_handing_request();
  } else {
    parallel_handing_request();
  }
}
// flow:
//
// recv request --> process request --> reply ack
//   |                                      |
//   --------------<-------------------------
void Session::serial_handing_request() {
  boost::asio::async_read(m_dm_socket, boost::asio::buffer(m_buffer, RBDSC_MSG_LEN),
                          boost::asio::transfer_exactly(RBDSC_MSG_LEN),
                          boost::bind(&Session::handle_read,
                                      shared_from_this(),
                                      boost::asio::placeholders::error,
                                      boost::asio::placeholders::bytes_transferred));
}

// flow :
//
//              --> thread 1: process request
// recv request --> thread 2: process request --> reply ack
//              --> thread n: process request
//
void Session::parallel_handing_request() {
  // TODO
}

void Session::handle_read(const boost::system::error_code& error, size_t bytes_transferred) {
  // when recv eof, the most proble is that client side close socket.
  // so, server side need to end handing_request
  if(error == boost::asio::error::eof) {
    ldout(cct, 20)<<"session: async_read : " << error.message() << dendl;
    return;
  }

  if(error) {
    ldout(cct, 20)<<"session: async_read fails: " << error.message() << dendl;
    assert(0);
  }

  if(bytes_transferred != RBDSC_MSG_LEN) {
    ldout(cct, 20)<<"session : request in-complete. "<<dendl;
    assert(0);
  }

  // TODO async_process can increse coding readable.
  // process_msg_callback call handle async_send
  process_msg(m_session_id, std::string(m_buffer, bytes_transferred));
}

void Session::handle_write(const boost::system::error_code& error, size_t bytes_transferred) {
  if (error) {
    ldout(cct, 20)<<"session: async_write fails: " << error.message() << dendl;
    assert(0);
  }

  if(bytes_transferred != RBDSC_MSG_LEN) {
    ldout(cct, 20)<<"session : reply in-complete. "<<dendl;
    assert(0);
  }

  boost::asio::async_read(m_dm_socket, boost::asio::buffer(m_buffer),
                          boost::asio::transfer_exactly(RBDSC_MSG_LEN),
                          boost::bind(&Session::handle_read,
                          shared_from_this(),
                          boost::asio::placeholders::error,
                          boost::asio::placeholders::bytes_transferred));

}

void Session::send(std::string msg) {
    boost::asio::async_write(m_dm_socket,
        boost::asio::buffer(msg.c_str(), msg.size()),
        boost::asio::transfer_exactly(RBDSC_MSG_LEN),
        boost::bind(&Session::handle_write,
                    shared_from_this(),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));

}


CacheServer::CacheServer(const std::string& file, ProcessMsg processmsg, CephContext* cct)
  : cct(cct), m_server_process_msg(processmsg),
    m_local_path(file), m_acceptor(m_io_service) {}

CacheServer::~CacheServer(){}

void CacheServer::run() {
  bool ret;
  ret = start_accept();
  if(!ret) {
    return;
  }
  m_io_service.run();
}

// TODO : use callback to replace this function.
void CacheServer::send(uint64_t session_id, std::string msg) {
  auto it = m_session_map.find(session_id);
  if (it != m_session_map.end()) {
    it->second->send(msg);
  } else {
    // TODO : why don't find existing session id ?
    ldout(cct, 20)<<"don't find session id..."<<dendl;
    assert(0);
  }
}

// when creating one acceptor, can control every step in this way.
bool CacheServer::start_accept() {
  boost::system::error_code ec;
  m_acceptor.open(m_local_path.protocol(), ec);
  if(ec) {
    ldout(cct, 20) << "m_acceptor open fails: " << ec.message() << dendl;
    return false;
  }

  // TODO control acceptor attribute.

  m_acceptor.bind(m_local_path, ec);
  if(ec) {
    ldout(cct, 20) << "m_acceptor bind fails: " << ec.message() << dendl;
    return false;
  }

  m_acceptor.listen(boost::asio::socket_base::max_connections, ec);
  if(ec) {
    ldout(cct, 20) << "m_acceptor listen fails: " << ec.message() << dendl;
    return false;
  }

  accept();
  return true;
}

void CacheServer::accept() {
  SessionPtr new_session(new Session(m_session_id, m_io_service, m_server_process_msg, cct));
  m_acceptor.async_accept(new_session->socket(),
      boost::bind(&CacheServer::handle_accept, this, new_session,
        boost::asio::placeholders::error));
}

void CacheServer::handle_accept(SessionPtr new_session, const boost::system::error_code& error) {

  if(error) {
    lderr(cct) << "async accept fails : " << error.message() << dendl;
    assert(0); // TODO
  }

  m_session_map.emplace(m_session_id, new_session);
  // TODO : session setting
  new_session->start();
  m_session_id++;

  // lanuch next accept
  accept();
}

} // namespace cache
} // namespace ceph

