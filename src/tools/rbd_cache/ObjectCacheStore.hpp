#ifndef OBJECT_CACHE_STORE_H
#define OBJECT_CACHE_STORE_H

#include "common/debug.h"
#include "common/errno.h"
#include "common/ceph_context.h"
#include "common/Mutex.h"
#include "include/rados/librados.hpp"
#include "include/rbd/librbd.h"
#include "librbd/ImageCtx.h"
#include "librbd/ImageState.h"
#include "os/CacheStore/SyncFile.h"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_rbd_cache
#undef dout_prefix
#define dout_prefix *_dout << "rbd::cache::ObjectCacheStore: " << this << " " \
                           << __func__ << ": "


using librados::Rados;
using librados::IoCtx;

typedef shared_ptr<librados::Rados> RadosRef;
typedef shared_ptr<librados::IoCtx> IoCtxRef;


class ObjectCacheStore {
  public:
    ObjectCacheStore(CephContext *cct, ContextWQ* work_queue): m_cct(cct), m_work_queue(work_queue), m_rados(new librados::Rados()){}
    ~ObjectCacheStore();
    int init();
    int shutdown();
    int open_pool(std::string pool_name, std::string volume_name, std::string snap_name);
    int promote_object(std::string pool_name, std::string object_name, uint64_t offset, uint64_t length);

  private:
    uint32_t block_size;

    CephContext *m_cct;
    ContextWQ* m_work_queue;
    RadosRef m_rados;

    librados::IoCtx m_ioctx;

    os::CacheStore::SyncFile *m_cache_file;
};

int ObjectCacheStore::init() {
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
  //Test
  if (0) {
    r = open_pool("rbd", "testimage", "");
    std::string obj_name = "rbd_data.10226b8b4567.0000000000000000";
    r = promote_object("rbd", obj_name, 0, 1<<20);
  }
  return r;
}

int ObjectCacheStore::shutdown() {
  int r = 0;

  m_rados->shutdown();
  delete m_cache_file;
  return r;
}

int ObjectCacheStore::open_pool(std::string pool_name, std::string volume_name, std::string snap_name) {
  int r = m_rados->ioctx_create(pool_name.c_str(), m_ioctx);

  return r;
}

int ObjectCacheStore::promote_object(std::string pool_name, std::string object_name, uint64_t offset, uint64_t length) {


  librados::bufferlist read_buf;
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
  
  m_cache_file = new os::CacheStore::SyncFile(m_cct, *m_work_queue, pool_name + "." + object_name);
  m_cache_file->open();
  m_cache_file->truncate(read_len, true, ctx);

  ctx = new FunctionContext(
    [this](int r){}
  );
  m_cache_file->write(offset, std::move(read_buf), true, ctx);

  ctx = new FunctionContext(
    [this](int r){}
  );
  m_cache_file->close(ctx);

  return 0;
}

#endif
