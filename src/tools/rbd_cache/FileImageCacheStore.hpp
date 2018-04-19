#ifndef FILE_IMAGE_CACHE_STORE_H
#define FILE_IMAGE_CACHE_STORE_H

#include "common/debug.h"
#include "common/errno.h"
#include "common/ceph_context.h"
#include "common/Mutex.h"
#include "include/rados/librados.hpp"
#include "include/rbd/librbd.h"
#include "librbd/ImageCtx.h"
#include "librbd/ImageState.h"
#include "os/CacheStore/SyncFile.h"
#include "imagecache/ImageStore.h"
#include "imagecache/MetaStore.h"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_rbd_cache
#undef dout_prefix
#define dout_prefix *_dout << "rbd::cache::FileImageCacheStore: " << this << " " \
                           << __func__ << ": "


using librados::Rados;
using librados::IoCtx;

typedef shared_ptr<librados::Rados> RadosRef;
typedef shared_ptr<librados::IoCtx> IoCtxRef;

static const uint32_t BLOCK_SIZE = 4096;

class FileImageCache {
 public:
  FileImageCache(CephContext *cct, ContextWQ *work_queue, std::string vol_name, uint64_t vol_size) {
    m_cct = cct;
    m_work_queue = work_queue;
    m_name = vol_name;
    m_size = vol_size;
  }

  ~FileImageCache() {
    Context *ctx = new FunctionContext(
      [this](int r){}
    );

    m_cache_file->close(ctx);
    delete m_cache_file;
  }

  int init(Context *on_finish) {

/*
    //TODO: init imagestore and metastore instead of syncfile
    m_cache_file = new os::CacheStore::SyncFile(m_cct, *m_work_queue, m_name);
    Context *ctx = new FunctionContext(
      [this, on_finish](int r) {
        if (r < 0) {
          on_finish->complete(r);
          return;
        }

        m_cache_file->truncate(m_size, false, on_finish);
      });

    m_cache_file->open(ctx);
*/

    m_image_store = new rbd::cache::file::ImageStore(m_cct, m_name, m_size, m_work_queue); 
    m_meta_store = new rbd::cache::file::MetaStore(m_cct, m_name, m_size/BLOCK_SIZE, m_work_queue);


    Context *ctx = new FunctionContext(
      [this, on_finish, ctx] (int r) {
        if (r < 0) {
          on_finish->complete(r);
          return;
        } else {
          m_image_store->init(on_finish);
        }
      });

    m_meta_store->init(ctx);
    return 0;
  }

  int write(uint64_t offset, ceph::bufferlist &&bl, bool fdatasync, Context *on_finish) {

    m_cache_file->write(offset, std::move(bl), true, on_finish);
    return 0;
  }

 private:
  CephContext *m_cct;
  ContextWQ *m_work_queue;
  std::string m_name;
  uint64_t m_size;
  rbd::cache::file::ImageStore *m_image_store = nullptr;
  rbd::cache::file::MetaStore *m_meta_store = nullptr;

  os::CacheStore::SyncFile *m_cache_file = nullptr;
  std::mutex lck;

};


class FileImageCacheStore {
  public:
    FileImageCacheStore(CephContext *cct, ContextWQ* work_queue): m_cct(cct), m_work_queue(work_queue), m_rados(new librados::Rados()){}
    ~FileImageCacheStore();
    int init();
    int init_cache(std::string vol_name, uint64_t vol_size);
    int lock_cache(std::string vol_name);
    int shutdown();
    int open_pool(std::string pool_name);
    int promote_object(std::string pool_name, std::string vol_name, std::string object_name, uint64_t offset, uint64_t length);

  private:
    uint32_t block_size;

    CephContext *m_cct;
    ContextWQ* m_work_queue;
    RadosRef m_rados;

    librados::IoCtx m_ioctx;

    FileImageCache *m_image_cache;
    std::map<std::string, FileImageCache*> volume_map;
};

int FileImageCacheStore::init() {
  int r = m_rados->init_with_context(m_cct);
  if (r < 0) {
    std::cout << "could not initialize rados handle\n";
    return r;
  }

  r = m_rados->connect();
  if (r < 0) {
    //derr << "error connecting to local cluster" << dendl;
    return r;
  }

  return r;
}

int FileImageCacheStore::init_cache(std::string vol_name, uint64_t vol_size) {

  //TODO(): chain call back 
  Context *ctx = new FunctionContext(
    [this](int r){}
  );

  m_image_cache = new FileImageCache(m_cct, m_work_queue, vol_name, vol_size);
  m_image_cache->init(ctx);

  volume_map[vol_name] = m_image_cache;

  return 0;
}

int FileImageCacheStore::lock_cache(std::string vol_name) {


  m_image_cache = volume_map[vol_name];
  //m_image_cache->lck.lock();

  return 0;
}

int FileImageCacheStore::shutdown() {
  int r = 0;
  for(auto &it: volume_map) {
    delete it.second;
  }
  m_rados->shutdown();
  return r;
}

int FileImageCacheStore::open_pool(std::string pool_name) {
  int r = m_rados->ioctx_create(pool_name.c_str(), m_ioctx);

  return r;
}

int FileImageCacheStore::promote_object(std::string pool_name, std::string vol_name, std::string object_name, uint64_t offset, uint64_t length) {


  librados::bufferlist read_buf;
  //TODO(): promote on length
  int read_len = 4194304; 

  librados::AioCompletion *read_completion = librados::Rados::aio_create_completion();
  int ret = m_ioctx.aio_read(object_name, read_completion, &read_buf, read_len, 0);
  if (ret < 0) {
    std::cerr << "couldn't start read object! error " << ret << std::endl;
    return ret;
  }

  read_completion->wait_for_complete();
  ret = read_completion->get_return_value();
  if (ret < 0) {
    std::cerr << "couldn't read object! error " << ret << std::endl;
    return ret;
  }

  //TODO(): async call back 
  Context *ctx = new FunctionContext(
    [this](int r){}
  );
  
  auto it = volume_map.find(vol_name);
  if (it != volume_map.end()) {
    it->second->write(offset, std::move(read_buf), true, ctx);
  }
  
  return 0;
}

#endif
