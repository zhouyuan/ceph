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
#include "ObjectCacheStore.h"

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


class CacheController {
 public:
  CacheController(CephContext *cct, const std::vector<const char*> &args);
  ~CacheController();

  int init();

  int shutdown();

  void handle_signal(int sinnum);

  void run();

  void handle_request(uint64_t sesstion_id, std::string msg);

 private:
  boost::asio::io_service io_service;
  CacheServer *m_cache_server;
  std::vector<const char*> m_args;
  CephContext *m_cct;
  ObjectCacheStore *m_object_cache_store;
  ContextWQ* pcache_op_work_queue;
};

#endif
