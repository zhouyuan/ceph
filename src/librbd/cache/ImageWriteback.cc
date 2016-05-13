// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "ImageWriteback.h"
#include "include/buffer.h"
#include "common/dout.h"
#include "librbd/AioCompletion.h"
#include "librbd/AioImageRequest.h"
#include "librbd/ImageCtx.h"

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd::ImageWriteback: " << __func__ << ": "

namespace librbd {
namespace cache {

template <typename I>
ImageWriteback<I>::ImageWriteback(I &image_ctx) : m_image_ctx(image_ctx) {
}

template <typename I>
void ImageWriteback<I>::aio_read(Extents &&image_extents, bufferlist *bl,
                                 int fadvise_flags, Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "image_extents=" << image_extents << ", "
                 << "on_finish=" << on_finish << dendl;

  AioCompletion *aio_comp = AioCompletion::create(on_finish);
  AioImageRequest<I>::aio_read(&m_image_ctx, aio_comp, std::move(image_extents),
                               nullptr, bl, fadvise_flags, true);
}

template <typename I>
void ImageWriteback<I>::aio_write(uint64_t offset, ceph::bufferlist&& bl,
                                  int fadvise_flags, Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "offset=" << offset << ", "
                 << "length=" << bl.length() << ", "
                 << "on_finish=" << on_finish << dendl;

  AioCompletion *aio_comp = AioCompletion::create(on_finish);
  AioImageRequest<I>::aio_write(&m_image_ctx, aio_comp, offset, std::move(bl),
                                fadvise_flags, true);
}

template <typename I>
void ImageWriteback<I>::aio_discard(uint64_t offset, uint64_t length,
                                    Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "offset=" << offset << ", "
                 << "length=" << length << ", "
                << "on_finish=" << on_finish << dendl;

  AioCompletion *aio_comp = AioCompletion::create(on_finish);
  AioImageRequest<I>::aio_discard(&m_image_ctx, aio_comp, offset, length, true);
}

template <typename I>
void ImageWriteback<I>::aio_flush(Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "on_finish=" << on_finish << dendl;

  AioCompletion *aio_comp = AioCompletion::create(on_finish);
  AioImageRequest<I>::aio_flush(&m_image_ctx, aio_comp, true);
}

} // namespace cache
} // namespace librbd

template class librbd::cache::ImageWriteback<librbd::ImageCtx>;

