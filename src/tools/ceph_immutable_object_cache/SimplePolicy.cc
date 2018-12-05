// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "SimplePolicy.h"

#include <vector>
#include <unordered_map>
#include <string>

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_immutable_obj_cache
#undef dout_prefix
#define dout_prefix *_dout << "ceph::cache::SimplePolicy: " << this << " " \
                           << __func__ << ": "

namespace ceph {
namespace immutable_obj_cache {

SimplePolicy::SimplePolicy(CephContext *cct, uint64_t block_num, float watermark)
  : cct(cct), m_watermark(watermark), m_entry_count(block_num),
    m_cache_map_lock("rbd::cache::SimplePolicy::m_cache_map_lock"),
    m_free_list_lock("rbd::cache::SimplePolicy::m_free_list_lock") {
  ldout(cct, 20) << dendl;
  for(uint64_t i = 0; i < m_entry_count; i++) {
    m_free_list.push_back(new Entry());
  }
}

SimplePolicy::~SimplePolicy() {
  ldout(cct, 20) << dendl;

  for (auto it: m_cache_map) {
    Entry* entry = reinterpret_cast<Entry*>(it.second);
    delete entry;
  }

  for(auto it : m_free_list) {
    Entry* entry = it;
    delete entry;
  }
}

cache_status_t SimplePolicy::alloc_entry(std::string file_name) {
  ldout(cct, 20) << "alloc entry for: " << file_name << dendl;

  m_free_list_lock.lock();

  if (m_free_list.size()) {
    Entry* entry = m_free_list.front();
    ceph_assert(entry != nullptr);
    m_free_list.pop_front();
    m_free_list_lock.unlock();

    {
      RWLock::WLocker wlocker(m_cache_map_lock);
      m_cache_map[file_name] = entry;
    }
    update_status(file_name, OBJ_CACHE_SKIP);
    return OBJ_CACHE_NONE;
  }

  m_free_list_lock.unlock();
  // if there's no free entry, return skip to read from rados
  return OBJ_CACHE_SKIP;
}

cache_status_t SimplePolicy::lookup_object(std::string file_name) {
  ldout(cct, 20) << "lookup: " << file_name << dendl;

  RWLock::RLocker rlocker(m_cache_map_lock);

  auto entry_it = m_cache_map.find(file_name);
  // simplely promote on first lookup
  if (entry_it == m_cache_map.end()) {
      rlocker.unlock();
      return alloc_entry(file_name);
  }

  Entry* entry = entry_it->second;

  if (entry->status == OBJ_CACHE_PROMOTED) {
    // bump pos in lru on hit
    m_promoted_lru.lru_touch(entry);
  }

  return entry->status;
}

void SimplePolicy::update_status(std::string file_name, cache_status_t new_status) {
  ldout(cct, 20) << "update status for: " << file_name
                 << " new status = " << new_status << dendl;

  RWLock::WLocker locker(m_cache_map_lock);

  auto entry_it = m_cache_map.find(file_name);
  if (entry_it == m_cache_map.end()) {
    return;
  }

  ceph_assert(entry_it != m_cache_map.end());
  Entry* entry = entry_it->second;

  // to promote
  if (entry->status == OBJ_CACHE_NONE && new_status== OBJ_CACHE_SKIP) {
    entry->status = new_status;
    entry->file_name = file_name;
    return;
  }

  // promoting done
  if (entry->status == OBJ_CACHE_SKIP && new_status== OBJ_CACHE_PROMOTED) {
    m_promoted_lru.lru_insert_top(entry);
    entry->status = new_status;
    return;
  }

  // promoting failed
  if (entry->status == OBJ_CACHE_SKIP && new_status== OBJ_CACHE_NONE) {
    // mark this entry as free
    entry->file_name = "";
    entry->status = new_status;
    {
      Mutex::Locker free_list_locker(m_free_list_lock);
      m_free_list.push_back(entry);
    }
    m_cache_map.erase(entry_it);
    return;
  }

  // to evict
  if (entry->status == OBJ_CACHE_PROMOTED && new_status== OBJ_CACHE_NONE) {
    // mark this entry as free
    entry->file_name = "";
    entry->status = new_status;
    {
      Mutex::Locker free_list_locker(m_free_list_lock);
      m_free_list.push_back(entry);
    }
    m_promoted_lru.lru_remove(entry);
    m_cache_map.erase(entry_it);
    return;
  }

}

int SimplePolicy::evict_entry(std::string file_name) {
  ldout(cct, 20) << "to evict: " << file_name << dendl;

  update_status(file_name, OBJ_CACHE_NONE);

  return 0;
}

cache_status_t SimplePolicy::get_status(std::string file_name) {
  ldout(cct, 20) << file_name << dendl;

  RWLock::RLocker locker(m_cache_map_lock);
  auto entry_it = m_cache_map.find(file_name);
  if(entry_it == m_cache_map.end()) {
    return OBJ_CACHE_NONE;
  }

  return entry_it->second->status;
}

void SimplePolicy::get_evict_list(std::list<std::string>* obj_list) {
  ldout(cct, 20) << dendl;

  RWLock::WLocker locker(m_cache_map_lock);
  // check free ratio, pop entries from LRU
  if ((float)m_free_list.size() / m_entry_count < m_watermark) {
    int evict_num = 10; //TODO(): make this configurable
    for (int i = 0; i < evict_num; i++) {
      Entry* entry = reinterpret_cast<Entry*>(m_promoted_lru.lru_expire());
      if (entry == nullptr) {
        continue;
      }
      std::string file_name = entry->file_name;
      obj_list->push_back(file_name);

    }
  }
}

// for unit test
uint64_t SimplePolicy::get_free_entry_num() {
  Mutex::Locker free_list_locker(m_free_list_lock);
  return m_free_list.size();
}

uint64_t SimplePolicy::get_promoting_entry_num() {
  uint64_t index = 0;
  RWLock::RLocker rlocker(m_cache_map_lock);
  for (auto it : m_cache_map) {
    if (it.second->status == OBJ_CACHE_SKIP) {
      index++;
    }
  }
  return index;
}

uint64_t SimplePolicy::get_promoted_entry_num() {
  return m_promoted_lru.lru_get_size();
}

std::string SimplePolicy::get_evict_entry() {
  Entry* entry = reinterpret_cast<Entry*>(m_promoted_lru.lru_get_next_expire());
  if (entry == nullptr) {
    return "";
  }
  return entry->file_name;
}

} // namespace immutable_obj_cache
} // namespace ceph
