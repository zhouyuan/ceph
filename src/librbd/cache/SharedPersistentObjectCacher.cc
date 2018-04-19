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
SharedPersistentObjectCacher<I>::SharedPersistentObjectCacher(I *image_ctx, std::string volume_name)
  : m_image_ctx(image_ctx), m_volume_name(volume_name) {
  auto *cct = m_image_ctx->cct;
  ldout(cct, 20) << "volume_name: " << volume_name << dendl;
  //m_cache_path = cct->_conf->get_val<std::string>("rbd_shared_cache_path");

}

template <typename I>
void SharedPersistentObjectCacher<I>::read_object(std::string oid, uint64_t offset, uint64_t length, Context *on_finish) {
  auto it = file_map.find(oid);
  if (it == file_map.end()) {
    // open new file and update map
  }

  // read from this object
 
}


} // namespace cache
} // namespace librbd

template class librbd::cache::SharedPersistentObjectCacher<librbd::ImageCtx>;
