// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

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

#include "cache_table.hpp"

using librados::Rados;
using librados::IoCtx;

typedef shared_ptr<librados::Rados> RadosRef;
typedef shared_ptr<librados::IoCtx> IoCtxRef;

// TODO execute evict_thread_function, ceph thread or new thread ??
//
class ObjectCacheStore 
{
  public:
    ObjectCacheStore(CephContext *cct, ContextWQ* work_queue);
    ~ObjectCacheStore();

    int init(bool init_way);

    int shutdown();

    int lookup_object(std::string pool_name, std::string object_name);

    // TODO
    int read_object_from_cache(std::string pool_name, std::string object_name)
    {}

    int init_cache(std::string vol_name, uint64_t vol_size);

    int lock_cache(std::string vol_name);

    void evict_thread_function();

  private:
    int _evict_object();

    int do_promote(std::string pool_name, std::string object_name);

    int promote_object(librados::IoCtx*, std::string object_name,
                       librados::bufferlist read_buf,
                       uint64_t length);


    CephContext *m_cct;
    RadosRef m_rados;
    ContextWQ* m_work_queue;

    CacheTable m_cache_table;
    Mutex m_cache_table_lock;

    std::map<std::string, librados::IoCtx*> m_ioctxs;

    os::CacheStore::SyncFile *m_cache_file;
     
    // TODO
    //bool evict_open; 

};

#endif
