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


class ObjectCacheStore 
{
  public:
    ObjectCacheStore(CephContext *cct, ContextWQ* work_queue)
        : m_cct(cct), 
        m_work_queue(work_queue), 
        m_rados(new librados::Rados())
    {}

    ~ObjectCacheStore();

    // TODO
    //
    // 1: new rados 
    // 2: init , init, init_with_conf /init_with_context 
    // 3: connect 
    // 4: ensure pool_name exit.
    // 5: create_ioctx  
    int init(std::string pool_name, bool init_way);

    // TODO
    //
    // 1: lookup object tables to sure hit or miss
    // 2: if hit, return true
    // 3:        miss
    //          /    \
    //         /      \
    //      free     full 
    //       |          \
    //    promotion    evict
    //     /    \      /   \
    //    s      f    f     s
    //    |      |           \
    //  true   false       promotion
    // 
    // return error code :
    // 0 --> successe
    // -1 --> promotion fail
    // -2 --> evict fails
    //
    int lookup_object();

    int shutdown();

  private:
    // 1: according to proxy, select one posiotion.
    // 2: delete this from map.
    // over...
    int _evict_object();

    // 1: call sync_read
    // 2: write cache
    // 3: return  // maybe directly return memory ptr.
    int _promote_object(std::string pool_name, std::string object_name, 
                       uint64_t offset, uint64_t length);

  private:
    // wrapper for rados interface
    //
    int _rados_init(bool is_config_file); // two ways to init
    int _rados_connect();
    int _rados_pool_create(std::string pool_name);
    int _rados_ioctx_create(std::string pool_name);
    //void _rados_sync_write();
    //void _rados_async_write();
    int _rados_sync_read(librados::bufferlist &read_buf, int read_len);
    //void _rados_async_read();

  private:

    // TODO 
    // object map related data structure
    LRU cache_metadata;

    // TODO
    std::list<uint32_t> free_cache_metadata;

    // TODO
    // namely, how to evict data when cache space is full.
    Policy policy_map;
    
    // TODO
    uint32_t block_num; 
    
    uint32_t block_size;

    CephContext *m_cct;
    ContextWQ* m_work_queue; // need this DS to sort IO request ?
    RadosRef m_rados;

    librados::IoCtx m_ioctx;

    os::CacheStore::SyncFile *m_cache_file;
};


int ObjectCacheStore::init() 
{
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

int ObjectCacheStore::shutdown() 
{
  int r = 0;

  m_rados->shutdown();
  delete m_cache_file;
  return r;
}

int ObjectCacheStore::open_pool(std::string pool_name, 
        std::string volume_name, std::string snap_name) 
{
  int r = m_rados->ioctx_create(pool_name.c_str(), m_ioctx);

  return r;
}

int ObjectCacheStore::promote_object(std::string pool_name, 
        std::string object_name, uint64_t offset, uint64_t length) 
{
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
