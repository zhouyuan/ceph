// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_LIBRBD_AIO_IMAGE_REQUEST_H
#define CEPH_LIBRBD_AIO_IMAGE_REQUEST_H

#include "include/int_types.h"
#include "include/buffer_fwd.h"
#include "common/snap_types.h"
#include "osd/osd_types.h"
#include "librbd/AioCompletion.h"
#include <list>
#include <utility>
#include <vector>

namespace librbd {

class AioObjectRequest;
class ImageCtx;

template <typename ImageCtxT = ImageCtx>
class AioImageRequest {
public:
  typedef std::vector<std::pair<uint64_t,uint64_t> > Extents;

  virtual ~AioImageRequest() {}

  static void aio_read(ImageCtxT *ictx, AioCompletion *c,
                       Extents &&image_extents, char *buf, bufferlist *pbl,
                       int op_flags, bool bypass_image_cache);
  static void aio_write(ImageCtxT *ictx, AioCompletion *c, uint64_t off,
                        size_t len, const char *buf, int op_flags);
  static void aio_write(ImageCtxT *ictx, AioCompletion *c, uint64_t off,
                        bufferlist &&bl, int op_flags, bool bypass_image_cache);
  static void aio_discard(ImageCtxT *ictx, AioCompletion *c, uint64_t off,
                          uint64_t len, bool bypass_image_cache);
  static void aio_flush(ImageCtxT *ictx, AioCompletion *c,
                        bool bypass_image_cache);

  virtual bool is_write_op() const {
    return false;
  }

  void start_op() {
    m_aio_comp->start_op();
  }

  void send();
  void fail(int r);

protected:
  typedef std::list<AioObjectRequest *> AioObjectRequests;

  ImageCtxT &m_image_ctx;
  AioCompletion *m_aio_comp;
  bool m_bypass_image_cache = false;

  AioImageRequest(ImageCtxT &image_ctx, AioCompletion *aio_comp)
    : m_image_ctx(image_ctx), m_aio_comp(aio_comp) {}

  void set_bypass_image_cache() {
    m_bypass_image_cache = true;
  }

  virtual int clip_request() = 0;
  virtual void send_request() = 0;
  virtual void send_image_cache_request() = 0;
  virtual const char *get_request_type() const = 0;
};

class AioImageRead : public AioImageRequest<> {
public:
  AioImageRead(ImageCtx &image_ctx, AioCompletion *aio_comp,
               Extents&& image_extents, char *buf, bufferlist *pbl,
               int op_flags)
    : AioImageRequest(image_ctx, aio_comp),
      m_image_extents(std::move(image_extents)), m_op_flags(op_flags) {
    m_aio_comp->read_buf = buf;
    m_aio_comp->read_bl = pbl;
  }

protected:
  virtual int clip_request();
  virtual void send_request();
  virtual void send_image_cache_request();

  virtual const char *get_request_type() const {
    return "aio_read";
  }
private:
  Extents m_image_extents;
  int m_op_flags;
};

class AbstractAioImageWrite : public AioImageRequest<> {
public:
  virtual bool is_write_op() const {
    return true;
  }

  inline void flag_synchronous() {
    m_synchronous = true;
  }

protected:
  typedef std::vector<ObjectExtent> ObjectExtents;

  const uint64_t m_off;
  size_t m_len;

  AbstractAioImageWrite(ImageCtx &image_ctx, AioCompletion *aio_comp,
                        uint64_t off, size_t len)
    : AioImageRequest(image_ctx, aio_comp), m_off(off), m_len(len),
      m_synchronous(false) {
  }

  virtual int clip_request();
  virtual void send_request();

  virtual uint32_t get_cache_request_count(bool journaling) const {
    return 0;
  }

  virtual void send_cache_requests(const ObjectExtents &object_extents,
                                   uint64_t journal_tid) = 0;

  virtual void send_object_requests(const ObjectExtents &object_extents,
                                    const ::SnapContext &snapc,
                                    AioObjectRequests *aio_object_requests);
  virtual AioObjectRequest *create_object_request(
      const ObjectExtent &object_extent, const ::SnapContext &snapc,
      Context *on_finish) = 0;

  virtual uint64_t append_journal_event(const AioObjectRequests &requests,
                                        bool synchronous) = 0;
  virtual void update_stats(size_t length) = 0;

private:
  bool m_synchronous;
};

class AioImageWrite : public AbstractAioImageWrite {
public:
  AioImageWrite(ImageCtx &image_ctx, AioCompletion *aio_comp, uint64_t off,
                size_t len, const char *buf, int op_flags)
    : AbstractAioImageWrite(image_ctx, aio_comp, off, len),
      m_op_flags(op_flags) {
    m_bl.append(buf, len);
  }
  AioImageWrite(ImageCtx &image_ctx, AioCompletion *aio_comp, uint64_t off,
                bufferlist &&bl, int op_flags)
    : AbstractAioImageWrite(image_ctx, aio_comp, off, bl.length()),
      m_bl(std::move(bl)), m_op_flags(op_flags) {
  }

protected:
  virtual const char *get_request_type() const {
    return "aio_write";
  }

  void assemble_extent(const ObjectExtent &object_extent, bufferlist *bl);

  virtual void send_image_cache_request();
  virtual void send_cache_requests(const ObjectExtents &object_extents,
                                   uint64_t journal_tid);

  virtual void send_object_requests(const ObjectExtents &object_extents,
                                    const ::SnapContext &snapc,
                                    AioObjectRequests *aio_object_requests);
  virtual AioObjectRequest *create_object_request(
      const ObjectExtent &object_extent, const ::SnapContext &snapc,
      Context *on_finish);

  virtual uint64_t append_journal_event(const AioObjectRequests &requests,
                                        bool synchronous);
  virtual void update_stats(size_t length);
private:
  bufferlist m_bl;
  int m_op_flags;
};

class AioImageDiscard : public AbstractAioImageWrite {
public:
  AioImageDiscard(ImageCtx &image_ctx, AioCompletion *aio_comp, uint64_t off,
                  uint64_t len)
    : AbstractAioImageWrite(image_ctx, aio_comp, off, len) {
  }

protected:
  virtual const char *get_request_type() const {
    return "aio_discard";
  }

  virtual uint32_t get_cache_request_count(bool journaling) const override;

  virtual void send_image_cache_request();
  virtual void send_cache_requests(const ObjectExtents &object_extents,
                                   uint64_t journal_tid);

  virtual AioObjectRequest *create_object_request(
      const ObjectExtent &object_extent, const ::SnapContext &snapc,
      Context *on_finish);

  virtual uint64_t append_journal_event(const AioObjectRequests &requests,
                                        bool synchronous);
  virtual void update_stats(size_t length);
};

class AioImageFlush : public AioImageRequest<> {
public:
  AioImageFlush(ImageCtx &image_ctx, AioCompletion *aio_comp)
    : AioImageRequest(image_ctx, aio_comp) {
  }

  virtual bool is_write_op() const {
    return true;
  }

protected:
  virtual int clip_request() {
    return 0;
  }
  virtual void send_request();
  virtual void send_image_cache_request();
  virtual const char *get_request_type() const {
    return "aio_flush";
  }
};

} // namespace librbd

extern template class librbd::AioImageRequest<librbd::ImageCtx>;

#endif // CEPH_LIBRBD_AIO_IMAGE_REQUEST_H
