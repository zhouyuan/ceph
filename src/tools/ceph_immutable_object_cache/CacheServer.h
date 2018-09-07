// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CACHE_CONTROLLER_SOCKET_H
#define CACHE_CONTROLLER_SOCKET_H

#include <cstdio>
#include <iostream>
#include <array>
#include <memory>
#include <string>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/asio/error.hpp>
#include <boost/algorithm/string.hpp>

#include "include/assert.h"
#include "CacheControllerSocketCommon.h"


using boost::asio::local::stream_protocol;

namespace ceph {
namespace cache {

class Session : public std::enable_shared_from_this<Session> {
public:
  Session(uint64_t session_id, boost::asio::io_service& io_service, ProcessMsg processmsg, CephContext* cct);
  ~Session();

  stream_protocol::socket& socket();
  void start();
  void serial_handing_request();
  void parallel_handing_request();

private:

  void handle_read(const boost::system::error_code& error, size_t bytes_transferred); 

  void handle_write(const boost::system::error_code& error, size_t bytes_transferred);

public:
  void send(std::string msg);

private:
  uint64_t m_session_id;
  stream_protocol::socket m_dm_socket;
  ProcessMsg process_msg;
  CephContext* cct;

  // Buffer used to store data received from the client.
  //std::array<char, 1024> data_;
  char m_buffer[1024];
};

typedef std::shared_ptr<Session> SessionPtr;

class CacheServer {

 public:
  CacheServer(const std::string& file, ProcessMsg processmsg, CephContext* cct);
  ~CacheServer();

  void run();
  void send(uint64_t session_id, std::string msg);

 private:
  bool start_accept();
  void accept();
  void handle_accept(SessionPtr new_session, const boost::system::error_code& error);

 private:
  CephContext* cct;
  boost::asio::io_service m_io_service; // TODO wrapper it.
  ProcessMsg m_server_process_msg;
  stream_protocol::endpoint m_local_path;
  stream_protocol::acceptor m_acceptor;
  uint64_t m_session_id = 1;
  std::map<uint64_t, SessionPtr> m_session_map;
};

} // namespace cache
} // namespace ceph

#endif
