// Copyright [2016] <Intel>

#ifndef SCACHE_SIMPLEBLOCKCACHER_H_
#define SCACHE_SIMPLEBLOCKCACHER_H_

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <mutex>
#include <unordered_map>
#include <utility>


#define log_print (void)
//#define log_err (void)
#define log_err printf

#define RW_READ 0
#define RW_WRITE 1
#define RW_DELETE 2

namespace librbd {
namespace cache {

typedef std::unordered_map<uint64_t, uint64_t> BLOCK_INDEX;

class SimpleBlockCacher{
 public:
        SimpleBlockCacher(const char* device_name, uint64_t cache_total_size,
                          uint64_t object_size);
        ~SimpleBlockCacher();

        bool init();
        int remove(uint64_t cache_name);
        int write(uint64_t cache_name, const char *buf, uint64_t offset,
                  uint64_t length);
        ssize_t read(uint64_t cache_name, char *buf, uint64_t offset,
                     uint64_t length);

 private:
    struct free_node{
        uint64_t index;
        free_node* next;
        free_node(){
            next = nullptr;
        }
    };

    BLOCK_INDEX m_meta_tbl;
    std::mutex m_meta_tbl_lock;

    // create two type of data to index free cache item
    // put evict node to free_node_head
    free_node* p_free_node_head;
    free_node* p_free_node_tail;


    const char* p_device_name;
    uint64_t m_object_size;
    uint64_t m_total_size;

    int m_device_fd;

    int open(const char* device_name);
    int close(int block_fd);
    int64_t write_lookup(uint64_t cache_name, bool no_update);
    int64_t read_lookup(uint64_t cache_name);
    int64_t free_lookup();
    bool update_meta(uint64_t cache_name, uint64_t ondisk_off);
    int update_meta_tbl(const typename BLOCK_INDEX::iterator it,
                        uint64_t free_off);
    uint64_t get_block_index(uint64_t* index, uint64_t offset);
};
}  //  namespace cache
}  //  namespace dslab

#endif  // SCACHE_SIMPLEBLOCKCACHER_H_
