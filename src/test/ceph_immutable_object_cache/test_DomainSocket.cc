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

class TestCommunication :public ::testing::Test {
public:
  CacheServer* m_cache_server;
  CacheClient* m_cache_client;
  std::thread* m_cache_server_thread;
  std::thread* m_cache_client_thread;
  std::string m_local_path;
  pthread_mutex_t m_mutex;
  pthread_cond_t m_cond;
  bool m_cache_client_ready;
  bool m_cache_server_ready;
  std::atomic<uint64_t> m_send_request_index;
  std::atomic<uint64_t> m_recv_ack_index;
  WaitEvent m_wait_event;
  unordered_set<uint64_t> m_hit_entry_set;

  TestCommunication() 
    : m_cache_server(nullptr), m_cache_client(nullptr), m_local_path("/tmp/ceph_test_domain_socket"), 
      m_cache_server_thread(nullptr), m_cache_client_thread(nullptr), m_cache_server_ready(false),
      m_send_request_index(0), m_recv_ack_index(0), m_cache_client_ready(false)
    {}

  ~TestCommunication() {}

  static void SetUpTestCase() {}
  static void TearDownTestCase() {}

  void SetUp() override {
    std::remove(m_local_path.c_str());
    m_cache_server = new CacheServer(g_ceph_context, m_local_path, [this](uint64_t xx, std::string yy ){
        handle_request(xx, yy);
    });
    ASSERT_TRUE(m_cache_server != nullptr);
    m_cache_client = new CacheClient(m_local_path, g_ceph_context);
    ASSERT_TRUE(m_cache_client != nullptr);
  }

  void TearDown() override {
    std::cout << "tear down ...." << std::endl;
    stop();
    if(m_cache_server != nullptr) {
      delete m_cache_server;
    }
    if(m_cache_client != nullptr) {
      delete m_cache_client;
    }
  }

  int create_communication_session() {

    m_cache_server_thread = new std::thread(
      [this] () {

        m_cache_server->start_accept();
        m_wait_event.signal();
        m_cache_server->just_run();
       
    });
    m_wait_event.wait();

    // TODO: just for io_service.run() preparation time.
    sleep(1);

    m_cache_client_thread = new std::thread(
      [this]() {
        client_thread_entry();
        m_wait_event.signal();
    });
    m_wait_event.wait();

    return 1;
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

   // ASSERT_TRUE(io_ctx->type == RBDSC_REGISTER || io_ctx->type == RBDSC_READ);

    switch (io_ctx->type) {
      case RBDSC_REGISTER: {
        io_ctx->type = RBDSC_REGISTER_REPLY;
        m_cache_server->send(session_id, std::string((char*)io_ctx, msg.size()));
        break;
      }
      case RBDSC_READ: {
         
        if (ret < 0) {
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

   void server_thread_entry() {
     m_cache_server->run();     
   }
  
   // times: message number
   // queue_deqth : imitate message queue depth
   // thinking : imitate handing message time
   void startup_pingpong_testing(uint64_t times, uint64_t queue_depth, int thinking) {
     m_send_request_index.store(0); 
     m_recv_ack_index.store(0);
     for(uint64_t index = 0; index < times; index++) {
       auto ctx = new FunctionContext([this, thinking, times](bool req){
          if(thinking != 0) {
            usleep(thinking); // handling message 
          }
          m_recv_ack_index++;
          if(m_recv_ack_index == times) {
            m_wait_event.signal();
          }
       });

       // simple queue depth
       while(m_send_request_index - m_recv_ack_index > queue_depth) {
         usleep(1);
       }
    
       m_cache_client->lookup_object("test_pool", "test_rbd", "123456", ctx);
       m_send_request_index++;
     }
     m_wait_event.wait();
   }

  // sequentialize read and write operation.
  void startup_message_testing(std::string pool_name, std::string volume_name, std::string object_id) {
    auto ctx = new FunctionContext([this, pool_name, volume_name, object_id](bool req){
       m_wait_event.signal(); 
    });

    m_cache_client->lookup_object(pool_name, volume_name, object_id, ctx);
    m_wait_event.wait();
  }

  void set_hit_entry_in_fake_lru(uint64_t oid) {
    if(m_hit_entry_set.find(oid) == m_hit_entry_set.end()) {
      m_hit_entry_set.insert(oid);
    } 
  }

  bool lookup_entry_in_fake_lru(uint64_t oid) {
    return m_hit_entry_set.find(oid) != m_hit_entry_set.end();
  }

};

TEST_F(TestCommunication, test_queue_depth) {
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


// sequentialize async_operation
TEST_F(TestCommunication, test_message_content) {
  create_communication_session();

}


