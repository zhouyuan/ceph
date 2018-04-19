// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "ImageStore.h"
#include "include/buffer.h"
#include "common/dout.h"

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd::cache::file::ImageStore: " << this \
                           << " " <<  __func__ << ": "

namespace rbd {
namespace cache {
namespace file {


ImageStore::ImageStore(CephContext *cct, std::string volume_name, uint64_t image_size, ContextWQ *work_queue)
  : m_image_size(image_size),
    m_cct(cct), op_work_queue(work_queue),
    m_cache_file(cct, *op_work_queue,
                 volume_name + ".image_cache") {
  ldout(m_cct, 20) << "volume_name: " << volume_name << dendl;
}


void ImageStore::init(Context *on_finish) {
  ldout(m_cct, 20) << dendl;

  // TODO don't reset the read-only cache
  Context *ctx = new FunctionContext(
    [this, on_finish](int r) {
      if (r < 0) {
        on_finish->complete(r);
        return;
      }

      reset(on_finish);
    });
  m_cache_file.open(ctx);
}


void ImageStore::remove(Context *on_finish) {
  ldout(m_cct, 20) << dendl;

  m_cache_file.remove(on_finish);
}


void ImageStore::shut_down(Context *on_finish) {
  ldout(m_cct, 20) << dendl;

  // TODO
  m_cache_file.close(on_finish);
}


void ImageStore::reset(Context *on_finish) {
  ldout(m_cct, 20) << "Cache_Size: " << m_image_size << dendl;

  // TODO
  m_cache_file.truncate(m_image_size, false, on_finish);
}


void ImageStore::read_block(uint64_t offset,
                               BlockExtents &&block_extents,
                               bufferlist *bl, Context *on_finish) {
  /*ldout(m_cct, 20) << "block=" << cache_block << ", "
                 << "extents=" << block_extents << dendl;*/

  // TODO add gather support
  assert(block_extents.size() == 1);
  auto &extent = block_extents.front();
  m_cache_file.read(offset + extent.first,
                    extent.second, bl, on_finish);
}


void ImageStore::write_block(uint64_t offset,
                                BlockExtents &&block_extents,
                                bufferlist &&bl, Context *on_finish) {
  /*ldout(m_cct, 20) << "block=" << cache_block << ", "
                 << "extents=" << block_extents << dendl;*/

  // TODO add scatter support
  C_Gather *ctx = new C_Gather(m_cct, on_finish);
  uint64_t buffer_offset = 0;
  for (auto &extent : block_extents) {
    bufferlist sub_bl;
    sub_bl.substr_of(bl, buffer_offset, extent.second);
    buffer_offset += extent.second;

    m_cache_file.write(offset + extent.first,
                       std::move(sub_bl), false, ctx->new_sub());

  }
  ctx->activate();
}


void ImageStore::discard_block(uint64_t cache_block, Context *on_finish) {
  ldout(m_cct, 20) << dendl;

  // TODO
  on_finish->complete(0);
}


bool ImageStore::check_exists() {
    return m_cache_file.try_open();
}

} // namespace file
} // namespace cache
} // namespace rbd
