// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_LIBRBD_CACHE_STORE_SYNC_FILE
#define CEPH_LIBRBD_CACHE_STORE_SYNC_FILE

#include "include/buffer_fwd.h"
#include "include/Context.h"
#include "common/WorkQueue.h"
#include <sys/mman.h>
#include <string>

struct Context;
struct ContextWQ;
class CephContext;

namespace librbd {

namespace cache {

class SyncFile {
public:
  SyncFile(CephContext *cct, const std::string &name);
  ~SyncFile();

//  virtual int send() = 0;

  // TODO use IO queue instead of individual commands so operations can be
  // submitted in batch

  // TODO use scatter/gather API

  void open(Context *on_finish);

  // ##
  void open();
  bool try_open();
  void close(Context *on_finish);
  void remove(Context *on_finish);

  void read(uint64_t offset, uint64_t length, ceph::bufferlist *bl, Context *on_finish);

  void write(uint64_t offset, ceph::bufferlist &&bl, bool fdatasync, Context *on_finish);

  void discard(uint64_t offset, uint64_t length, bool fdatasync, Context *on_finish);

  void truncate(uint64_t length, bool fdatasync, Context *on_finish);

  void fsync(Context *on_finish);

  void fdatasync(Context *on_finish);

  uint64_t filesize();

  int load(void** dest, uint64_t filesize);

  int remove();

  // ============== new interface =================

  int open(int flag, std::string temp_name);

  int rename(std::string old_name, std::string new_name);
 
  int remove(std::string delete_file_name);  

  int write_object_to_file(ceph::bufferlist read_buf, uint64_t object_len);
  int read_object_from_file(ceph::bufferlist* read_buf, uint64_t object_off, uint64_t object_len);

  CephContext* get_ceph_context() {
    return cct;
  }

  std::string get_file_name() {
    return m_name;
  }

private:
  CephContext *cct;
  std::string m_name;
  int m_fd = -1;

  int write(uint64_t offset, const ceph::bufferlist &bl, bool fdatasync);
  int read(uint64_t offset, uint64_t length, ceph::bufferlist *bl);
  int discard(uint64_t offset, uint64_t length, bool fdatasync);
  int truncate(uint64_t length, bool fdatasync);
  int fdatasync();
};


class ReadSyncFileRequest : public Context {
public:
  ReadSyncFileRequest(CephContext* cct, ContextWQ* op_work_queue, 
                      const std::string& file_name, ceph::bufferlist* read_bl,
                      const uint64_t object_off, const uint64_t object_len,
                      Context* on_finish);
  ~ReadSyncFileRequest(){}

  void finish(int r);

  int send(){
    m_op_work_queue->queue(this, 0);
    return 0;
  }

private:
  Context* m_on_finish;
  SyncFile m_sync_file;
  ceph::bufferlist* m_read_bl;
  const uint64_t m_object_offset;
  const uint64_t m_object_len;
  ContextWQ* m_op_work_queue;
};

// =========

class WriteSyncFileRequest : public Context {
public:
  WriteSyncFileRequest(CephContext* cct, const std::string& file_name, 
                       ceph::bufferlist& write_bl,
                       uint64_t object_off, 
                       uint64_t object_len,
                       Context* on_finish);
  ~WriteSyncFileRequest(){}
  int send(){
    m_op_work_queue->queue(this, 0);
    return 0;
  }

  void finish(int r);
private:
  ContextWQ* m_op_work_queue;
  Context* m_on_finish;
  SyncFile m_sync_file;
  ceph::bufferlist& m_write_bl;
  const uint64_t m_object_offset;
  const uint64_t m_object_len;
};


class DeleteSyncFileRequest : public Context {
public:
  DeleteSyncFileRequest(CephContext* cct, ContextWQ* op_work_queue, 
                        const std::string& file_name, 
                        Context* on_finish);

  ~DeleteSyncFileRequest(){}

  void finish(int r);

  int send(){return 0;}
private:
  SyncFile m_sync_file;
  ContextWQ* m_op_work_queue;
  Context* m_on_finish;
};



} // namespace cache
} // namespace librbd

#endif // CEPH_LIBRBD_CACHE_STORE_SYNC_FILE
