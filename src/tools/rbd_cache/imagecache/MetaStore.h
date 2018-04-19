// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_LIBRBD_CACHE_FILE_META_STORE
#define CEPH_LIBRBD_CACHE_FILE_META_STORE

#include "common/WorkQueue.h"
#include "common/ceph_context.h"
#include "include/int_types.h"
#include "os/CacheStore/SyncFile.h"
#include <mutex>

namespace rbd {

namespace cache {
namespace file {

class MetaStore {
public:
  MetaStore(CephContext *cct, std::string vol_name, uint64_t block_count, ContextWQ* work_queue);

  bool check_exists();
  void init(Context *on_finish);
  void remove(Context *on_finish);
  void shut_down(Context *on_finish);
  void update(uint64_t block_id, uint32_t loc);
  void get_loc_map(uint32_t *dest);
  void load(uint32_t loc);

private:
  CephContext *m_cct;
  ContextWQ *op_work_queue;
  uint64_t m_block_count;
  uint32_t *m_loc_map;
  //mutable Mutex m_lock;
  std::mutex m_lock;
  os::CacheStore::SyncFile m_meta_file;

};

} // namespace file
} // namespace cache
} // namespace rbd


#endif // CEPH_LIBRBD_CACHE_FILE_META_STORE
