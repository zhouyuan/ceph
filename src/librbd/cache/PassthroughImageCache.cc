// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "PassthroughImageCache.h"
#include "include/buffer.h"
#include "common/dout.h"
#include "librbd/ImageCtx.h"

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd::PassthroughImageCache: " << this << " " \
                           <<  __func__ << ": "

namespace librbd {
namespace cache {

template <typename I>
PassthroughImageCache<I>::PassthroughImageCache(ImageCtx &image_ctx)
  : m_image_ctx(image_ctx), m_image_writeback(image_ctx) {
}

template <typename I>
void PassthroughImageCache<I>::aio_read(Extents &&image_extents, bufferlist *bl,
                                        int fadvise_flags, Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "image_extents=" << image_extents << ", "
                 << "on_finish=" << on_finish << dendl;

  int ret = -1;
  uint64_t buffer_offset = 0;
  for (auto &image_extent : image_extents) {
    uint64_t length = image_extent.second;
    if (0 == length || 0 != (length % cct->_conf->rbd_image_cache_block_size)) {
      ldout(cct, 20) << "skip cache read extent=" << image_extent << dendl;
      continue;
    }
    uint64_t offset = image_extent.first;
    bufferlist sub_bl;
    ret = m_image_ctx.sbc->read(cct->_conf->rbd_image_cache_block_size,
                                sub_bl.c_str(), offset, length);

    if (ret < 0) {
      ldout(cct, 20) << "cache read extent=" << image_extent << ", "
                     << "error " << ret << dendl;
      break;
    }
    ldout(cct, 20) << "cache read extent=" << image_extent << ", "
                   << ret << dendl;
    bl->append(sub_bl);
  }

  if (ret > 0) {
    //  promote to cache
    buffer_offset = 0;
    for (auto &image_extent : image_extents) {
      uint64_t length = image_extent.second;
      if (0 == length || 0 != (length % cct->_conf->rbd_image_cache_block_size)) {
        continue;
      }
      uint64_t offset = image_extent.first;
      bufferlist sub_bl;
      sub_bl.substr_of(*bl, buffer_offset, length);
      ret = m_image_ctx.sbc->write(offset/cct->_conf->rbd_image_cache_block_size,
                                   sub_bl.c_str(), offset, length);

      buffer_offset += length;
      ldout(cct, 20) << "extent=" << image_extent << ", "
                     << ret << dendl;
    }
    on_finish->complete(ret);
  } else {
    m_image_writeback.aio_read(std::move(image_extents), bl, fadvise_flags,
                               on_finish);
  }
}

template <typename I>
void PassthroughImageCache<I>::aio_write(Extents &&image_extents,
                                         bufferlist&& bl,
                                         int fadvise_flags,
                                         Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "image_extents=" << image_extents << ", "
                 << "on_finish=" << on_finish << dendl;

  int ret = -1;
  uint64_t buffer_offset = 0;
  for (auto &image_extent : image_extents) {
    uint64_t length = image_extent.second;
    if (0 == length || 0 != (length % cct->_conf->rbd_image_cache_block_size)) {
      ldout(cct, 20) << "skip cache write extent=" << image_extent << dendl;
      continue;
    }
    uint64_t offset = image_extent.first;
    bufferlist sub_bl;
    sub_bl.substr_of(bl, buffer_offset, length);
    ret = m_image_ctx.sbc->write(offset/cct->_conf->rbd_image_cache_block_size,
                                 sub_bl.c_str(), offset, length);

    buffer_offset += length;
    ldout(cct, 20) << "cache write extent=" << image_extent << ", "
                   << ret << dendl;
  }

  m_image_writeback.aio_write(std::move(image_extents), std::move(bl),
                              fadvise_flags, on_finish);
}

template <typename I>
void PassthroughImageCache<I>::aio_discard(uint64_t offset, uint64_t length,
                                           Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "offset=" << offset << ", "
                 << "length=" << length << ", "
                 << "on_finish=" << on_finish << dendl;

  m_image_writeback.aio_discard(offset, length, on_finish);
}

template <typename I>
void PassthroughImageCache<I>::aio_flush(Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "on_finish=" << on_finish << dendl;

  m_image_writeback.aio_flush(on_finish);
}

template <typename I>
void PassthroughImageCache<I>::init(Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << dendl;

  on_finish->complete(0);
}

template <typename I>
void PassthroughImageCache<I>::shut_down(Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << dendl;

  on_finish->complete(0);
}

template <typename I>
void PassthroughImageCache<I>::invalidate(Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << dendl;

  // dump cache contents (don't have anything)
  on_finish->complete(0);
}

template <typename I>
void PassthroughImageCache<I>::flush(Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << dendl;

  // internal flush -- nothing to writeback but make sure
  // in-flight IO is flushed
  aio_flush(on_finish);
}

} // namespace cache
} // namespace librbd

template class librbd::cache::PassthroughImageCache<librbd::ImageCtx>;
