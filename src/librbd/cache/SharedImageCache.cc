// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "librbd/cache/SharedImageCache.h"
#include "librbd/cache/ImageStore.h"
#include "librbd/cache/Types.h"
#include "librbd/cache/Block.h"
#include "include/buffer.h"
#include "common/dout.h"
#include "librbd/ImageCtx.h"

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd::SharedImageCache: " << this << " " \
                           <<  __func__ << ": "

namespace librbd {
namespace cache {

static const uint32_t BLOCK_SIZE = 4096;
typedef std::map<uint64_t, bufferlist> ExtentBuffers;
typedef std::list<bufferlist> Buffers;
typedef std::function<void(uint64_t)> ReleaseBlock;

struct C_ReleaseBlockGuard : public BlockGuard::C_BlockIORequest {
  BlockGuard::C_BlockRequest *block_request;
  BlockGuard::BlockIO block_io;

  C_ReleaseBlockGuard(CephContext *cct,
                      BlockGuard::C_BlockRequest *block_request,
		      BlockGuard::BlockIO &block_io)
    : C_BlockIORequest(cct, nullptr),
      block_request(block_request), block_io(block_io) {
  }

  virtual void send() override {
    ldout(cct, 20) << "(" << get_name()
                  << ") , this req is " << this
                  << " , next req is " << next_block_request
                  << " , block_io = " << block_io.block_info->block
                  << dendl;
    auto block_info = block_io.block_info;
    //block_info->lock.lock();
    if(next_block_request == nullptr
	&& block_info->tail_block_io_request == this) {
      block_info->tail_block_io_request = nullptr;
      block_info->in_process = false;
    }
    finish(0);
    //block_info->lock.unlock();
    if(next_block_request != nullptr) {
      next_block_request->send();
    }
  }
  virtual const char *get_name() const override {
    return "C_ReleaseBlockGuard";
  }

  virtual void finish(int r) override {
    ldout(cct, 20) << "(" << get_name()
                   << "): block_io = "
                   << block_io.block_info->block
                   << ", r=" << r << dendl;

    // complete block request
    block_request->complete_request(r);
  }
};

template <typename I>
struct C_ReadFromCacheRequest : public BlockGuard::C_BlockIORequest {
  ImageStore<I> &image_store;
  BlockGuard::BlockIO block_io;
  ExtentBuffers *extent_buffers;

  C_ReadFromCacheRequest(CephContext *cct, ImageStore<I> &image_store,
                         BlockGuard::BlockIO &&block_io,
                         ExtentBuffers *extent_buffers,
                         C_BlockIORequest *next_block_request)
    : C_BlockIORequest(cct, next_block_request),
      image_store(image_store), block_io(block_io),
      extent_buffers(extent_buffers) {
  }

  virtual void send() override {
    ldout(cct, 20) << "(" << get_name() << "): "
                   << "block_io=[" << block_io << "]" << dendl;
    C_Gather *ctx = new C_Gather(cct, this);
    for (auto &extent : block_io.extents) {
      image_store.read_block((block_io.block_info->block)*BLOCK_SIZE,
                             {{extent.block_offset, extent.block_length}},
                             &(*extent_buffers)[extent.buffer_offset],
                             ctx->new_sub());
    }
    ctx->activate();
  }
  virtual const char *get_name() const override {
    return "C_ReadFromCacheRequest";
  }
};

template <typename I>
struct C_ReadFromImageRequest : public BlockGuard::C_BlockIORequest {
  ImageWriteback<I> &image_writeback;
  BlockGuard::BlockIO block_io;
  ExtentBuffers *extent_buffers;

  C_ReadFromImageRequest(CephContext *cct, ImageWriteback<I> &image_writeback,
                         BlockGuard::BlockIO &&block_io,
                         ExtentBuffers *extent_buffers,
                         C_BlockIORequest *next_block_request)
    : C_BlockIORequest(cct, next_block_request),
      image_writeback(image_writeback), block_io(block_io),
      extent_buffers(extent_buffers) {
  }

  virtual void send() override {
    ldout(cct, 20) << "(" << get_name() << "): "
                   << "block_io=[" << block_io << "]" << dendl;

    // TODO improve scatter/gather to include buffer offsets
    uint64_t image_offset = block_io.block_info->block * BLOCK_SIZE;
    C_Gather *ctx = new C_Gather(cct, this);
    for (auto &extent : block_io.extents) {
      image_writeback.aio_read({{image_offset + extent.block_offset,
                                 extent.block_length}},
                               &(*extent_buffers)[extent.buffer_offset],
                               0, ctx->new_sub());
    }
    ctx->activate();
  }
  virtual const char *get_name() const override {
    return "C_ReadFromImageRequest";
  }
};

template <typename I>
struct C_ReadBlockRequest : public BlockGuard::C_BlockRequest {
  I &image_ctx;
  SharedImageCache<I> *cache_ctx;
  bufferlist *bl;

  ExtentBuffers extent_buffers;
  Buffers promote_buffers;

  C_ReadBlockRequest(I &image_ctx,
                     SharedImageCache<I> *cache_ctx,
                     bufferlist *bl,
                     Context *on_finish)
    : C_BlockRequest(on_finish), image_ctx(image_ctx),
      cache_ctx(cache_ctx), bl(bl) {
  }

  virtual void remap(bool policy_map_result,
                     BlockGuard::BlockIO &&block_io) {
    CephContext *cct = image_ctx.cct;

    // TODO: consolidate multiple reads into a single request (i.e. don't
    // have 1024 4K requests to read a single object)

    // NOTE: block guard active -- must be released after IO completes
    BlockGuard::C_BlockIORequest *req = new C_ReleaseBlockGuard(cct, this, block_io);
    BlockGuard::C_BlockIORequest *orig_tail_block_io_req = nullptr;
    auto block_info = block_io.block_info;
    std::lock_guard<std::mutex> lock(block_info->lock);
    if(block_info->tail_block_io_request != nullptr) {
      orig_tail_block_io_req = (BlockGuard::C_BlockIORequest*)block_info->tail_block_io_request;
    }
    block_info->tail_block_io_request = (void*)req;

    if (policy_map_result) {
      req = new C_ReadFromCacheRequest<I>(cct, *cache_ctx->m_image_store,
                                          std::move(block_io),
                                          &extent_buffers, req);

    } else {
      req = new C_ReadFromImageRequest<I>(cct, cache_ctx->m_image_writeback,
                                          std::move(block_io), &extent_buffers,
                                          req);
    }

    if(orig_tail_block_io_req != nullptr) {
      orig_tail_block_io_req->next_block_request = req;
    }
    if(!block_info->in_process) {
      ldout(cct, 10) << "block_io: "<< block_io << " is not in process, will schedule" << dendl;
      block_info->in_process = true;
      //schedule send on another tread
      req->send();
    }else{
      ldout(cct, 10) << "block_io: "<< block_io << " is in process, skip schedule" << dendl;
    }
  }

  virtual void finish(int r) override {
    CephContext *cct = image_ctx.cct;
    ldout(cct, 20) << "(C_ReadBlockRequest): r=" << r << dendl;

    if (r < 0) {
      C_BlockRequest::finish(r);
      return;
    }

    ldout(cct, 20) << "assembling read extents" << dendl;
    for (auto &extent_bl : extent_buffers) {
      ldout(cct, 20) << extent_bl.first << "~" << extent_bl.second.length()
                     << dendl;
      bl->claim_append(extent_bl.second);
    }
    C_BlockRequest::finish(0);
  }
};

template <typename I>
SharedImageCache<I>::SharedImageCache(ImageCtx &image_ctx)
  : m_image_ctx(image_ctx),
    m_block_guard(image_ctx.cct, image_ctx.size, BLOCK_SIZE),
    m_image_writeback(image_ctx) {
}

template <typename I>
SharedImageCache<I>::~SharedImageCache() {
  if (m_cache_client) {
    delete m_cache_client;
    m_cache_client = nullptr;
  }
}

template <typename I>
void SharedImageCache<I>::aio_read(Extents &&image_extents, bufferlist *bl,
                                        int fadvise_flags, Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "image_extents=" << image_extents << ", "
                 << "on_finish=" << on_finish << dendl;

  if (m_cache_client && m_cache_client->connected && m_image_store) {

    // try to read from parent image
    BlockGuard::C_BlockRequest *req = new C_ReadBlockRequest<I>(
      m_image_ctx, this, bl, on_finish);
    ldout(cct, 20) << "to map" << dendl;
    return map_blocks(IO_TYPE_READ, std::move(image_extents), req);

  }

  m_image_writeback.aio_read(std::move(image_extents), bl, fadvise_flags,
                             on_finish);
}

template <typename I>
void SharedImageCache<I>::aio_write(Extents &&image_extents,
                                         bufferlist&& bl,
                                         int fadvise_flags,
                                         Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "image_extents=" << image_extents << ", "
                 << "on_finish=" << on_finish << dendl;

  m_image_writeback.aio_write(std::move(image_extents), std::move(bl),
                              fadvise_flags, on_finish);
}

template <typename I>
void SharedImageCache<I>::aio_discard(uint64_t offset, uint64_t length,
                                           bool skip_partial_discard, Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "offset=" << offset << ", "
                 << "length=" << length << ", "
                 << "on_finish=" << on_finish << dendl;

  m_image_writeback.aio_discard(offset, length, skip_partial_discard, on_finish);
}

template <typename I>
void SharedImageCache<I>::aio_flush(Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "on_finish=" << on_finish << dendl;

  m_image_writeback.aio_flush(on_finish);
}

template <typename I>
void SharedImageCache<I>::aio_writesame(uint64_t offset, uint64_t length,
                                             bufferlist&& bl, int fadvise_flags,
                                             Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "offset=" << offset << ", "
                 << "length=" << length << ", "
                 << "data_len=" << bl.length() << ", "
                 << "on_finish=" << on_finish << dendl;

  m_image_writeback.aio_writesame(offset, length, std::move(bl), fadvise_flags,
                                  on_finish);
}

template <typename I>
void SharedImageCache<I>::aio_compare_and_write(Extents &&image_extents,
                                                     bufferlist&& cmp_bl,
                                                     bufferlist&& bl,
                                                     uint64_t *mismatch_offset,
                                                     int fadvise_flags,
                                                     Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "image_extents=" << image_extents << ", "
                 << "on_finish=" << on_finish << dendl;

  m_image_writeback.aio_compare_and_write(
    std::move(image_extents), std::move(cmp_bl), std::move(bl), mismatch_offset,
    fadvise_flags, on_finish);
}

template <typename I>
void SharedImageCache<I>::init(Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << dendl;


  if (m_image_ctx.parent != nullptr) {
    ldout(cct, 20) << "child image: skipping cache client" << dendl;
    on_finish->complete(0);
    return;
  }

  ldout(cct, 20) << "parent image: setup cache client = "
                 << m_image_ctx.name
                 << dendl;
  std::string controller_path = "/tmp/rbd_shared_readonly_cache_demo";
  m_cache_client = new CacheClient(io_service, controller_path.c_str(),
    ([&](std::string s){client_handle_request(s);}));

  int ret = m_cache_client->connect();
  if (ret < 0) {
    ldout(cct, 20) << "cache client fail to connect: " << dendl;
  } else {
    //TODO: ping cache daemon to init cache layout
    ldout(cct, 20) << "cache client register volume "
                   << "name = " << m_image_ctx.id 
                   << dendl;
    m_cache_client->register_volume(m_image_ctx.data_ctx.get_pool_name(),
                                    m_image_ctx.id, m_image_ctx.size);
      //m_image_store = new ImageStore<I>(m_image_ctx, m_image_ctx.size, m_image_ctx.id);
      //m_image_store->open();



    //return m_image_store->init(on_finish);
  }

  on_finish->complete(0);
}

template <typename I>
void SharedImageCache<I>::shut_down(Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << dendl;

  on_finish->complete(0);
}

template <typename I>
void SharedImageCache<I>::invalidate(Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << dendl;

  // dump cache contents (don't have anything)
  on_finish->complete(0);
}

template <typename I>
void SharedImageCache<I>::flush(Context *on_finish) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << dendl;

  // internal flush -- nothing to writeback but make sure
  // in-flight IO is flushed
  aio_flush(on_finish);
}

template <typename I>
void SharedImageCache<I>::map_blocks(IOType io_type, Extents &&image_extents,
                                   BlockGuard::C_BlockRequest *block_request) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << io_type << dendl;

  BlockGuard::BlockIOs block_ios;
  m_block_guard.create_block_ios(io_type, image_extents, &block_ios,
                                 block_request);

  // map block IO requests to the cache or backing image based upon policy
  for (auto &block_io : block_ios) {
    map_block(std::move(block_io));
  }

  // advance the policy statistics
  block_request->activate();
}

template <typename I>
void SharedImageCache<I>::map_block(BlockGuard::BlockIO &&block_io) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << dendl;

  bool exists = false;
  m_cache_client->lookup_block(m_image_ctx.data_ctx.get_pool_name(),
    m_image_ctx.id, block_io.block_info->block, &exists);
  //TODO(): move to process call back
  block_io.block_request->remap(1, std::move(block_io));
}

template <typename I>
void SharedImageCache<I>::client_handle_request(std::string msg) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << dendl;

  rbdsc_req_type_t *io_ctx = (rbdsc_req_type_t*)(msg.c_str());

  switch (io_ctx->type) {
    case RBDSC_REGISTER_REPLY: {
      // open cache handler for volume        
      ldout(cct, 20) << "client open cache handler" << dendl;
      m_image_store = new ImageStore<I>(m_image_ctx, m_image_ctx.size, m_image_ctx.id);
      if (m_image_store) {
        m_image_store->open();
      }

      break;
    }
    case RBDSC_READ_REPLY: {
      ldout(cct, 20) << "client start to read" << dendl;

      break;
    }
    
  }
}




} // namespace cache
} // namespace librbd

template class librbd::cache::SharedImageCache<librbd::ImageCtx>;
