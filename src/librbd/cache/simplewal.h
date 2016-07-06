#ifndef SIMPLE_WAL_H
#define SIMPLE_WAL_H

#include <fcntl.h>
#include <string>
#include <stdint.h>
#include <mutex>

#include <boost/unordered_map.hpp>

namespace librbd {
namespace cache {

class simplewal{
public:
    typedef boost::unordered::unordered_map<uint64_t, uint64_t> BlockIndex;
    struct free_node{
        uint64_t index;
        free_node* next;
        free_node(){
            next = NULL;
        }
    };
    std::string device_name;

    simplewal( std::string device_name, uint64_t cache_total_size, uint64_t object_size );
    ~simplewal();

    int remove( uint64_t cache_name );
    int write(  uint64_t cache_name, const char *buf, uint64_t offset, uint64_t length );
    ssize_t read(  uint64_t cache_name, char *buf, uint64_t offset, uint64_t length );

private:
    BlockIndex cache_index;
    std::mutex cache_index_lock;

    // create two type of data to index free cache item
    // put evict node to free_node_head
    free_node* free_node_head;
    free_node* free_node_tail;

    uint64_t cache_index_size;
    uint64_t object_size;

    int device_fd;

    int open(const char* device_name);
    int open(std::string device_name);
    int close(int block_fd);
    int64_t write_index_lookup( uint64_t cache_name );
    int64_t read_index_lookup( uint64_t cache_name );
    int64_t index_insert( uint64_t cache_name, uint64_t free_off );
    int64_t free_lookup();
    int update_cache_index( const typename BlockIndex::iterator it, uint64_t );
    uint64_t get_block_index( uint64_t* index, uint64_t offset );
};
}
}



#endif
