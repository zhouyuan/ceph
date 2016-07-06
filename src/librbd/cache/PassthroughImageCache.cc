// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "PassthroughImageCache.h"
#include "include/buffer.h"
#include "common/dout.h"
#include "librbd/ImageCtx.h"

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd::PassthroughImageCache: " << __func__ \
                           << ": "

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

  int ret = 0;
  for (auto &image_extent : image_extents) {
    if (image_extent.second == 0) {
      continue;
    }
    uint64_t offset = image_extent.first;
    ret = m_image_ctx.simple_wal->read(offset/4096, bl->c_str(), offset, image_extent.second);

    ldout(cct, 0) << "extent=" << image_extent << ", "
                  << ret << dendl;
  }

  if (ret > 0 ) {
    on_finish->complete(ret);
  } else {
    m_image_writeback.aio_read(std::move(image_extents), bl, fadvise_flags,
                               on_finish);
  }
}

template <typename I>
void PassthroughImageCache<I>::aio_write(uint64_t offset, bufferlist&& bl,
                                         int fadvise_flags,
                                         Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "offset=" << offset << ", "
                 << "length=" << bl.length() << ", "
                 << "on_finish=" << on_finish << dendl;

  //assert(m_image_ctx.simple_wal != nullptr);
  m_image_ctx.simple_wal->write(offset/4096, bl.c_str(), offset, bl.length());
  m_image_writeback.aio_write(offset, std::move(bl), fadvise_flags, on_finish);
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
