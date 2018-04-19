#include "os/CacheStore/SyncFile.h"
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
#define dout_prefix *_dout << "librbd::file::SyncFile: " << this << " " \
                           <<  __func__ << ": "

namespace os {
namespace CacheStore {

SyncFile::SyncFile(CephContext *cct, const std::string &name)
  : cct(cct)
{
  m_name = cct->_conf->get_val<std::string>("rbd_shared_cache_path") + "/rbd_cache." + name;
  ldout(cct, 20) << "file path=" << m_name << dendl;
}

SyncFile::~SyncFile() 
{
  // TODO force proper cleanup
  if (m_fd != -1) {
    ::close(m_fd);
  }
}

void SyncFile::open(Context *on_finish) 
{
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

void SyncFile::open() 
{
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

bool SyncFile::try_open() 
{
  m_fd = ::open(m_name.c_str(), O_DIRECT | O_NOATIME | O_RDWR | O_SYNC,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

  if (m_fd < 0) {
    ::close(m_fd);
    return false;
  }
  ::close(m_fd);
  return true;
}

void SyncFile::close(Context *on_finish) 
{
  assert(m_fd >= 0);
  while (true) 
  {
    int r = ::close(m_fd);
    if (r == -1) {
      r = -errno;
      if (r == -EINTR) {
        continue;
      }
      on_finish->complete(r);
      return;
    }
    break;
  }

  m_fd = -1;
  on_finish->complete(0);
}

void SyncFile::remove(Context *on_finish) 
{
  on_finish->complete(remove());
}

/*
void SyncFile::write_file(){
}

void SyncFile::read_file(){
}
*/

void SyncFile::read(uint64_t offset, uint64_t length, ceph::bufferlist *bl, Context *on_finish) 
{
  on_finish->complete(read(offset, length, bl));
}

void SyncFile::write(uint64_t offset, ceph::bufferlist &&bl, bool fdatasync, Context *on_finish) 
{
  on_finish->complete(write(offset, bl, fdatasync));
  //on_finish->complete(0);
}

void SyncFile::discard(uint64_t offset, uint64_t length, bool fdatasync, Context *on_finish) 
{
  on_finish->complete(discard(offset, length, fdatasync));
}

void SyncFile::truncate(uint64_t length, bool fdatasync, Context *on_finish) 
{
  on_finish->complete(truncate(length, fdatasync));
}

void SyncFile::fsync(Context *on_finish) 
{
  int r = ::fsync(m_fd);
  if (r == -1) {
    r = -errno;
    on_finish->complete(r);
    return;
  }
  on_finish->complete(0);
}

void SyncFile::fdatasync(Context *on_finish) {
  on_finish->complete(fdatasync());
}

int SyncFile::write(uint64_t offset, const ceph::bufferlist &bl,
                      bool sync) {
  sync = false;
  ldout(cct, 20) << "offset=" << offset << ", "
                 << "length=" << bl.length() << dendl;

  //int r = bl.write_fd(m_fd, offset);
  //todo: bl is not aligned in one 4096 block
  char *aligned_buffer = (char*)aligned_alloc(4096, sizeof(char)*4096);
  int r;
  /*r = posix_memalign((void**)&aligned_buffer, sizeof(char)*4096, 1);
  if( r < 0 ){
    return -r;
  }*/
  bl.copy( 0, bl.length(), aligned_buffer );
  r = pwrite(m_fd, aligned_buffer, 4096, offset); 
  if (r < 0) {
    free(aligned_buffer);
    return -r;
  }

  if (sync) {
    r = fdatasync();
  }
  free(aligned_buffer);
  return r;
}

int SyncFile::read(uint64_t offset, uint64_t length, ceph::bufferlist *bl) {

  bufferptr bp = buffer::create(length);
  bl->push_back(bp);

  int r = 0;
  char *buffer = reinterpret_cast<char *>(bp.c_str());
  char *aligned_buffer = (char*)aligned_alloc(4096, sizeof(char)*4096);
  /*char *aligned_buffer;
  r = posix_memalign((void**)&aligned_buffer, sizeof(char)*4096, 1);
  if( r < 0 ){
    return -r;
  }*/
  uint64_t left = length;
  ssize_t ret_val;
  uint64_t count = 0;
  uint64_t cpy_length;
  do {
    //todo: offset is not aligned.
    ret_val = pread64(m_fd, aligned_buffer, 4096, offset);
    if( ret_val < 0 ){
      r = -ret_val;
      if (r == -EINTR) {
        continue;
      }
      free(aligned_buffer);
      return r;
    }
    //ret_val = pread64(m_fd, buffer, length - count, offset + count);
    cpy_length = left <= 4096 ? left : 4096;
    memcpy(buffer + count, aligned_buffer, cpy_length);
    count += cpy_length;
    left -= cpy_length;
  } while ( left );
  free(aligned_buffer);
  return count;
}

int SyncFile::discard(uint64_t offset, uint64_t length, bool sync) {
  ldout(cct, 20) << "offset=" << offset << ", "
                 << "length=" << length << dendl;

  int r;
  while (true) {
    r = fallocate(m_fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                  offset, length);
    if (r == -1) {
      r = -errno;
      if (r == -EINTR) {
        continue;
      }
      return r;
    }
    break;
  }

  if (sync) {
    r = fdatasync();
  }
  return r;
}

int SyncFile::truncate(uint64_t length, bool sync) {
  ldout(cct, 20) << "length=" << length << dendl;

  int r;
  while (true) {
    r = ftruncate(m_fd, length);
    if (r == -1) {
      r = -errno;
      if (r == -EINTR) {
        continue;
      }
      return r;
    }
    break;
  }

  if (sync) {
    r = fdatasync();
  }
  return r;
}

int SyncFile::fdatasync() 
{
  ldout(cct, 20) << dendl;

  int r = ::fdatasync(m_fd);
  if (r == -1) {
    r = -errno;
    return r;
  }
  return 0;
}

uint64_t SyncFile::filesize() 
{
  ldout(cct, 20) << dendl;

  struct stat file_st;
  memset(&file_st, 0, sizeof(file_st));
  fstat(m_fd, &file_st);
  return file_st.st_size;
}

int SyncFile::load(void** dest, uint64_t size) 
{
  ldout(cct, 20) << dendl;
  if (size > filesize()) {
    truncate(size, true);
  }

  void* mmappedData = ::mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, m_fd, 0);
  if (mmappedData != MAP_FAILED) {
    *dest = mmappedData;
    return 0;
  } else {
    return -1;
  }
}

int SyncFile::remove() {
  ldout(cct, 20) << m_name << dendl;
  return ::remove(m_name.c_str());
}

int SyncFile::write_object_to_file(ceph::bufferlist read_buf, uint64_t object_len) {
  int ret;

  // TODO
  ret = pwrite(m_fd, read_buf.c_str(), object_len, 0); 
  if(ret < 0) {
    lderr(cct)<<"write file fail:" << std::strerror(errno) << dendl;
    return -1;
  }

  return ret;
}

int SyncFile::read_object_from_file(ceph::bufferlist* read_buf, uint64_t object_off, uint64_t object_len) {
  int ret;

  // TODO
  ret = pread(m_fd, read_buf, object_off, object_len); 
  if(ret < 0) {
    lderr(cct)<<"read file fail:" << std::strerror(errno) << dendl;
    return -1;
  }

  return ret;
}

} // namespace CacheStore
} // namespace os
