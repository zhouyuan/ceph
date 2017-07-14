// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "librbd/cache/file/StupidPolicy.h"
#include "common/dout.h"
#include "librbd/ImageCtx.h"
#include "librbd/cache/BlockGuard.h"

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd::cache::file::StupidPolicy: " << this \
                           << " " <<  __func__ << ": "

namespace librbd {
namespace cache {
namespace file {

template <typename I>
StupidPolicy<I>::StupidPolicy(I &image_ctx, BlockGuard &block_guard)
  : m_image_ctx(image_ctx), m_block_guard(block_guard),
    m_lock("librbd::cache::file::StupidPolicy::m_lock") {

  set_block_count(offset_to_block(image_ctx.size));
  // TODO support resizing of entries based on number of provisioned blocks
  m_entries.resize(m_block_count); // 1GB of storage
  for (auto &entry : m_entries) {
    m_free_lru.insert_tail(&entry);
  }
}

template <typename I>
void StupidPolicy<I>::set_block_count(uint64_t block_count) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "block_count=" << block_count << dendl;

  // TODO ensure all entries are in-bound
  Mutex::Locker locker(m_lock);
  m_block_count = block_count;

}

template <typename I>
int StupidPolicy<I>::invalidate(uint64_t block) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "block=" << block << dendl;

  // TODO handle case where block is in prison (shouldn't be possible
  // if core properly registered blocks)

  Mutex::Locker locker(m_lock);
  auto entry_it = m_block_to_entries.find(block);
  if (entry_it == m_block_to_entries.end()) {
    return 0;
  }

  Entry *entry = entry_it->second;
  m_block_to_entries.erase(entry_it);

  LRUList *lru;
  lru = &m_clean_lru;
  lru->remove(entry);

  m_free_lru.insert_tail(entry);
  return 0;
}

template <typename I>
int StupidPolicy<I>::map(IOType io_type, uint64_t block, bool partial_block,
                         PolicyMapResult *policy_map_result,
                         bool in_base_cache) {
  CephContext *cct = m_image_ctx.cct;
  ldout(cct, 20) << "block=" << block << dendl;

  Mutex::Locker locker(m_lock);
  if (block >= m_block_count) {
    ldout(cct, 1) << "block outside of valid range" << dendl;
    *policy_map_result = POLICY_MAP_RESULT_MISS;
    // TODO return error once resize handling is in-place
    return 0;
  }

  Entry *entry;
  auto entry_it = m_block_to_entries.find(block);
  if (entry_it != m_block_to_entries.end()) {
    // cache hit -- move entry to the front of the queue
    entry = entry_it->second;
    LRUList *lru;
    lru = &m_clean_lru;
    
    if (io_type == IO_TYPE_WRITE) {
      *policy_map_result = POLICY_MAP_RESULT_MISS;
      if( entry->in_base_cache ) {
        entry->in_base_cache = false;
      }
      lru->remove(entry);
      m_free_lru.insert_tail(entry);
      m_block_to_entries.erase(entry_it);
      return 0;
    }
    if( entry->in_base_cache ) {
      ldout(cct, 1) << "cache hit in base snap, blocks: " << block << dendl;
      *policy_map_result = POLICY_MAP_RESULT_HIT_IN_BASE;
    } else {
      ldout(cct, 1) << "cache hit, block: " << block << dendl;
      *policy_map_result = POLICY_MAP_RESULT_HIT;
    }

    lru->remove(entry);
    lru->insert_head(entry);
    return 0;
  }

  // cache miss
  entry = reinterpret_cast<Entry*>(m_free_lru.get_head());
  if (entry != nullptr) {
    // entries are available -- allocate a slot
    ldout(cct, 1) << "cache miss -- new entry, block: " << block << dendl;
    *policy_map_result = POLICY_MAP_RESULT_NEW;
    m_free_lru.remove(entry);

    entry->block = block;
    entry->in_base_cache = in_base_cache;
    m_block_to_entries[block] = entry;
    m_clean_lru.insert_head(entry);
    return 0;
  } else {
    ldout(cct, 1) << "cache miss: " << block << dendl;
  }

  // no clean entries to evict -- treat this as a miss
  *policy_map_result = POLICY_MAP_RESULT_MISS;
  return 0;
}

template <typename I>
void StupidPolicy<I>::tick() {
  // stupid policy -- do nothing
}

template <typename I>
void StupidPolicy<I>::set_to_base_cache(uint64_t block) {
  Mutex::Locker locker(m_lock);
  auto entry_it = m_block_to_entries.find(block);
  assert(entry_it != m_block_to_entries.end());
  Entry* entry = entry_it->second;
  entry->in_base_cache = true;
}

} // namespace file
} // namespace cache
} // namespace librbd

template class librbd::cache::file::StupidPolicy<librbd::ImageCtx>;
