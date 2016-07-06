#include "simplewal.h"

#if 0
  #define log_err printf 
  #define log_print printf
#else
  #define log_err (void) 
  #define log_print (void)
#endif

namespace librbd {
namespace cache {

simplewal::simplewal( std::string device_name, uint64_t cache_total_size, uint64_t p_object_size ){
    device_name = device_name;
    object_size = p_object_size;
    cache_index_size = cache_total_size/object_size;
    free_node_head = (free_node*)malloc(sizeof(free_node));

    free_node* tmp = free_node_head;
    free_node* cur_tmp = tmp;
    for(uint64_t i = 0 ; i < cache_index_size; i++ ){
        tmp->index = i;
        free_node* next_tmp = (free_node*)malloc(sizeof(free_node));
        tmp->next = next_tmp;
        cur_tmp = tmp;
        tmp = tmp->next;
    }
    cur_tmp->next = NULL;
    free_node_tail = cur_tmp;

    device_fd = open( device_name );
}

simplewal::~simplewal(){
    close( device_fd );
}

int simplewal::open( std::string device_name ){
    int mode = O_RDWR | O_SYNC;
    int fd = ::open( device_name.c_str(), mode );
    if ( fd <= 0 ){
        log_err( "[ERROR] simplewal::simplewal, unable to open %s, error code: %d ", device_name.c_str(), fd );
        return fd;
    }
    return fd;
}

int64_t simplewal::read_index_lookup( uint64_t cache_name ){
    int64_t off;
    cache_index_lock.lock();
    const typename BlockIndex::iterator it = cache_index.find( cache_name );
    if( it == cache_index.end() ){
        log_err("simplewal::read_index_lookup_can't find cache_name=%lu\n", cache_name);
        off = -1;
    }else{
        off = it->second;
    }
    cache_index_lock.unlock();
    return off;
}

int64_t simplewal::write_index_lookup( uint64_t cache_name ){
    cache_index_lock.lock();
    const typename BlockIndex::iterator it = cache_index.find( cache_name );
    int64_t free_off;
    free_off = free_lookup();
    if( free_off < 0 ){
        cache_index_lock.unlock();
        return free_off;
    }
    if( it != cache_index.end() ){
        update_cache_index( it, free_off );
    }else{
        index_insert( cache_name, free_off );
    }
    cache_index_lock.unlock();
    return free_off;
}

int64_t simplewal::index_insert( uint64_t cache_name, uint64_t free_off ){
    cache_index.insert( std::make_pair( cache_name, free_off ) );
    return free_off;
}

int64_t simplewal::free_lookup(){
    int64_t free_off;
    if ( free_node_head != NULL ){
        free_node* this_node = free_node_head;
        free_off = this_node->index;
        free_node_head = this_node->next;
        free(this_node);
        return free_off;
    }else{
        log_print("[ERROR] simplewal::free_lookup can't find free node\n");
        return -1;
    }
}

int simplewal::remove( uint64_t cache_name ){
    int ret = 0;
    cache_index_lock.lock();
    const typename BlockIndex::iterator it = cache_index.find( cache_name );
    if( it != cache_index.end() ){

        uint64_t block_id = it->second;
        free_node* new_free_node = (free_node*)malloc(sizeof(free_node));
        new_free_node->index = block_id;
        new_free_node->next = NULL;
        free_node_tail->next = new_free_node;
        free_node_tail = new_free_node;

        cache_index.erase( it );
    }
    cache_index_lock.unlock();
    return ret;
}

int simplewal::update_cache_index( const typename BlockIndex::iterator it, uint64_t free_off ){
    uint64_t block_id = it->second;
    free_node* new_free_node = (free_node*)malloc(sizeof(free_node));
    new_free_node->index = block_id;
    new_free_node->next = NULL;
    free_node_tail->next = new_free_node;
    free_node_tail = new_free_node;

    it->second = free_off;
    return 0;

}

int simplewal::close( int block_fd ){
    int ret = ::close(block_fd);
    if(ret < 0){
        log_err( "[ERROR] simplewal: close block_fd failed\r\n" );
        return -1;
    }
    return 0;

}

uint64_t simplewal::get_block_index( uint64_t* index, uint64_t offset ){
  *index = offset / object_size;
  return offset % object_size;
}

int simplewal::write( uint64_t cache_name, const char *buf, uint64_t offset, uint64_t length ){
    int64_t block_id = write_index_lookup( cache_name );

    if(block_id < 0)
        return -1;

    uint64_t index = 0;
    uint64_t off_by_block = get_block_index( &index, offset );
    int64_t left = off_by_block + length;
    int ret;
    char* alignedBuff;
    uint64_t write_len;

    // handle partial write
    while( left > 0 ){
        alignedBuff = (char*) malloc(object_size);
        write_len = object_size;
        if( off_by_block > 0 || left < object_size ){
            ssize_t ret = ::pread( device_fd, alignedBuff, object_size, block_id * object_size + index * object_size );
            if(ret < 0){
                log_err( "[ERROR] simplewal::write_fd, unable to read data from index: %lu\n", block_id * object_size + index * object_size );
                return -1;
            }
            if( off_by_block )
                write_len -= off_by_block;
            if( left < object_size )
                write_len -= ( object_size - left );
        }
        memcpy( alignedBuff+off_by_block, buf, write_len );

        ret = ::pwrite( device_fd, alignedBuff, object_size, block_id * object_size + index * object_size );
        if ( ret < 0 ){
            log_err( "[ERROR] simplewal::write_fd, unable to write data, block_id: %lu\n", block_id );
            return -1;
        }
        free(alignedBuff);

        left -= object_size;
        off_by_block = 0;
        index++;
    }
    return 0;
}

ssize_t simplewal::read( uint64_t cache_name, char *buf, uint64_t offset, uint64_t length ){
    uint64_t index = 0;
    int64_t block_id = read_index_lookup( cache_name );
    if(block_id < 0)
        return -1;
    uint64_t off_by_block = get_block_index( &index, offset );
    int64_t left = off_by_block + length;
    char *alignedBuff;

    while(left > 0){
        if( off_by_block )
            alignedBuff = (char*) malloc(object_size);
        else
            alignedBuff = &buf[off_by_block + length - left];

        ssize_t ret = ::pread( device_fd, alignedBuff, object_size, block_id * object_size + index * object_size );
        if ( ret < 0 ){
            log_err( "[ERROR] simplewal::read_fd, unable to read data, error code: %zd ", ret );
            return -1;
        }

        if( off_by_block ){
            memcpy( &buf[off_by_block + length - left], &alignedBuff[off_by_block], object_size );
            free(alignedBuff);
        }

        left -= object_size;
        off_by_block = 0;
        index++;

    }
    return length;
}

}
}
