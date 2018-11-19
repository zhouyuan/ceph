// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include <iostream>
#include <unistd.h>

#include "gtest/gtest.h"
#include "include/Context.h"
#include "common/Mutex.h"
#include "common/Cond.h"
#include "global/global_init.h"
#include "global/global_context.h"

#include "tools/ceph_immutable_object_cache/CacheClient.h"
#include "tools/ceph_immutable_object_cache/CacheServer.h"

using namespace ceph::immutable_obj_cache;

// sequentialize async_operation
class WaitEvent {
public:
  WaitEvent() : m_signaled(false) {
    pthread_mutex_init(&m_lock, NULL);
    pthread_cond_init(&m_cond, NULL);
  }

  ~WaitEvent() {
    pthread_mutex_destroy(&m_lock);
    pthread_cond_destroy(&m_cond);
  }

  void wait() {
    pthread_mutex_lock(&m_lock);
    while (!m_signaled) {
      pthread_cond_wait(&m_cond, &m_lock);
    }
    m_signaled = false;
    pthread_mutex_unlock(&m_lock);
   }

  void signal() {
    pthread_mutex_lock(&m_lock);
    m_signaled = true;
    pthread_cond_signal(&m_cond);
    pthread_mutex_unlock(&m_lock);
  }
private:
    pthread_mutex_t m_lock;
    pthread_cond_t m_cond;
    bool m_signaled;
};


class TestMultiSession : public ::testing::Test {
public:
  std::string m_local_path;
  CacheServer* m_cache_server;
  std::thread* m_cache_server_thread;
  std::vector<CacheClient*> m_cache_client_vec;
  int m_cache_client_index;
  WaitEvent m_wait_event;
  std::atomic<uint64_t> m_send_request_index;
  std::atomic<uint64_t> m_recv_ack_index;
  uint64_t m_session_num = 110;
  
  
  TestMultiSession() : m_local_path("/tmp/ceph_test_multisession_socket"),
                       m_cache_server_thread(nullptr), m_cache_client_vec(m_session_num, nullptr),
                       m_cache_client_index(0), m_send_request_index(0), m_recv_ack_index(0) {
  }

  ~TestMultiSession() {}

  static void SetUpTestCase() {}
  static void TearDownTestCase() {}

  void SetUp() override {
    std::remove(m_local_path.c_str());
    m_cache_server = new CacheServer(g_ceph_context, m_local_path, [this](uint64_t session_id, std::string request ){
        server_handle_request(session_id, request);
    });
    ASSERT_TRUE(m_cache_server != nullptr);
   
    m_cache_server_thread = new std::thread(([this]() {
        m_wait_event.signal();
        m_cache_server->run();
    })); 

    // waiting for thread running.
    m_wait_event.wait();  

    // waiting for io_service run.
    usleep(2);
  }
  
  void TearDown() override {
    m_cache_server->stop();
    m_cache_server_thread->join();
    delete m_cache_server;
    for(uint64_t i = 0; i < m_session_num; i++) {
      if(m_cache_client_vec[i] != nullptr) {
        delete m_cache_client_vec[i];
      }
    }

    std::remove(m_local_path.c_str());
  }

  CacheClient* create_session() {
     CacheClient* cache_client = new CacheClient(m_local_path, g_ceph_context); 
     cache_client->run();
     while (true) {
       if (0 == cache_client->connect()) {
         break;
       }
     }
    m_cache_client_vec[m_cache_client_index++] = cache_client;
    return cache_client;  
  }   

  void server_handle_request(uint64_t session_id, std::string request) {
    rbdsc_req_type_t *io_ctx = (rbdsc_req_type_t*)(request.c_str());

    switch (io_ctx->type) {
      case RBDSC_REGISTER: {

        io_ctx->type = RBDSC_REGISTER_REPLY;
        m_cache_server->send(session_id, std::string((char*)io_ctx, request.size()));
        break;
      }
      case RBDSC_READ: {
        io_ctx->type = RBDSC_READ_REPLY;
        m_cache_server->send(session_id, std::string((char*)io_ctx, request.size()));
        break;
      }
    }
  }

  void test_register_volume(std::string pool_name, std::string rbd_name) {
    auto ctx = new FunctionContext([](bool ret){
       ASSERT_TRUE(ret); 
    });
    auto session = create_session();
    session->register_volume(pool_name, rbd_name, 4096, ctx);
    ASSERT_TRUE(session->is_session_work()); 
  }

  void test_lookup_object(std::string pool, std::string rbd, uint64_t index, uint64_t request_num, bool is_last) {

    for (uint64_t i = 0; i < request_num; i++) {  
      auto ctx = new FunctionContext([this](bool ret) {
        m_recv_ack_index++;
      });    
      m_send_request_index++;      
      m_cache_client_vec[index]->lookup_object(pool, rbd, "1234", ctx);
    }

    if(is_last) {
      while(m_send_request_index != m_recv_ack_index) {
        usleep(1);
      }
      m_wait_event.signal();
    }
  }
  
};


// test : multi-session + register_request + lookup_request
TEST_F(TestMultiSession, test_case_1) {

  uint64_t test_times = 1000;
  uint64_t test_session_num = 100;

  for(uint64_t i = 0; i <= test_times; i++) {
    uint64_t random_index = random() % test_session_num;
    if (m_cache_client_vec[random_index] == nullptr) {
      test_register_volume(string("test_pool") + std::to_string(random_index), string("test_rbd"));
    } else {
      test_lookup_object(string("test_pool") + std::to_string(random_index), string("test_rbd"), random_index, 32, i == test_times ? true : false);
    }
  }

  // make sure all ack will be received.
  m_wait_event.wait();

  ASSERT_TRUE(m_send_request_index == m_recv_ack_index);
}

