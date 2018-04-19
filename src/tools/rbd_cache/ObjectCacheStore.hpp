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
        m_rados(new librados::Rados()) {}

    ~ObjectCacheStore();

    int init(bool init_way);

    int lookup_object(std::string pool_name, std::string object_name);

    int shutdown();

    // read interface ..
    // TODO
    //int read_object(librados::bufferlist &buf);
    int init_cache(std::string vol_name, uint64_t vol_size);
    int lock_cache(std::string vol_name);

  private:
    int _evict_object();

    int promote_object(librados::IoCtx*, std::string object_name,
                       librados::bufferlist read_buf,
                       uint64_t length);

  private:

    enum {
      PROMOTING, 
      PROMOTED, 
    };

    // (pool_name + object_name ) --> (cache file status)
    std::map<std::string, int> cache_table;
    std::mutex cache_table_lock;
    
    // pool_name --> ioctx
    std::map<std::string, librados::IoCtx*> m_ioctxs;

    uint32_t block_num; 
    uint32_t block_size;


    CephContext *m_cct;

    RadosRef m_rados;

    ContextWQ* m_work_queue;

    os::CacheStore::SyncFile *m_cache_file;
};


#endif
