// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "ObjectCacheStore.h"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_rbd_cache
#undef dout_prefix
#define dout_prefix *_dout << "rbd::cache::ObjectCacheStore: " << this << " " \
                           << __func__ << ": "


ObjectCacheStore::ObjectCacheStore(CephContext *cct, ContextWQ* work_queue)
      : m_cct(cct), 
        m_work_queue(work_queue),
        m_cache_table(1024, 0, 0.9), //  TODO read param from config file...
        m_cache_table_lock("rbd::cache::ObjectCacheStore"),
        m_rados(new librados::Rados()) {
}

ObjectCacheStore::~ObjectCacheStore() {

}

int ObjectCacheStore::init(bool init_way) {
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

int ObjectCacheStore::do_promote(std::string pool_name, std::string object_name) {
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

  // insert new cache file name. 
  assert(m_cache_table.lookup(cache_file_name) == false);
  m_cache_table.insert(cache_file_name);

  librados::bufferlist read_buf;      
  int object_size = 4096*1024; //TODO(): read config from image metadata


  //TODO(): async promote
  
  // update cache file status 
  m_cache_table.set_status(cache_file_name, PROMOTING);
  ret = promote_object(ioctx, object_name, read_buf, object_size);
  if (ret == -ENOENT) {
    read_buf.append(std::string(object_size, '0'));
    ret = 0;
  }

  if( ret < 0) {
    lderr(m_cct) << "fail to read from rados" << dendl;
    return ret;
  }

  // TODO : these work can be placed into CacheFile class. 
  // persistent to cache
  os::CacheStore::SyncFile cache_file(m_cct, cache_file_name);
  cache_file.open();
  ret = cache_file.write_object_to_file(read_buf, object_size);
  
  // update metadata
  m_cache_table.set_status(cache_file_name, PROMOTED);

  return 0;
}
 
int ObjectCacheStore::lookup_object(std::string pool_name, std::string object_name) {

  int ret; 

  std::string cache_file_name =  pool_name + object_name;

  {
    Mutex::Locker locker(m_cache_table_lock);

    bool if_hit;
    std::string file_name_out;
    if_hit = m_cache_table.lookup_and_touch(file_name_out);
    if (if_hit) {
      switch (m_cache_table.get_status(file_name_out)) {
        case PROMOTING:
          return -1;
        case PROMOTED:
          return 0;
        case IN_USING:
          return 0;
        case EVICTING:
          return -1;
        case EVICTED:
          return -1;
      }
    }
  }

   // if miss, promote it.
  do_promote(pool_name, object_name);

  return ret;
}

//
void ObjectCacheStore::evict_thread_function() 
{
  while(1) 
  {
    if(m_cache_table.if_lower_evict_level()) {
       // TODO or sleep , or wakeup ?
      continue;
    }

    std::string cache_file_name_out;
    bool if_obtain = m_cache_table.get_the_lowest_priority_key(cache_file_name_out);
    if(if_obtain) 
    {
      switch(m_cache_table.get_status(cache_file_name_out)) {
        case NONE: 
          // TODO
          break;
        case PROMOTING:
          break;
        case PROMOTED:
          m_cache_table.remove(cache_file_name_out);
          break;
        case IN_USING :
          m_cache_table.touch(cache_file_name_out);
          break;
        case EVICTING: 
          break;
        case EVICTED:
          break;
      }

    } else {
      assert(0);
    }

  } //while
  
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
        librados::bufferlist read_buf, uint64_t read_len) {
  int ret;

  librados::AioCompletion* read_completion = librados::Rados::aio_create_completion(); 

  ret = ioctx->aio_read(object_name, read_completion, &read_buf, read_len, 0);
  if(ret < 0) {
    lderr(m_cct) << "fail to read from rados" << dendl;
  }
  read_completion->wait_for_complete();
  ret = read_completion->get_return_value();
  return ret;
  
}
