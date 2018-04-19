// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_LIBRBD_CACHE_FILE_IMAGE_STORE
#define CEPH_LIBRBD_CACHE_FILE_IMAGE_STORE

#include "common/WorkQueue.h"
#include "common/ceph_context.h"
#include "include/buffer_fwd.h"
#include "include/int_types.h"
#include "os/CacheStore/SyncFile.h"
#include <vector>

//struct Context;

namespace rbd {


namespace cache {
namespace file {

class ImageStore {
public:
  typedef std::vector<std::pair<uint32_t, uint32_t> > BlockExtents;

  ImageStore(CephContext *cct, std::string volume_name, uint64_t image_size, ContextWQ* work_queue);

  void init(Context *on_finish);
  void remove(Context *on_finish);
  void shut_down(Context *on_finish);
  void reset(Context *on_finish);

  void read_block(uint64_t offset, BlockExtents &&block_extents,
                  ceph::bufferlist *bl, Context *on_finish);
  void write_block(uint64_t offset, BlockExtents &&block_extents,
                   ceph::bufferlist &&bl, Context *on_finish);
  void discard_block(uint64_t cache_block, Context *on_finish);
  bool check_exists();

private:
  CephContext *m_cct;
  ContextWQ *op_work_queue;
  uint64_t m_image_size;
  os::CacheStore::SyncFile m_cache_file;

};

} // namespace file
} // namespace cache
} // namespace rbd

#endif // CEPH_LIBRBD_CACHE_FILE_IMAGE_STORE
