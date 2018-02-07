#ifndef ADMIN_SOCKET_CLIENT_H
#define ADMIN_SOCKET_CLIENT_H

#define BOOST_DISABLE_ASSERTS

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>


using boost::asio::local::stream_protocol;

class CacheClient {
public:
  CacheClient(boost::asio::io_service& io_service, const std::string& file)
    : io_service_(io_service),
      socket_(io_service),
      ep_(stream_protocol::endpoint(file))
  {
     std::thread th([this](){io_service_.run();});
     th.detach();
  }

  int connect() {
    try {
      socket_.connect(ep_);
    } catch (std::exception& e) {
      return -1;
    }
    return 0;
  }

  int lookup_block(uint64_t block_id, bool* result) {
    boost::asio::async_write(socket_,  boost::asio::buffer(buffer_, block_size_),
        [this, result](const boost::system::error_code& err, size_t cb) {
        if (!err) {
          assert(cb == block_size_);
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
    boost::asio::async_read(socket_, boost::asio::buffer(buffer_, block_size_),
        [this, result](const boost::system::error_code& err, size_t cb) {
        if (!err) {
            assert(cb == block_size_);
            *result = true;
            cv.notify_one();
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
  stream_protocol::socket socket_;
  stream_protocol::endpoint ep_;
  char* buffer_ = (char*)malloc(1024);
  int block_size_ = 1024;

  std::condition_variable cv;
  std::mutex m;
};

#endif
