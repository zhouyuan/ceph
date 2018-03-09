// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_LIBOS_CACHE_STORE_SYNC_FILE
#define CEPH_LIBOS_CACHE_STORE_SYNC_FILE

#include "include/buffer_fwd.h"
#include <string>

struct Context;
struct ContextWQ;
class CephContext;

namespace os {

struct ImageCtx;

namespace CacheStore {

class SyncFile {
public:
  SyncFile(CephContext *cct, ContextWQ &work_queue, const std::string &name);
  ~SyncFile();

  // TODO use IO queue instead of individual commands so operations can be
  // submitted in batch

  // TODO use scatter/gather API

  void open(Context *on_finish);
  void close(Context *on_finish);

  void read(uint64_t offset, uint64_t length, ceph::bufferlist *bl,
            Context *on_finish);
  void write(uint64_t offset, ceph::bufferlist &&bl, bool fdatasync,
             Context *on_finish);
  void discard(uint64_t offset, uint64_t length,
               bool fdatasync, Context *on_finish);
  void truncate(uint64_t length, bool fdatasync, Context *on_finish);

  void fsync(Context *on_finish);
  void fdatasync(Context *on_finish);
  uint64_t filesize();

private:
  CephContext *cct;
  ContextWQ &m_work_queue;
  std::string m_name;
  int m_fd = -1;

  int write(uint64_t offset, const ceph::bufferlist &bl, bool fdatasync);
  int read(uint64_t offset, uint64_t length, ceph::bufferlist *bl);
  int discard(uint64_t offset, uint64_t length, bool fdatasync);
  int truncate(uint64_t length, bool fdatasync);
  int fdatasync();
};

} // namespace CacheStore
} // namespace os

#endif // CEPH_LIBOS_CACHE_STORE_SYNC_FILE
