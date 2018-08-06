// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "ObjectCacheStore.h"
#include "librbd/Utils.h"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_rbd_cache
#undef dout_prefix
#define dout_prefix *_dout << "rbd::cache::ObjectCacheStore: " << this << " " \
                           << __func__ << ": "



namespace rbd {
namespace cache {

using librbd::util::create_context_callback;
using librbd::util::create_rados_callback;

ObjectCacheStore::ObjectCacheStore(CephContext *cct, ContextWQ* work_queue)
      : m_cct(cct), m_work_queue(work_queue),
        m_cache_table_lock("rbd::cache::ObjectCacheStore"),
        m_rados(new librados::Rados()) {
}

ObjectCacheStore::~ObjectCacheStore() {

}

int ObjectCacheStore::init(bool reset) {

  int ret = m_rados->init_with_context(m_cct);
  if(ret < 0) {
    lderr(m_cct) << "fail to init Ceph context" << dendl;
    return ret;
  }

  ret = m_rados->connect();
  if(ret < 0 ) {
    lderr(m_cct) << "fail to conect to cluster" << dendl;
    return ret;
  }
  //TODO(): check existing cache objects
  return ret;
}

int ObjectCacheStore::do_promote(std::string pool_name, std::string object_name, 
                                 ProcessMsg0* on_finish) {
  int ret = 0;
  std::string cache_file_name =  pool_name + object_name;

  if (m_ioctxs.find(pool_name) == m_ioctxs.end()) {
    librados::IoCtx* io_ctx = new librados::IoCtx();
    ret = m_rados->ioctx_create(pool_name.c_str(), *io_ctx);
    if (ret < 0) {
      lderr(m_cct) << "fail to create ioctx" << dendl;
      assert(0);
    }
    m_ioctxs.emplace(pool_name, io_ctx); 
  }

  assert(m_ioctxs.find(pool_name) != m_ioctxs.end());
  
  librados::IoCtx* ioctx = m_ioctxs[pool_name]; 

  //promoting: update metadata 
  {
    Mutex::Locker locker(m_cache_table_lock);
    m_cache_table.emplace(cache_file_name, PROMOTING);
  }

  librados::bufferlist* read_buf = new librados::bufferlist();
  int object_size = 4096*1024; //TODO(): read config from image metadata

  auto promote_callback = make_lambda_process_function(
            [on_finish, this, read_buf, object_size, cache_file_name](int r, std::string temp) {

               if (r == -ENOENT) {
                 read_buf->append(std::string(object_size, '0'));
                 r = 0;
               }

               if( r < 0) {
                 lderr(m_cct) << "fail to read from rados" << dendl;
                 on_finish->process_msg(r, "");
                 return;
               }

               // persistent to cache
               librbd::cache::SyncFile cache_file(m_cct, cache_file_name);
               cache_file.open();
               cache_file.write_object_to_file(*read_buf, object_size);
               
               assert(m_cache_table.find(cache_file_name) != m_cache_table.end()); 

               // update metadata
               {
                 Mutex::Locker locker(m_cache_table_lock);
                 m_cache_table.emplace(cache_file_name, PROMOTED);
               }
                            
                on_finish->process_msg(r, "");
            });

  //TODO(): async promote
  ret = promote_object(ioctx, object_name, read_buf, object_size, promote_callback);

  return 0;

}
 
int ObjectCacheStore::lookup_object(std::string pool_name, std::string object_name, 
                                    ProcessMsg0* on_finish) {

  std::string cache_file_name =  pool_name + object_name;
  {
    Mutex::Locker locker(m_cache_table_lock);

    auto it = m_cache_table.find(cache_file_name);
    if (it != m_cache_table.end()) {

      if (it->second == PROMOTING) {
        on_finish->process_msg(-1, ""); // null -> string 
        return 0;
      } else if (it->second == PROMOTED) {
        on_finish->process_msg(0, ""); 
        return 0;
      } else {
        assert(0);
      }
    }
  }

  do_promote(pool_name, object_name, on_finish);

  return 0;
}

int ObjectCacheStore::shutdown() {
  m_rados->shutdown();
  return 0;
}

int ObjectCacheStore::init_cache(std::string vol_name, uint64_t vol_size) {
  return 0;
}

int ObjectCacheStore::lock_cache(std::string vol_name) {
  return 0;
}

int ObjectCacheStore::promote_object(librados::IoCtx* ioctx, std::string object_name, 
                                     librados::bufferlist* read_buf, uint64_t read_len,
                                     ProcessMsg0* on_finish) {
  int ret;

  // TODO look for other callback implements
  // FIXME FIXME FIXME FIXME
  Context* rados_on_finish = new FunctionContext(
        [this, on_finish](int r) {
            on_finish->process_msg(r, "");
        });

  librados::AioCompletion* read_completion = create_rados_callback(rados_on_finish);

  ret = ioctx->aio_read(object_name, read_completion, read_buf, read_len, 0);

  return ret;
}

} // namespace cache
} // namespace rbd
