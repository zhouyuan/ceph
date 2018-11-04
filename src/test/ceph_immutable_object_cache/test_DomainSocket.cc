// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
#include <iostream> 
#include <unistd.h>

#include "gtest/gtest.h"
#include "include/Context.h"
#include "common/Mutex.h"
#include "common/Cond.h"
#include "global/global_init.h"
#include "common/ceph_argparse.h"
#include "global/global_context.h"
#include "tools/ceph_immutable_object_cache/CacheClient.h"
#include "tools/ceph_immutable_object_cache/CacheServer.h"

using namespace ceph::immutable_obj_cache;

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

class TestCommunication :public ::testing::Test {
public:
  CacheServer* m_cache_server;
  CacheClient* m_cache_client;
  std::thread* m_cache_server_thread;
  std::thread* m_cache_client_thread;
  std::string m_local_path;
  std::atomic<uint64_t> m_send_request_index;
  std::atomic<uint64_t> m_recv_ack_index;
  WaitEvent m_wait_event;
  unordered_set<std::string> m_hit_entry_set;

  TestCommunication() 
    : m_cache_server(nullptr), m_cache_client(nullptr), m_local_path("/tmp/ceph_test_domain_socket"), 
      m_cache_server_thread(nullptr), m_cache_client_thread(nullptr), m_send_request_index(0), m_recv_ack_index(0)
  {}

  ~TestCommunication() {}

  static void SetUpTestCase() {}
  static void TearDownTestCase() {}

  void SetUp() override {
    std::remove(m_local_path.c_str());
    m_cache_server = new CacheServer(g_ceph_context, m_local_path, [this](uint64_t session_id, std::string msg ){
      handle_request(session_id, msg);
    });
    ASSERT_TRUE(m_cache_server != nullptr);
    m_cache_client = new CacheClient(m_local_path, g_ceph_context);
    ASSERT_TRUE(m_cache_client != nullptr);
  }

  void TearDown() override {
    stop();
    if (m_cache_server != nullptr) {
      delete m_cache_server;
    }
    if (m_cache_client != nullptr) {
      delete m_cache_client;
    }
  }

  void create_communication_session() {
    m_cache_server_thread = new std::thread(
      [this] () {
        m_cache_server->start_accept();
        m_wait_event.signal();
        m_cache_server->just_run(); 
    });
    m_wait_event.wait();

    sleep(1); 

    m_cache_client_thread = new std::thread(
      [this]() {
        client_thread_entry();
        m_wait_event.signal();
    });
    m_wait_event.wait();
  }

  void stop() {
    m_cache_client->stop();
    m_cache_client_thread->join();
    m_cache_server->stop();
    m_cache_server_thread->join();
  }

  void handle_request(uint64_t session_id, std::string msg){
    int ret;
    rbdsc_req_type_t *io_ctx = (rbdsc_req_type_t*)(msg.c_str());
    
    switch (io_ctx->type) {
      case RBDSC_REGISTER: {
        io_ctx->type = RBDSC_REGISTER_REPLY;
        m_cache_server->send(session_id, std::string((char*)io_ctx, msg.size()));
        break;
      }
      case RBDSC_READ: {
        if ( m_hit_entry_set.find(io_ctx->oid) == m_hit_entry_set.end()) { 
          io_ctx->type = RBDSC_READ_RADOS;
        } else {
          io_ctx->type = RBDSC_READ_REPLY;
        }
        m_cache_server->send(session_id, std::string((char*)io_ctx, msg.size()));
        break;
      } 
    }
  }

   void client_thread_entry() {
     m_cache_client->run();
     ASSERT_TRUE(0 == m_cache_client->connect());

     auto ctx = new FunctionContext([this](bool reg) {
       ASSERT_TRUE(reg);
     });
     m_cache_client->register_volume("pool_name", "rbd_name", 4096, ctx);
     ASSERT_TRUE(m_cache_client->is_session_work());
   }

   void startup_pingpong_testing(uint64_t times, uint64_t queue_depth, int thinking) {
     m_send_request_index.store(0); 
     m_recv_ack_index.store(0);

     for (uint64_t index = 0; index < times; index++) {
       auto ctx = new FunctionContext([this, thinking, times](bool req){
         if (thinking != 0) {
           usleep(thinking);
         }
         m_recv_ack_index++;
         if (m_recv_ack_index == times) {
           m_wait_event.signal();
         }
       });

       while (m_send_request_index - m_recv_ack_index > queue_depth) {
         usleep(1);
       }

       m_cache_client->lookup_object("test_pool", "test_rbd", "123456", ctx);
       m_send_request_index++;
     }

     m_wait_event.wait();
   }

  bool startup_lookupobject_testing(std::string pool_name, std::string volume_name, std::string object_id) {
    bool hit;
    auto ctx = new FunctionContext([this, &hit](bool req){
       hit = req;
       m_wait_event.signal(); 
    });
    m_cache_client->lookup_object(pool_name, volume_name, object_id, ctx);
    m_wait_event.wait();
    return hit;
  }

  void set_hit_entry_in_fake_lru (std::string oid) {
    if (m_hit_entry_set.find(oid) == m_hit_entry_set.end()) {
      m_hit_entry_set.insert(oid);
    } 
  }

  bool lookup_entry_in_fake_lru (std::string oid) {
    return m_hit_entry_set.find(oid) != m_hit_entry_set.end();
  }
};

TEST_F(TestCommunication, test_pingpong) {
  create_communication_session();
  startup_pingpong_testing(10000, 16, 0);
  ASSERT_TRUE(m_send_request_index == m_recv_ack_index);
  startup_pingpong_testing(10000, 32, 0);
  ASSERT_TRUE(m_send_request_index == m_recv_ack_index);
  startup_pingpong_testing(10000, 64, 0);
  ASSERT_TRUE(m_send_request_index == m_recv_ack_index);
  startup_pingpong_testing(10000, 128, 0);
  ASSERT_TRUE(m_send_request_index == m_recv_ack_index);
  startup_pingpong_testing(10000, 256, 0);
  ASSERT_TRUE(m_send_request_index == m_recv_ack_index);
  startup_pingpong_testing(10000, 512, 0);
  ASSERT_TRUE(m_send_request_index == m_recv_ack_index);
}

TEST_F(TestCommunication, test_lookup_object) {
  create_communication_session();
  m_hit_entry_set.clear();    
  auto generate_oid = [](uint64_t index){
    std::ostringstream oss;
    oss << index;
    return oss.str();
  };
  for (uint64_t i = 0; i < 100; i++) {
    set_hit_entry_in_fake_lru(generate_oid(i));
    set_hit_entry_in_fake_lru(generate_oid(i + 20));
    set_hit_entry_in_fake_lru(generate_oid(i + 40));
    set_hit_entry_in_fake_lru(generate_oid(i + 60));
    set_hit_entry_in_fake_lru(generate_oid(i + 80));
  }
  for (uint64_t i = 0; i < 300; i++) {
    if (m_hit_entry_set.find(generate_oid(i)) == m_hit_entry_set.end()) {
      ASSERT_FALSE(startup_lookupobject_testing("test_pool", "testing_volume", generate_oid(i)));
    } else {
      ASSERT_TRUE(startup_lookupobject_testing("test_pool", "testing_volume", generate_oid(i)));
    }
  }
}
