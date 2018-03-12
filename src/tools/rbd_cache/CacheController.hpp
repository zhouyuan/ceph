#ifndef CACHE_CONTROLLER_H
#define CACHE_CONTROLLER_H

#include <thread>
#include "common/Formatter.h"
#include "common/admin_socket.h"
#include "common/debug.h"
#include "common/errno.h"
#include "common/ceph_context.h"
#include "common/Mutex.h"
#include "common/WorkQueue.h"
#include "include/rados/librados.hpp"
#include "include/rbd/librbd.h"
#include "librbd/ImageCtx.h"
#include "librbd/ImageState.h"

#include "AdminSocket.hpp"
#include "FileImageCacheStore.hpp"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_rbd_cache
#undef dout_prefix
#define dout_prefix *_dout << "rbd::cache::CacheController: " << this << " " \
                           << __func__ << ": "


using boost::asio::local::stream_protocol;
using librados::Rados;
using librados::IoCtx;

typedef shared_ptr<librados::Rados> RadosRef;
typedef shared_ptr<librados::IoCtx> IoCtxRef;


class ThreadPoolSingleton : public ThreadPool {
public:
  ContextWQ *op_work_queue;

  explicit ThreadPoolSingleton(CephContext *cct)
    : ThreadPool(cct, "librbd::cache::thread_pool", "tp_librbd_cache", 32,
                 "pcache_threads"),
      op_work_queue(new ContextWQ("librbd::pcache_op_work_queue",
                    cct->_conf->get_val<int64_t>("rbd_op_thread_timeout"),
                    this)) {
    start();
  }
  ~ThreadPoolSingleton() override {
    op_work_queue->drain();
    delete op_work_queue;

    stop();
  }
};


class CacheController {
 public:
  CacheController(CephContext *cct, const std::vector<const char*> &args):
  m_args(args),
  m_cct(cct){}
  //m_lock("rbd::cache::CacheController"),
  ~CacheController(){}

  int init() {
    m_cct->lookup_or_create_singleton_object<ThreadPoolSingleton>(
      thread_pool_singleton, "rbd::cache::thread_pool");
    pcache_op_work_queue = thread_pool_singleton->op_work_queue;

    m_image_cache_store = new FileImageCacheStore(m_cct, pcache_op_work_queue);
    int r = m_image_cache_store->init();
    if (r < 0) {
      //ldout(m_cct, 10) << "init error\n" << dendl;
    }
    //r = m_image_cache_store->init_cache("10176b8b4567", 104857600);
    return r;
  }

  int shutdown() {
    int r = m_image_cache_store->shutdown();
    return r;
  }

  void handle_signal(int signum){}

  void run() {
    try {
      std::string controller_path = "/tmp/rbd_shared_readonly_cache_demo";
      std::remove(controller_path.c_str()); 
      
      m_cache_server = new CacheServer(io_service, controller_path,
        ([&](uint64_t p, std::string s){handle_request(p, s);}));
      io_service.run();
    } catch (std::exception& e) {
      std::cerr << "Exception: " << e.what() << "\n";
    }
  }

  void handle_request(uint64_t sesstion_id, std::string msg){
    rbdsc_req_type_t *io_ctx = (rbdsc_req_type_t*)(msg.c_str());
    
    std::cout << "AAA " << io_ctx->vol_name << std::endl;
    std::cout << "AAA " << io_ctx->type << std::endl;
    switch (io_ctx->type) {
      case RBDSC_REGISTER: {
        // init cache layout for volume        

        m_image_cache_store->init_cache(io_ctx->vol_name, io_ctx->vol_size);
        io_ctx->type = RBDSC_REGISTER_REPLY;
        m_cache_server->send(sesstion_id, std::string((char*)io_ctx, msg.size()));

        break;
      }
      case RBDSC_READ: {

        m_image_cache_store->lock_cache(io_ctx->vol_name);
        io_ctx->type = RBDSC_READ_REPLY;
        m_cache_server->send(sesstion_id, std::string((char*)io_ctx, msg.size()));

        break;
      }
      
    }
  }

 private:
  boost::asio::io_service io_service;
  CacheServer *m_cache_server;
  std::vector<const char*> m_args;
  CephContext *m_cct;
  FileImageCacheStore *m_image_cache_store;
  //Mutex &m_lock;
  ContextWQ* pcache_op_work_queue;
  ThreadPoolSingleton *thread_pool_singleton;

};

#endif
