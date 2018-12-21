// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_LIBRBD_CACHE_SHARED_PERSISTENT_OBJECT_CACHER
#define CEPH_LIBRBD_CACHE_SHARED_PERSISTENT_OBJECT_CACHER

#include "include/buffer_fwd.h"
#include "include/int_types.h"
#include "tools/immutable_object_cache/ObjectCacheFile.h"
#include "common/Mutex.h"
#include <vector>
#include <unordered_map>

struct Context;

using namespace ceph::immutable_obj_cache;

namespace librbd {

struct ImageCtx;

namespace cache {

template <typename ImageCtxT>
class SharedPersistentObjectCacher {
public:

  SharedPersistentObjectCacher(ImageCtxT *image_ctx, std::string cache_path);
  ~SharedPersistentObjectCacher();

  int read_object(std::string oid, ceph::bufferlist* read_data,
		  uint64_t offset, uint64_t length, Context *on_finish);

private:
  ImageCtxT *m_image_ctx;
  std::string m_cache_path;
  Mutex m_file_map_lock;
  std::unordered_map<std::string, ObjectCacheFile*> file_map;

};

} // namespace cache
} // namespace librbd

extern template class librbd::cache::SharedPersistentObjectCacher<librbd::ImageCtx>;

#endif // CEPH_LIBRBD_CACHE_FILE_IMAGE_STORE
