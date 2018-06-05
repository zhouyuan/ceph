
#include "ObjectCacheStore.hpp"







int lookup_object(std::string object_name) {
  // hit
  if(cache_metadata.find(object_name) != cache_metadata.end()) {
    // updata cache_metadata.
    return 0;
  }

  // miss and have free space
  if(free_cache_metadata.size() > 0) {
    // get free space
    uint32_t offset = *(free_cache_metadata.front());  

    // promote : cluster data --> memory space
    librados::bufferlist read_buf;      
    int read_len = 4096; // fix size ???
    _promote_object(object_name, read_buf, read_len);

    // promote : memory space --> persistent space.
    m_cache_file.write(offset, std::move(read_buf), true, ctx); 

    // insert this element into metadata.
    // TODO    

  } else if(free_cache_metadata.size() == 0) {
    // miss and don't have any free space.
   
    // firstly, evict.
    _evict_object();
    
    // updata cache_metadata;
  }

  return 0;
}




int ObjectCacheStore::_promote_object(std::string object_name)
{
  int ret;

  librados::bufferlist read_buf;
  int read_len = 4096; // how to set this param ? TODO
  ret = _rados_sync_read(object_name, read_buf, read_len);
  if(ret < 0) {
    std::cerr<<"promote failed."<<std::endl;
    return ret;
  }

}


int ObjectCacheStore::_rados_init(bool is_config_file)
{
  int ret;
  m_rados = new librados::Rados();

  if(0) {
    ret = m_rados->init("admin");
  }

  if(is_config_file) {
    ret = m_rados->conf_read_file("/etc/ceph/ceph.conf");
    if(ret < 0) {
      std::cerr<<"failed to parse config file"<<std::endl;
    }
  } else {
    ret = m_rados->init_with_context(m_cct);
    if(ret < 0) {
      std::cerr<<"failed to init with ceph context"<<std::endl;
  }

  return ret;
}


int ObjectCacheStore::_rados_connect() {
  int ret;
  ret = m_rados->connect();
  if(ret < 0 ) {
    std::cerr<< "radon connect to cluster, failed"<<std::endl;
  }
  return ret;
}


int ObjectCacheStore::_rados_pool_create(std::string pool_name) {
  int ret;
  ret = m_rados->pool_create(pool_name);
  if(ret < 0) {
    std::cerr<<"rados create pool, failed"<<std::endl;
  }

  return ret;
}


int ObjectCacheStore::_rados_ioctx_create(std::string pool_name){
  int ret;
  ret = m_rados.ioctx_create(pool_name, m_ioctx);
  if(ret < 0) {
    std::cerr<<"rados create io_ctx, failed " << std::endl;
  }
  return ret;
}

int ObjectCacheStore::_rados_sync_read(std::string object_name, 
                                        librados::bufferlist &read_buf, 
                                        int read_len) 
{
  int ret;
  librados::AioCompletion* read_completion = librados::Rados::aio_create_completion(); 
  ret = m_ioctx.aio_read(object_name, read_completion, &read_buf, read_len, 0);
  if(ret < 0) {
    std::cerr<<"rados read object, failed."<<std::endl;
  }
  read_completion->wait_for_complete();
  ret = read_completion->get_return_value();
  return ret;
  
}








