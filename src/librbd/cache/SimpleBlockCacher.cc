// Copyright [2016] <Intel>

#include "SimpleBlockCacher.h"

namespace librbd {
namespace cache {
SimpleBlockCacher::SimpleBlockCacher(const char* device_name,
                                     uint64_t cache_total_size,
                                     uint64_t object_size) {
    p_device_name = device_name;
    m_object_size = object_size;
    m_total_size = cache_total_size;
    p_free_node_head = p_free_node_tail = nullptr;
    m_device_fd = -1;
}

SimpleBlockCacher::~SimpleBlockCacher() {
    free_node* tmp = p_free_node_head;
    while (tmp) {
        free_node* tmp_next = tmp->next;
        free(tmp);
        tmp = tmp_next;
    }
    close(m_device_fd);
}

bool SimpleBlockCacher::init() {
    uint64_t free_list_length = m_total_size/m_object_size;

    // TODO(yuan): handle malloc fail
    p_free_node_head = new free_node();
    free_node* tmp = p_free_node_head;
    for (uint64_t i = 0 ; i < free_list_length; i++) {
        tmp->index = i;
        if (i < (free_list_length - 1)) {
            free_node* next_tmp = new free_node();
            tmp->next = next_tmp;
            tmp = tmp->next;
        } else {
            tmp->next = nullptr;
        }
    }
    p_free_node_tail = tmp;
    //assert(p_free_node_tail->next == nullptr);

    m_device_fd = open(p_device_name);

    if (m_device_fd < 0) {
        log_err("[ERROR] SimpleBlockCacher::init, unable to open %s,"
                 "error code: %d ", p_device_name, m_device_fd);
        return false;
    }
    return true;
}

int SimpleBlockCacher::open(const char* device_name) {
    int mode = O_CREAT | O_RDWR | O_SYNC, permission = S_IRUSR | S_IWUSR;
    int fd = ::open(device_name, mode, permission);
    if (fd <= 0) {
        log_err("[ERROR] SimpleBlockCacher::SimpleBlockCacher, "
                 "unable to open %s, error code: %d ", device_name, fd);
        close(fd);
        return fd;
    }

    struct stat file_st;
    memset(&file_st, 0, sizeof(file_st));
    fstat(fd, &file_st);
    if (file_st.st_size < m_total_size) {
        if (-1 == ftruncate(fd, m_total_size)) {
            close(fd);
            return fd;
        }
    }

    return fd;
}

int SimpleBlockCacher::close(int block_fd) {
    int ret = ::close(block_fd);
    if (ret < 0) {
        log_err("[ERROR] SimpleBlockCacher::SimpleBlockCacher, "
                 "close block_fd failed");
        return ret;
    }
    return ret;
}

int64_t SimpleBlockCacher::read_lookup(uint64_t cache_name) {
    m_meta_tbl_lock.lock();
    int64_t ondisk_idx = -1;
    auto it = m_meta_tbl.find(cache_name);
    if (it == m_meta_tbl.end()) {
        log_err("SimpleBlockCacher::read_index_lookup can't find "
                 "cache_name=%lu, ondisk_idx=%ld\n", cache_name, ondisk_idx);
        // TODO(XXX): fix look up failure
        //assert(0);
    } else {
        ondisk_idx = it->second;
    }
    m_meta_tbl_lock.unlock();
    return ondisk_idx;
}

int64_t SimpleBlockCacher::write_lookup(uint64_t cache_name,
                                        bool no_update = false) {
    m_meta_tbl_lock.lock();
    auto it = m_meta_tbl.find(cache_name);

    // when io is small than object_size(let's say 4k here)
    // we should do in-place write
    if (no_update && it!= m_meta_tbl.end()) {
        m_meta_tbl_lock.unlock();
        return it->second;
    }

    int64_t free_idx = free_lookup();

    if (free_idx < 0 && it == m_meta_tbl.end()) {
        // write miss and no free space on cache
        //assert(0);
        m_meta_tbl_lock.unlock();
        return free_idx;
    }

    if (free_idx < 0 && it != m_meta_tbl.end()) {
        // write hit and no free space
        m_meta_tbl_lock.unlock();
        return it->second;
    }

    m_meta_tbl_lock.unlock();
    return free_idx;
}

int64_t SimpleBlockCacher::free_lookup() {
    int64_t free_idx = -1;
    if (p_free_node_head != nullptr) {
        free_node* this_node = p_free_node_head;
        free_idx = this_node->index;
        p_free_node_head = this_node->next;
        free(this_node);
    } else {
        log_print("SimpleBlockCacher::free_lookup can't find free node\n");
    }

    return free_idx;
}

bool SimpleBlockCacher::update_meta(uint64_t cache_name, uint64_t ondisk_idx) {
    m_meta_tbl_lock.lock();
    auto it = m_meta_tbl.find(cache_name);

    if (it != m_meta_tbl.end()) {
         update_meta_tbl(it, ondisk_idx);
    } else {
        m_meta_tbl.insert(std::make_pair(cache_name, ondisk_idx));
    }
    m_meta_tbl_lock.unlock();
    return true;
}

int SimpleBlockCacher::update_meta_tbl(const typename BLOCK_INDEX::iterator it,
                                       uint64_t free_idx) {
    uint64_t block_id = it->second;
    free_node* new_free_node = new free_node();
    new_free_node->index = block_id;
    new_free_node->next = nullptr;
    p_free_node_tail->next = new_free_node;
    p_free_node_tail = new_free_node;

    it->second = free_idx;
    return 0;
}

uint64_t SimpleBlockCacher::get_block_index(uint64_t* index, uint64_t offset) {
  *index = offset / m_object_size;
  return offset % m_object_size;
}

int SimpleBlockCacher::write(uint64_t cache_name, const char *buf,
                             uint64_t offset, uint64_t length) {
    uint64_t block_id;
    if (length < m_object_size)
        block_id = write_lookup(cache_name, true);
    else
        block_id = write_lookup(cache_name);
    if (block_id < 0)
        return -1;
    uint64_t index = 0;
    uint64_t off_by_block = get_block_index(&index, offset);
    int64_t left = off_by_block + length;
    int ret;
    char* alignedBuff;
    uint64_t write_len;
    uint64_t ondisk_idx;

    // handle partial write
    while (left > 0) {
        write_len = left > m_object_size ? \
            (m_object_size-off_by_block):(left-off_by_block);
        if (off_by_block > 0 || left < m_object_size) {
            alignedBuff = reinterpret_cast<char*>(malloc(m_object_size));
            ssize_t ret = ::pread(m_device_fd, alignedBuff, m_object_size,
                block_id * m_object_size + index * m_object_size);
            if (ret < 0) {
                log_err("[ERROR] SimpleBlockCacher::write_fd, "
                        "unable to read data from index: %lu\n",
                        block_id * m_object_size + index * m_object_size);
                free(alignedBuff);
                return -1;
            }
            memcpy(alignedBuff+off_by_block, buf, write_len);
        } else {
            alignedBuff = const_cast<char*>(buf);
        }
        ondisk_idx = block_id * m_object_size + index * m_object_size;
        ret = ::pwrite(m_device_fd, alignedBuff, m_object_size,
                       block_id * m_object_size + index * m_object_size);
        if (ret < 0) {
            log_err("[ERROR] SimpleBlockCacher::write_fd, "
                    "unable to write data, block_id: %lu\n", block_id);
            if (off_by_block > 0 || left < m_object_size) {
                free(alignedBuff);
            }
            return -1;
        }
        update_meta(cache_name, block_id);
        posix_fadvise(m_device_fd,
            block_id * m_object_size + index * m_object_size, length,
            POSIX_FADV_DONTNEED);
        if (off_by_block > 0 || left < m_object_size) {
            free(alignedBuff);
        }

        if (ret < 0) {
            log_err("[ERROR] SimpleBlockCacher::write_fd, "
                    "unable to update metadata, block_id: %lu\n", block_id);
            return -1;
        }

        left -= m_object_size;
        off_by_block = 0;
        index++;
    }
    return 0;
}

ssize_t SimpleBlockCacher::read(uint64_t cache_name, char *buf,
                                uint64_t offset, uint64_t length) {
    uint64_t index = 0;
    int64_t block_id = read_lookup(cache_name);
    if (block_id < 0)
        return -1;
    uint64_t off_by_block = get_block_index(&index, offset);
    int64_t left = off_by_block + length;
    int ret;
    char *alignedBuff;

    while (left > 0) {
        if (off_by_block || left < m_object_size)
            alignedBuff = reinterpret_cast<char*>(malloc(m_object_size));
        else
            alignedBuff = &buf[off_by_block + length - left];

        ssize_t ret = ::pread(m_device_fd, alignedBuff, m_object_size,
                              block_id * m_object_size + index * m_object_size);
        if (ret < 0) {
            log_err("[ERROR] SimpleBlockCacher::read_fd, unable to read data, "
                    "error code: %zd ", ret);
            if (off_by_block || left < m_object_size)
                free(alignedBuff);
            return -1;
        }

        if (off_by_block || left < m_object_size) {
            memcpy(&buf[off_by_block + length - left],
                   &alignedBuff[off_by_block], left > m_object_size ? \
                   (m_object_size-off_by_block):(left-off_by_block));
            free(alignedBuff);
        }

        left -= m_object_size;
        off_by_block = 0;
        index++;
    }
    return length;
}

int SimpleBlockCacher::remove(uint64_t cache_name) {
    int ret = 0;
    m_meta_tbl_lock.lock();
    const typename BLOCK_INDEX::iterator it = m_meta_tbl.find(cache_name);
    if (it != m_meta_tbl.end()) {
        uint64_t block_id = it->second;
        free_node* new_free_node = new free_node();
        new_free_node->index = block_id;
        new_free_node->next = nullptr;
        p_free_node_tail->next = new_free_node;
        p_free_node_tail = new_free_node;

        m_meta_tbl.erase(it);
    }
    m_meta_tbl_lock.unlock();
    return ret;
}

}  //  namespace cache
}  //  namespace dslab
