#include "ObjectCacheStore.hpp"

int ObjectCacheStore::init(bool init_way) {
  int ret = m_rados->init_with_context(m_cct);
  if(ret < 0) {
    std::cerr<<"failed to init with ceph context"<<std::endl;
    return ret;
  }

  ret = m_rados->connect();
  if(ret < 0 ) {
    std::cerr<< "radon connect to cluster, failed"<<std::endl;
    return ret;
  }

  return ret;
}
 

int ObjectCacheStore::lookup_object(std::string pool_name, std::string object_name) {
  int ret; 

  std::string cache_file_name =  "/tmp/" + pool_name + object_name;

  // hit
  auto it = cache_table.find(cache_file_name);
  if(it != cache_table.end()) {
    // when this object is promoting, directly return error code.
    if (it->second == PROMOTING) {
      return -1;
    } else if(it->second == PROMOTED){
      return 0;
    } else {
      assert(0);
    }
  }

  std::cout << "start to promote" << std::endl;
  // check io_ctx for this pool.
  if(m_ioctxs.find(pool_name) == m_ioctxs.end()) {
    librados::IoCtx* io_ctx = new librados::IoCtx();
    ret = m_rados->ioctx_create(pool_name.c_str(), *io_ctx);
    std::cout << "creat ioctx" << pool_name << std::endl;
    if(ret < 0) {
      std::cerr<<"rados create io_ctx, failed " << std::endl;
      assert(0);
    }
    m_ioctxs.insert(std::pair<std::string, librados::IoCtx*>(pool_name, io_ctx)); 
  }

  assert(m_ioctxs.find(pool_name) != m_ioctxs.end());
  
  // get current pool's io_ctx
  librados::IoCtx* ioctx = m_ioctxs[pool_name]; 

  // miss and have free space
  // update cache metadata 
  cache_table_lock.lock();
  cache_table.insert(std::pair<std::string, int>(cache_file_name, PROMOTING));
  cache_table_lock.unlock();

  // promote data to memory space
  librados::bufferlist read_buf;      
  int object_size = 4096*1024; // fix size ??? TODO
  ret = promote_object(ioctx, object_name, read_buf, object_size);
  if( ret < 0) {
    std::cerr<<"rados read, failed " << std::endl;
    return -2;
  }

  std::cout << "rados read" << std::endl;
  // persistent into ssd.
  os::CacheStore::SyncFile cache_file(m_cct, *m_work_queue, cache_file_name);
  cache_file.open();
  cache_file.write_object_to_file(read_buf, object_size);
  
  assert( cache_table.find(cache_file_name) != cache_table.end()); 

  // update metadata info
  cache_table_lock.lock();
  cache_table[cache_file_name] = PROMOTED;
  cache_table_lock.unlock();

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
   

int ObjectCacheStore::promote_object(librados::IoCtx* ioctx, 
                                      std::string object_name, 
                                      librados::bufferlist read_buf,
                                      uint64_t read_len)
{
  int ret;

  librados::AioCompletion* read_completion = librados::Rados::aio_create_completion(); 

  ret = ioctx->aio_read(object_name, read_completion, &read_buf, read_len, 0);
  if(ret < 0) {
    std::cerr<<"rados read object, failed."<<std::endl;
  }
  read_completion->wait_for_complete();
  ret = read_completion->get_return_value();
  std::cout << "rados get return" << ret << std::endl;
  return ret;
  
}


/*
int ObjectCacheStore::read_object(std::string pool_name, std::string object_name,
                                  librados::bufferlist &read_buf){
  int ret;
  std::string cache_file_name = pool_name + object_name;

  assert(cache_table.find(cache_file_name) != cache_table.end());

}
*/
