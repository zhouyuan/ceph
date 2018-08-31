// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CACHE_CONTROLLER_SOCKET_CLIENT_H
#define CACHE_CONTROLLER_SOCKET_CLIENT_H

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/asio/error.hpp>
#include <boost/algorithm/string.hpp>
#include "librbd/ImageCtx.h"
#include "include/assert.h"
#include "include/Context.h"
#include "CacheControllerSocketCommon.h"


using boost::asio::local::stream_protocol;

namespace rbd {
namespace cache {

class CacheClient {
public:
  CacheClient(const std::string& file, ClientProcessMsg processmsg, CephContext* ceph_ctx)
    : m_io_service_work(m_io_service),
      m_dm_socket(m_io_service),
      m_client_process_msg(processmsg),
      m_ep(stream_protocol::endpoint(file)),
      cct(ceph_ctx)
  {
     // TODO wrapper io_service
     std::thread thd([this](){  
                      m_io_service.run();});
     thd.detach();
  }

  void run(){
  } 

  int connect() {
    boost::system::error_code ec;
    m_dm_socket.connect(m_ep, ec);
    if(ec) {
      //ldout(cct, 20) << "connect: " << ec.message() << dendl;
      if(m_dm_socket.is_open()) {
        // Set to indicate what error occurred, if any. 
        // Note that, even if the function indicates an error, 
        // the underlying descriptor is closed.
        boost::system::error_code close_ec;
        m_dm_socket.close(close_ec);
        if(close_ec) {
          //ldout(cct, 20) << "close: " << close_ec.message() << dendl;
        }
      }
      return -1; 
    }
    
    std::cout<<"connect success"<<std::endl;

    connected = true;
    return 0;
  }

  int register_volume(std::string pool_name, std::string vol_name, uint64_t vol_size) {
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
      //ldcout << "write fails : " << ec.message() << dendl;
      assert(0);
    }

    if(ret != message->size()) {
      //ldcout << "write fails : ret != send_bytes "<< dendl;
      assert(0);
    }    

    // hard code TODO
    char* receive_buffer = new char[544 + 1];
    ret = boost::asio::read(m_dm_socket, boost::asio::buffer(receive_buffer, 544), ec);
    if(ec == boost::asio::error::eof) {
      std::cout<< "recv eof"<<std::endl;
      return -1;
    }
    if(ec) {
      //ldcout << "write fails : " << ec.message() << dendl;
      assert(0);
    }

    if(ret != 544) {
      //ldcout << "write fails : ret != receive bytes " << dendl;
      assert(0);
    }

    m_client_process_msg(std::string(receive_buffer, ret));
    
    delete[] receive_buffer;
    // delete message;
    
    std::cout << "register volume success" << std::endl;

    return 0;
  }

  // if occur any error, we just return false. Then read from rados.
  int lookup_object(std::string pool_name, std::string vol_name, std::string object_id, Context* on_finish) {
    rbdsc_req_type_t *message = new rbdsc_req_type_t();
    message->type = RBDSC_READ;
    memcpy(message->pool_name, pool_name.c_str(), pool_name.size());
    memcpy(message->vol_name, object_id.c_str(), object_id.size());
    message->vol_size = 0;
    message->offset = 0;
    message->length = 0;

    boost::asio::async_write(m_dm_socket,  
                             boost::asio::buffer((char*)message, message->size()),
                             boost::asio::transfer_exactly(544),
        [this, on_finish, message](const boost::system::error_code& err, size_t cb) {
          delete message;
          if(err) {
            std::cout<< "lookup_object: async_write fails." << err.message() << std::endl;
            on_finish->complete(false);
            return;
          }
          if(cb != 544) {
            std::cout<< "lookup_object: async_write fails. in-complete request" <<std::endl;
            on_finish->complete(false);
            return;
          }
          get_result(on_finish);
    });

    return 0;
  }

  void get_result(Context* on_finish) {
    boost::asio::async_read(m_dm_socket, boost::asio::buffer(m_recv_buffer, 544),
                            boost::asio::transfer_exactly(544),
        [this, on_finish](const boost::system::error_code& err, size_t cb) {
          if(err == boost::asio::error::eof) {
            std::cout<<"get_result: ack is EOF." << std::endl;
            on_finish->complete(false);
            return;
          }
          if(err) {
            std::cout<< "get_result: async_read fails:" << err.message() << std::endl;
            on_finish->complete(false); // TODO replace this assert with some metohds.
            return;
          }
          if (cb != 544) {
            std::cout << "get_result: in-complete ack." << std::endl;
	    on_finish->complete(false); // TODO: replace this assert with some methods.
          }

	  rbdsc_req_type_t *io_ctx = (rbdsc_req_type_t*)(m_recv_buffer);
          
          // TODO: re-occur yuan's bug
          if(io_ctx->type == RBDSC_READ) {
            std::cout << "get rbdsc_read... " << std::endl;
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

  void handle_connect(const boost::system::error_code& error) {
    //TODO(): open librbd snap
  }

  void handle_write(const boost::system::error_code& error) {
  }

private:
  boost::asio::io_service m_io_service;
  boost::asio::io_service::work m_io_service_work;
  stream_protocol::socket m_dm_socket;
  ClientProcessMsg m_client_process_msg;
  stream_protocol::endpoint m_ep;
  char m_recv_buffer[1024];
  int block_size_ = 1024;

  std::condition_variable cv;
  std::mutex m;
  
  CephContext* cct;
public:
  bool connected = false;
};

} // namespace cache
} // namespace rbd
#endif
