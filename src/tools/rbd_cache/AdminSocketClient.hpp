#ifndef ADMIN_SOCKET_CLIENT_H
#define ADMIN_SOCKET_CLIENT_H

#define BOOST_DISABLE_ASSERTS

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
#include "AdminSocketCommon.h"


using boost::asio::local::stream_protocol;

class CacheClient {
public:
  CacheClient(boost::asio::io_service& io_service,
              const std::string& file, ClientProcessMsg processmsg)
    : io_service_(io_service),
      io_service_work_(io_service),
      socket_(io_service),
      m_client_process_msg(processmsg),
      ep_(stream_protocol::endpoint(file))
  {
     std::thread th([this](){io_service_.run(); });
     th.detach();
  }

  void run(){
     //th= new std::thread([this](){io_service_.run(); std::cout<<"thread over"<<std::endl;});
     //th->detach();
  } 

  int connect() {
    try {
      socket_.connect(ep_);
    } catch (std::exception& e) {
      return -1;
    }
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
    boost::asio::async_write(socket_,  boost::asio::buffer((char*)message, message->size()),
        [this](const boost::system::error_code& err, size_t cb) {
        if (!err) {

          boost::asio::async_read(socket_, boost::asio::buffer(buffer_),
              boost::asio::transfer_exactly(544),
              [this](const boost::system::error_code& err, size_t cb) {
              if (!err) {
                m_client_process_msg(std::string(buffer_, cb));
              } else {
                  return -1;
              }
          });



        } else {
          return -1;
        }
    });

    return 0;
  }

  int open_volume(std::string pool_name, std::string vol_name, uint64_t vol_size) {
    // cache client will open volume

    return 0;
  }

  int lookup_block(std::string pool_name, std::string vol_name, uint64_t block_id, bool* result) {
    rbdsc_req_type_t *message = new rbdsc_req_type_t();
    message->type = RBDSC_READ;
    memcpy(message->pool_name, pool_name.c_str(), pool_name.size());
    memcpy(message->vol_name, vol_name.c_str(), vol_name.size());
    message->vol_size = 0;
    message->offset = block_id;
    message->length = block_id;

    boost::asio::async_write(socket_,  boost::asio::buffer((char*)message, message->size()),
        [this, result](const boost::system::error_code& err, size_t cb) {
        if (!err) {
          get_result(result);
        } else {
          return -1;
        }
    });
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk);
    return 0;
  }

  void get_result(bool* result) {
    boost::asio::async_read(socket_, boost::asio::buffer(buffer_),
        boost::asio::transfer_exactly(544),
        [this, result](const boost::system::error_code& err, size_t cb) {
        if (!err) {
            *result = true;
            cv.notify_one();
            m_client_process_msg(std::string(buffer_, cb));
        } else {
            return -1;
        }
    });
  }


  void handle_connect(const boost::system::error_code& error) {
    //TODO(): open librbd snap
  }

  void handle_write(const boost::system::error_code& error) {
  }

private:
  boost::asio::io_service& io_service_;
  boost::asio::io_service::work io_service_work_;
  stream_protocol::socket socket_;
  stream_protocol::endpoint ep_;
  char buffer_[1024];
  int block_size_ = 1024;
  ClientProcessMsg m_client_process_msg;

  std::condition_variable cv;
  std::mutex m;

public:
  bool connected = false;
};

#endif
