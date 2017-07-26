// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "librbd/cache/file/MetaStore.h"
#include "include/stringify.h"
#include "common/dout.h"
#include "librbd/ImageCtx.h"
#include <string>

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd::cache::file::MetaStore: " << this \
                           << " " <<  __func__ << ": "

namespace librbd {
namespace cache {
namespace file {

template <typename I>
MetaStore<I>::MetaStore(I &image_ctx, uint64_t block_count)
  : m_image_ctx(image_ctx), m_block_count(block_count),
    m_aio_file(image_ctx.cct, *image_ctx.op_work_queue, image_ctx.id + ".meta"), init_m_loc_map(false) {
    //m_lock("librbd::cache::file::MetaStore::m_lock") {
      //m_loc_map = new uint8_t[block_count]();
}

template <typename I>
void MetaStore<I>::init(Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << dendl;

  // TODO
  if (!m_aio_file.try_open()) {
    init_m_loc_map = true;
  }
  m_aio_file.open(on_finish);
}

template <typename I>
void MetaStore<I>::remove(Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << dendl;

  m_aio_file.remove(on_finish);
}

template <typename I>
void MetaStore<I>::shut_down(Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << dendl;

  // TODO
  m_aio_file.close(on_finish);
}

template <typename I>
void MetaStore<I>::load(uint8_t loc) {
  assert(m_aio_file.load((void**)&m_loc_map, m_block_count) == 0);
  if (init_m_loc_map) {
    for(uint64_t block = 0; block < m_block_count; block++) {
      m_loc_map[block] = loc;
    }
  }
}

template <typename I>
void MetaStore<I>::update(uint64_t block_id, uint8_t loc) {
  //Mutex::Locker locker(m_lock);
  std::lock_guard<std::mutex> lock(m_lock);
  m_loc_map[block_id] = loc;
}

template <typename I>
void MetaStore<I>::get_loc_map(uint8_t* dest) {
  for(uint64_t block = 0; block < m_block_count; block++) {
    dest[block] = m_loc_map[block];
  }
}

} // namespace file
} // namespace cache
} // namespace librbd

template class librbd::cache::file::MetaStore<librbd::ImageCtx>;
