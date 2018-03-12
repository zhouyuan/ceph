// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_LIBRBD_CACHE_PASSTHROUGH_IMAGE_CACHE
#define CEPH_LIBRBD_CACHE_PASSTHROUGH_IMAGE_CACHE

#include "ImageCache.h"
#include "ImageStore.h"
#include "ImageWriteback.h"
#include "librbd/cache/BlockGuard.h"
#include "tools/rbd_cache/AdminSocketClient.hpp"

namespace librbd {

struct ImageCtx;

namespace cache {

/**
 * Example passthrough client-side, image extent cache
 */
template <typename ImageCtxT = librbd::ImageCtx>
class SharedImageCache : public ImageCache {
public:
  SharedImageCache(ImageCtx &image_ctx);
  ~SharedImageCache();

  /// client AIO methods
  void aio_read(Extents&& image_extents, ceph::bufferlist *bl,
                int fadvise_flags, Context *on_finish) override;
  void aio_write(Extents&& image_extents, ceph::bufferlist&& bl,
                 int fadvise_flags, Context *on_finish) override;
  void aio_discard(uint64_t offset, uint64_t length,
                   bool skip_partial_discard, Context *on_finish);
  void aio_flush(Context *on_finish) override;
  void aio_writesame(uint64_t offset, uint64_t length,
                     ceph::bufferlist&& bl,
                     int fadvise_flags, Context *on_finish) override;
  void aio_compare_and_write(Extents&& image_extents,
                             ceph::bufferlist&& cmp_bl, ceph::bufferlist&& bl,
                             uint64_t *mismatch_offset,int fadvise_flags,
                             Context *on_finish) override;

  /// internal state methods
  void init(Context *on_finish) override;
  void shut_down(Context *on_finish) override;

  void invalidate(Context *on_finish) override;
  void flush(Context *on_finish) override;

  ImageStore<ImageCtxT> *m_image_store = nullptr;

private:
  void map_blocks(IOType io_type, Extents &&image_extents,
                  BlockGuard::C_BlockRequest *block_request);
  void map_block(BlockGuard::BlockIO &&block_io);
  void client_handle_request(std::string msg);

  ImageCtxT &m_image_ctx;
  BlockGuard m_block_guard;
  ImageWriteback<ImageCtxT> m_image_writeback;
  CacheClient *m_cache_client = nullptr;
  boost::asio::io_service io_service;

};

} // namespace cache
} // namespace librbd

extern template class librbd::cache::SharedImageCache<librbd::ImageCtx>;

#endif // CEPH_LIBRBD_CACHE_PASSTHROUGH_IMAGE_CACHE
