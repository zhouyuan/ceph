// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "librbd/cache/SharedPersistentObjectCacher.h"
#include "include/buffer.h"
#include "common/dout.h"
#include "librbd/ImageCtx.h"

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd::cache::SharedPersistentObjectCacher: " << this \
                           << " " <<  __func__ << ": "

namespace librbd {
namespace cache {

template <typename I>
SharedPersistentObjectCacher<I>::SharedPersistentObjectCacher(I *image_ctx, std::string cache_path)
  : m_image_ctx(image_ctx), m_cache_path(cache_path) {
  auto *cct = m_image_ctx->cct;

}

template <typename I>
SharedPersistentObjectCacher<I>::~SharedPersistentObjectCacher() {
  for(auto &it: file_map) {
    if(it.second) {
      delete it.second;
    }
  }
}

template <typename I>
void SharedPersistentObjectCacher<I>::read_object(std::string oid, ceph::bufferlist* read_data, uint64_t offset, uint64_t length, Context *on_finish) {

  auto *cct = m_image_ctx->cct;
  ldout(cct, 20) << "object: " << oid << dendl;

  std::string cache_file_name = m_image_ctx->data_ctx.get_pool_name() + oid;

  os::CacheStore::SyncFile* target_cache_file;

  auto it = file_map.find(cache_file_name);
  if (it == file_map.end()) {
    // open new file and update map
    target_cache_file = new os::CacheStore::SyncFile(cct, cache_file_name);
    file_map.emplace(oid, target_cache_file);
  } else {
    target_cache_file = it->second;
  }

  // read from this object
  target_cache_file->open();
/*
  int ret = target_cache_file->open();
  if (ret < 0) {
    lderr(cct) << "file open error" << ret << dendl;
    on_finish->complete(ret);
  }
*/

  int ret = target_cache_file->read_object_from_file(read_data, offset, length);
  if (ret < 0) {
    ldout(cct, 5) << "read from file return error: " << ret 
                  << "file name= " << cache_file_name
                  << dendl;
  } 

  on_finish->complete(ret);
}


} // namespace cache
} // namespace librbd

template class librbd::cache::SharedPersistentObjectCacher<librbd::ImageCtx>;
