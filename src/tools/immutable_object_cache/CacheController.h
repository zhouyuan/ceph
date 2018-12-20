// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_CACHE_CACHE_CONTROLLER_H
#define CEPH_CACHE_CACHE_CONTROLLER_H

#include "common/ceph_context.h"
#include "common/WorkQueue.h"
#include "CacheServer.h"
#include "ObjectCacheStore.h"

namespace ceph {
namespace immutable_obj_cache {

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
  CacheServer *m_cache_server;
  std::vector<const char*> m_args;
  CephContext *m_cct;
  ObjectCacheStore *m_object_cache_store;
  ContextWQ* pcache_op_work_queue;
};

} // namespace immutable_obj_cache
} // namespace ceph

#endif
