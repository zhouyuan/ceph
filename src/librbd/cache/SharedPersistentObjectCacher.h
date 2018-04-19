// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_LIBRBD_CACHE_SHARED_PERSISTENT_OBJECT_CACHER
#define CEPH_LIBRBD_CACHE_SHARED_PERSISTENT_OBJECT_CACHER

#include "include/buffer_fwd.h"
#include "include/int_types.h"
#include "os/CacheStore/SyncFile.h"
#include <vector>
#include <map>

struct Context;

namespace librbd {

struct ImageCtx;

namespace cache {

template <typename ImageCtxT>
class SharedPersistentObjectCacher {
public:

  SharedPersistentObjectCacher(ImageCtxT *image_ctx, std::string volumd_name);

  void read_object(std::string oid, uint64_t offset, uint64_t length, Context *on_finish);

private:
  ImageCtxT *m_image_ctx;
  std::map<std::string,  os::CacheStore::SyncFile*> file_map;
  //os::CacheStore::SyncFile* m_cache_file;
  std::string m_cache_path;

  std::string m_volume_name;

};

} // namespace cache
} // namespace librbd

extern template class librbd::cache::SharedPersistentObjectCacher<librbd::ImageCtx>;

#endif // CEPH_LIBRBD_CACHE_FILE_IMAGE_STORE
