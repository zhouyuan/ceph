// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "common/debug.h"
#include "common/ceph_context.h"
#include "CacheServer.h"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_immutable_obj_cache
#undef dout_prefix
#define dout_prefix *_dout << "ceph::cache::CacheControllerSocket: " << this << " " \
                           << __func__ << ": "


using boost::asio::local::stream_protocol;

namespace ceph {
namespace immutable_obj_cache {

CacheServer::CacheServer(CephContext* cct, const std::string& file, ProcessMsg processmsg)
  : cct(cct), m_server_process_msg(processmsg),
    m_local_path(file), m_acceptor(m_io_service) {}

CacheServer::~CacheServer() {
  stop();
}

int CacheServer::run() {
  ldout(cct, 20) << dendl;

  int ret = start_accept();
  if(ret != 0) {
    return ret;
  }

  boost::system::error_code ec;
  ret = m_io_service.run(ec);
  if(ec) {
    ldout(cct, 1) << "m_io_service run fails: " << ec.message() << dendl;
    return -1;
  }
  return 0;
}

int CacheServer::stop() {
  m_io_service.stop();
  return 0;
}

void CacheServer::send(uint64_t session_id, std::string msg) {
  ldout(cct, 20) << dendl;

  auto it = m_session_map.find(session_id);
  if (it != m_session_map.end()) {
    it->second->send(msg);
  } else {
    ldout(cct, 20) << "missing reply session id" << dendl;
    assert(0);
  }
}

int CacheServer::start_accept() {
  ldout(cct, 20) << dendl;

  boost::system::error_code ec;
  m_acceptor.open(m_local_path.protocol(), ec);
  if(ec) {
    ldout(cct, 1) << "m_acceptor open fails: " << ec.message() << dendl;
    return -1;
  }

  m_acceptor.bind(m_local_path, ec);
  if(ec) {
    ldout(cct, 1) << "m_acceptor bind fails: " << ec.message() << dendl;
    return -1;
  }

  m_acceptor.listen(boost::asio::socket_base::max_connections, ec);
  if(ec) {
    ldout(cct, 1) << "m_acceptor listen fails: " << ec.message() << dendl;
    return -1;
  }

  accept();
  return 0;
}

void CacheServer::accept() {

  CacheSessionPtr new_session(new CacheSession(m_session_id, m_io_service, m_server_process_msg, cct));
  m_acceptor.async_accept(new_session->socket(),
      boost::bind(&CacheServer::handle_accept, this, new_session,
        boost::asio::placeholders::error));
}

void CacheServer::handle_accept(CacheSessionPtr new_session, const boost::system::error_code& error) {
  ldout(cct, 20) << dendl;
  if (error) {
    // operation_absort
    lderr(cct) << "async accept fails : " << error.message() << dendl;
    return;
  }

  m_session_map.emplace(m_session_id, new_session);
  // TODO : session setting
  new_session->start();
  m_session_id++;

  // lanuch next accept
  accept();
}

} // namespace immutable_obj_cache
} // namespace ceph
