// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_CACHE_SIMPLE_POLICY_HPP
#define CEPH_CACHE_SIMPLE_POLICY_HPP

#include "Policy.hpp"
#include "include/lru.h"
#include "common/RWLock.h"
#include "common/Mutex.h"

#include <vector>
#include <unordered_map>
#include <string>

namespace ceph {
namespace immutable_obj_cache {


class SimplePolicy : public Policy {
public:
  SimplePolicy(uint64_t block_num, float watermark)
    : m_watermark(watermark), m_entry_count(block_num),
      m_cache_map_lock("rbd::cache::SimplePolicy::m_cache_map_lock"),
      m_free_list_lock("rbd::cache::SimplePolicy::m_free_list_lock")
  {
    for(uint64_t i = 0; i < m_entry_count; i++) {
      m_free_list.push_back(new Entry());
    }
  }

  // entry include free entry, promoting entry and promoted entry.
  ~SimplePolicy() {
    // release free entry  
    while(!m_free_list.empty()) {
      Entry* entry = reinterpret_cast<Entry*>(m_free_list.front());
      delete entry;
      m_free_list.pop_front();
    }
    // release promoting and promoted entry.
    for(auto it = m_cache_map.begin(); it != m_cache_map.end(); ++it) {
       Entry* entry = reinterpret_cast<Entry*>(it->second);
       delete entry;
    }
  }

  CACHESTATUS lookup_object(std::string cache_file_name) {

    //TODO(): check race condition
    RWLock::WLocker wlocker(m_cache_map_lock);

    auto entry_it = m_cache_map.find(cache_file_name);
    if(entry_it == m_cache_map.end()) {
      // the following two cases will cause that cache don't have free space.
      // 1: evict thread don't startup. <--current situation
      // 2: due to race condition, evict thread don't timely kick off the oldest entry at some times points. 
      if(m_free_list.size() == 0) { 
        // missing and don't have free space
        return OBJ_CACHE_ERROR; 
      }
      Mutex::Locker locker(m_free_list_lock);
      Entry* entry = reinterpret_cast<Entry*>(m_free_list.front());
      assert(entry != nullptr);
      m_free_list.pop_front();
      entry->status = OBJ_CACHE_PROMOTING;

      m_cache_map[cache_file_name] = entry;

      // miss and have free space
      return OBJ_CACHE_NONE;
    }

    Entry* entry = entry_it->second;

    if(entry->status == OBJ_CACHE_PROMOTED) {
      // touch it
      m_promoted_lru.lru_touch(entry);
    }

    // hit, namely promoting or promoted.
    return entry->status;
  }

  int evict_object(std::string& out_cache_file_name) {
    RWLock::WLocker locker(m_cache_map_lock);

    return 1;
  }

  // TODO(): simplify the logic...yuan.
  // when calling this method, must ensure that file_name is in m_cache_map. 
  // only one case: promoting --> promoted.
  void update_status(std::string file_name, CACHESTATUS new_status) {
    RWLock::WLocker locker(m_cache_map_lock);

    auto entry_it = m_cache_map.find(file_name);
    assert(new_status == OBJ_CACHE_PROMOTED && entry_it != m_cache_map.end() 
           && entry_it->second->status == OBJ_CACHE_PROMOTING);

    entry_it->second->status = new_status;
    m_promoted_lru.lru_insert_top(entry_it->second);
  }

  // get entry status
  CACHESTATUS get_status(std::string file_name) {
    RWLock::RLocker locker(m_cache_map_lock);
    auto entry_it = m_cache_map.find(file_name);
    if(entry_it == m_cache_map.end()) {
      return OBJ_CACHE_NONE;
    }

    return entry_it->second->status;
  }

  // according to watermark, evcit thread kick off the oldest entry.
  void get_evict_list(std::list<std::string>* obj_list) {
    RWLock::WLocker locker(m_cache_map_lock);
    // check free ratio, pop entries from LRU
    if (m_free_list.size() / m_entry_count < m_watermark) {
      int evict_num = 10; //TODO(): make this configurable
      for(int i = 0; i < evict_num; i++) {
        Entry* entry = reinterpret_cast<Entry*>(m_promoted_lru.lru_expire());
        if (entry == nullptr) {
	  continue;
        }
        std::string file_name = entry->cache_file_name;
        obj_list->push_back(file_name);

        auto entry_it = m_cache_map.find(file_name);
        m_cache_map.erase(entry_it);

        //mark this entry as free
	entry->status = OBJ_CACHE_NONE;
        Mutex::Locker locker(m_free_list_lock);
        m_free_list.push_back(entry);
      }
    }
  }

private:

  class Entry : public LRUObject {
    public:
      CACHESTATUS status;
      Entry() : status(OBJ_CACHE_NONE){}
      std::string cache_file_name;
      void encode(bufferlist &bl){}
      void decode(bufferlist::iterator &it){}
  };

  float m_watermark;
  uint64_t m_entry_count;

  std::unordered_map<std::string, Entry*> m_cache_map;
  RWLock m_cache_map_lock;

  std::deque<Entry*> m_free_list;
  Mutex m_free_list_lock;

  LRU m_promoted_lru; // include promoted, using status.

};

} // namespace immutable_obj_cache
} // namespace ceph
#endif
