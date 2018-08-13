// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "SharedPersistentObjectCacherFile.h"
#include "include/Context.h"
#include "common/dout.h"
#include "common/WorkQueue.h"
#include "librbd/ImageCtx.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <utility>

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd::cache::SyncFile: " << this << " " \
                           <<  __func__ << ": "

namespace librbd {
namespace cache {

SyncFile::SyncFile(CephContext *cct, const std::string &name)
  : cct(cct) {
  m_name = cct->_conf.get_val<std::string>("rbd_shared_cache_path") + "/rbd_cache." + name;
  ldout(cct, 20) << "file path=" << m_name << dendl;
}

SyncFile::~SyncFile() {
  // TODO force proper cleanup
  if (m_fd != -1) {
    ::close(m_fd);
  }
}

void SyncFile::open(Context *on_finish) {
  while (true) {
    m_fd = ::open(m_name.c_str(), O_CREAT | O_DIRECT | O_NOATIME | O_RDWR | O_SYNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (m_fd == -1) {
      int r = -errno;
      if (r == -EINTR) {
        continue;
      }
      on_finish->complete(r);
      return;
    }
    break;
  }

  on_finish->complete(0);
}

void SyncFile::open() {
  while (true) 
  {
    m_fd = ::open(m_name.c_str(), O_CREAT | O_NOATIME | O_RDWR | O_SYNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (m_fd == -1) 
    {
      int r = -errno;
      if (r == -EINTR) {
        continue;
      }
      return;
    }
    break;
  }
}

int SyncFile::open(int flag, std::string temp_name) {
  if(temp_name.empty()) {
    m_fd = :: open(m_name.c_str(), flag, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  } else {
    m_fd = :: open(temp_name.c_str(), flag, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  }

  if(m_fd == -1) {
    return -errno;
  }
  return m_fd;
}

int SyncFile::rename(std::string old_name, std::string new_name) {
  int ret;
  ret = ::rename(old_name.c_str(), new_name.c_str());
  
  if(ret != 0) {
    ldout(cct, 20) <<" rename system call file, error : "<< strerror(errno)<<dendl;
  } 
  
  return ret;
}

/*
 * remove() deletes a name from the filesystem.  It calls unlink for files, and rmdir for directories.
 *
 * If the removed name was the last link to a file and no processes have the file open, 
 *  the file is deleted and the space it was using is made available for reuse.
 * 
 * If the name was the last link to a file, but any processes still have the file open, 
 *  the file will remain in existence until the last file descriptor referring to it is closed.
 *
 */
int SyncFile::remove(std::string delete_file_name) { 
  int ret;
  ret = remove(delete_file_name.c_str()); 
  if(ret < 0) {
    ldout(cct, 20) <<" remove, error : "<< strerror(errno)<<dendl;
  } 

  return ret;
}

void SyncFile::read(uint64_t offset, uint64_t length, ceph::bufferlist *bl, Context *on_finish) {
  on_finish->complete(read_object_from_file(bl, offset, length));
}

void SyncFile::write(uint64_t offset, ceph::bufferlist &&bl, bool fdatasync, Context *on_finish) {
  on_finish->complete(write_object_to_file(bl, bl.length()));
}

int SyncFile::write_object_to_file(ceph::bufferlist read_buf, uint64_t object_len) {

  ldout(cct, 20) << "cache file name:" << m_name
                 << ", length:" << object_len <<  dendl;

  // TODO(): aio
  int ret = pwrite(m_fd, read_buf.c_str(), object_len, 0); 
  if(ret < 0) {
    lderr(cct)<<"write file fail:" << std::strerror(errno) << dendl;
    return ret;
  }

  return ret;
}

int SyncFile::read_object_from_file(ceph::bufferlist* read_buf, uint64_t object_off, uint64_t object_len) {

  ldout(cct, 20) << "offset:" << object_off
                 << ", length:" << object_len <<  dendl;

  bufferptr buf(object_len);

  // TODO(): aio
  int ret = pread(m_fd, buf.c_str(), object_len, object_off); 
  if(ret < 0) {
    lderr(cct)<<"read file fail:" << std::strerror(errno) << dendl;
    return ret;
  }
  read_buf->append(std::move(buf));

  return ret;
}

// =========== SyncFile's child class ==============


ReadSyncFileRequest::ReadSyncFileRequest(CephContext* cct, 
                                         ContextWQ* op_work_queue, 
                                         const std::string& file_name, 
                                         ceph::bufferlist* read_bl, 
                                         const uint64_t object_off, 
                                         const uint64_t object_len, 
                                         Context* on_finish)
  : m_sync_file(cct, file_name),
    m_op_work_queue(op_work_queue),
    m_read_bl(read_bl),
    m_object_offset(object_off),
    m_on_finish(on_finish),
    m_object_len(object_len)
{}

void ReadSyncFileRequest::finish(int r) {

  // no such r<0 case 
  if(r < 0) {
    m_on_finish->complete(-1);
    return;
  }

  int ret;
  int flag = O_RDONLY | O_DIRECT | O_SYNC | O_NOATIME;

  ret = m_sync_file.open(flag, "");
  // no such file
  if(ret == -ENOENT) {
    m_on_finish->complete(-1); 
    return;
  }
 
  // TODO error handing
  if(ret < 0) {
    m_on_finish->complete(-1);
    return;
  }
  
  ret = m_sync_file.read_object_from_file(m_read_bl, m_object_offset, m_object_len);
  if(ret < 0) {
    m_on_finish->complete(-1);
    return;
  }
     
  m_on_finish->complete(0); 
}

WriteSyncFileRequest::WriteSyncFileRequest(CephContext* cct, 
                                           const std::string& file_name, 
                                           ceph::bufferlist& write_bl, 
                                           uint64_t object_off, 
                                           uint64_t object_len,
                                           Context* on_finish)
  : m_sync_file(cct, file_name),
    m_write_bl(write_bl),
    m_object_offset(object_off),
    m_object_len(object_len),
    m_on_finish(on_finish)
{}


 void WriteSyncFileRequest::finish(int r) {
  if(r < 0) {
    m_on_finish->complete(-1);
  }

  int ret;

  std::string temp_name = m_sync_file.get_ceph_context()->_conf.get_val<std::string>("rbd_shared_cache_path") + "/rbd_cache." 
                                + m_sync_file.get_file_name() + "write_temp_file";

  int flag = O_WRONLY | O_CREAT | O_DIRECT | O_SYNC | O_NOATIME;
  ret = m_sync_file.open(flag, temp_name); 
  if(ret < 0) {
    m_on_finish->complete(-1); 
    return;
  }

  ret = m_sync_file.write_object_to_file(m_write_bl, m_object_len);
  if(ret < 0) {
    m_on_finish->complete(-1);
    return;
  }

  
  ret = m_sync_file.rename(temp_name, m_sync_file.get_file_name());
  if(ret < 0) {
    m_on_finish->complete(-1);
    return;
  }
  
  m_on_finish->complete(0);
    
}


DeleteSyncFileRequest::DeleteSyncFileRequest(CephContext* cct, 
                                             ContextWQ* op_work_queue,
                                             const std::string& file_name,
                                             Context* on_finish) 
  : m_sync_file(cct, file_name),
    m_on_finish(on_finish),
    m_op_work_queue(op_work_queue)
{}

void DeleteSyncFileRequest::finish(int r) {
  if(r < 0) {
    m_on_finish->complete(-1);
    return;
  }

  int ret;
  std::string temp_name = m_sync_file.get_file_name() + "delte_temp_file";
  ret = m_sync_file.rename(m_sync_file.get_file_name(), temp_name); 
  if(ret < 0) {
    m_on_finish->complete(-1);
    return;
  }
  m_sync_file.remove(temp_name);
  m_on_finish->complete(0);

}



} // namespace cache
} // namespace librbd
