// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "CacheClient.h"
#include "CacheControllerSocketCommon.h"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_rbd_cache
#undef dout_prefix
#define dout_prefix *_dout << "rbd::cache::CacheControllerSocketClient: " << this << " " \
                           << __func__ << ": "


using boost::asio::local::stream_protocol;

namespace ceph {
namespace cache {

 CacheClient::CacheClient(const std::string& file, ClientProcessMsg processmsg, CephContext* ceph_ctx)
    : m_io_service_work(m_io_service),
      m_dm_socket(m_io_service),
      m_client_process_msg(processmsg),
      m_ep(stream_protocol::endpoint(file)),
      m_session_work(false),
      cct(ceph_ctx)
  {
     // TODO wrapper io_service
     std::thread thd([this](){
                      m_io_service.run();});
     thd.detach();
  }

  void CacheClient::run(){
  }

  bool CacheClient::is_session_work() {
    return m_session_work.load() == true;
  }

  // just when error occur, call this method.
  void CacheClient::close() {
    m_session_work.store(false);
    boost::system::error_code close_ec;
    m_dm_socket.close(close_ec);
    if(close_ec) {
       ldout(cct, 20) << "close: " << close_ec.message() << dendl;
    }
    ldout(cct, 20) << "session don't work, later all request will be dispatched to rados layer" << dendl;
  }

  int CacheClient::connect() {
    boost::system::error_code ec;
    m_dm_socket.connect(m_ep, ec);
    if(ec) {
      if(ec == boost::asio::error::connection_refused) {
        ldout(cct, 20) << ec.message() << " : maybe rbd-cache Controller don't startup. "
                  << "Now data will be read from ceph cluster " << dendl;
      } else {
        ldout(cct, 20) << "connect: " << ec.message() << dendl;
      }

      if(m_dm_socket.is_open()) {
        // Set to indicate what error occurred, if any.
        // Note that, even if the function indicates an error,
        // the underlying descriptor is closed.
        boost::system::error_code close_ec;
        m_dm_socket.close(close_ec);
        if(close_ec) {
          ldout(cct, 20) << "close: " << close_ec.message() << dendl;
        }
      }
      return -1;
    }

    ldout(cct, 20)<<"connect success"<<dendl;

    return 0;
  }

  int CacheClient::register_volume(std::string pool_name, std::string vol_name, uint64_t vol_size) {
    // cache controller will init layout
    rbdsc_req_type_t *message = new rbdsc_req_type_t();
    message->type = RBDSC_REGISTER;
    memcpy(message->pool_name, pool_name.c_str(), pool_name.size());
    memcpy(message->vol_name, vol_name.c_str(), vol_name.size());
    message->vol_size = vol_size;
    message->offset = 0;
    message->length = 0;

    uint64_t ret;
    boost::system::error_code ec;

    ret = boost::asio::write(m_dm_socket, boost::asio::buffer((char*)message, message->size()), ec);
    if(ec) {
      ldout(cct, 20) << "write fails : " << ec.message() << dendl;
      return -1;
    }

    if(ret != message->size()) {
      ldout(cct, 20) << "write fails : ret != send_bytes "<< dendl;
      return -1;
    }

    // hard code TODO
    ret = boost::asio::read(m_dm_socket, boost::asio::buffer(m_recv_buffer, RBDSC_MSG_LEN), ec);
    if(ec == boost::asio::error::eof) {
      ldout(cct, 20)<< "recv eof"<<dendl;
      return -1;
    }

    if(ec) {
      ldout(cct, 20) << "write fails : " << ec.message() << dendl;
      return -1;
    }

    if(ret != RBDSC_MSG_LEN) {
      ldout(cct, 20) << "write fails : ret != receive bytes " << dendl;
      return -1;
    }

    m_client_process_msg(std::string(m_recv_buffer, ret));

    delete message;

    ldout(cct, 20) << "register volume success" << dendl;

    // TODO
    m_session_work.store(true);

    return 0;
  }

  // if occur any error, we just return false. Then read from rados.
  int CacheClient::lookup_object(std::string pool_name, std::string vol_name, std::string object_id, Context* on_finish) {
    rbdsc_req_type_t *message = new rbdsc_req_type_t();
    message->type = RBDSC_READ;
    memcpy(message->pool_name, pool_name.c_str(), pool_name.size());
    memcpy(message->vol_name, object_id.c_str(), object_id.size());
    message->vol_size = 0;
    message->offset = 0;
    message->length = 0;

    boost::asio::async_write(m_dm_socket,
                             boost::asio::buffer((char*)message, message->size()),
                             boost::asio::transfer_exactly(RBDSC_MSG_LEN),
        [this, on_finish, message](const boost::system::error_code& err, size_t cb) {
          delete message;
          if(err) {
            ldout(cct, 20)<< "lookup_object: async_write fails." << err.message() << dendl;
            close();
            on_finish->complete(false);
            return;
          }
          if(cb != RBDSC_MSG_LEN) {
            ldout(cct, 20)<< "lookup_object: async_write fails. in-complete request" <<dendl;
            close();
            on_finish->complete(false);
            return;
          }
          get_result(on_finish);
    });

    return 0;
  }

  void CacheClient::get_result(Context* on_finish) {
    boost::asio::async_read(m_dm_socket, boost::asio::buffer(m_recv_buffer, RBDSC_MSG_LEN),
                            boost::asio::transfer_exactly(RBDSC_MSG_LEN),
        [this, on_finish](const boost::system::error_code& err, size_t cb) {
          if(err == boost::asio::error::eof) {
            ldout(cct, 20)<<"get_result: ack is EOF." << dendl;
            close();
            on_finish->complete(false);
            return;
          }
          if(err) {
            ldout(cct, 20)<< "get_result: async_read fails:" << err.message() << dendl;
            close();
            on_finish->complete(false); // TODO replace this assert with some metohds.
            return;
          }
          if (cb != RBDSC_MSG_LEN) {
            close();
            ldout(cct, 20) << "get_result: in-complete ack." << dendl;
	    on_finish->complete(false); // TODO: replace this assert with some methods.
          }

	  rbdsc_req_type_t *io_ctx = (rbdsc_req_type_t*)(m_recv_buffer);

          // TODO: re-occur yuan's bug
          if(io_ctx->type == RBDSC_READ) {
            ldout(cct, 20) << "get rbdsc_read... " << dendl;
            assert(0);
          }

          if (io_ctx->type == RBDSC_READ_REPLY) {
	    on_finish->complete(true);
            return;
          } else {
	    on_finish->complete(false);
            return;
          }
    });
  }

} // namespace cache
} // namespace ceph
