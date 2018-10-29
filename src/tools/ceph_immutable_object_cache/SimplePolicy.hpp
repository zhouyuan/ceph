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

  // Entry have two situations: 
  //  1 : free entry is in m_free_list
  //  2 : promoting or promoted entry is in m_cache_map. <---- memory leak.                     
  ~SimplePolicy() {
    for(auto it = m_free_list.begin(); it != m_free_list.end(); it++) {
      Entry* entry = reinterpret_cast<Entry*>(m_free_list.front());
      delete entry;
    }
    for(auto it = m_cache_map.begin(); it != m_cache_map.end(); it++) {
      Entry* entry = reinterpret_cast<Entry*>(it->second);
      delete entry;
    }
  }

  CACHESTATUS lookup_object(std::string cache_file_name) {

    //TODO(): check race condition
    RWLock::WLocker wlocker(m_cache_map_lock);

    auto entry_it = m_cache_map.find(cache_file_name);
    // simplely promote on first lookup
    if(entry_it == m_cache_map.end()) {
      Mutex::Locker locker(m_free_list_lock);
      // when miss and don't have free space, can't execute the following codes. 
      if(m_free_list.size() == 0) {
        return OBJ_CACHE_NONE;
      }
      Entry* entry = m_free_list.front();
      m_free_list.pop_front();
      entry->status = OBJ_CACHE_PROMOTING;
      entry->cache_file_name = cache_file_name;

      m_cache_map[cache_file_name] = entry;

      return OBJ_CACHE_NONE;
    }

    Entry* entry = entry_it->second;

    if(entry->status == OBJ_CACHE_PROMOTED) {
      // touch it
      m_promoted_lru.lru_touch(entry);
    }

    return entry->status;
  }

  void update_status(std::string file_name, CACHESTATUS new_status) {
    RWLock::WLocker locker(m_cache_map_lock);

    auto entry_it = m_cache_map.find(file_name);

    // NONE --> PROMOTING : lookup_object have handled this case.
    if(entry_it == m_cache_map.end() && new_status == OBJ_CACHE_PROMOTING) {
      assert(0);
    }
    
    // async promote fails: promoting --> none 
    if(entry_it->second->status == OBJ_CACHE_PROMOTING && new_status == OBJ_CACHE_NONE) {
      entry_it->second->status = OBJ_CACHE_NONE;
      entry_it->second->cache_file_name = "";
      m_free_list.push_back(entry_it->second);
      m_cache_map.erase(entry_it); 
      return;
    }

    // async promote fails or release space: promoted --> none
    if(entry_it->second->status == OBJ_CACHE_PROMOTED && new_status == OBJ_CACHE_NONE) {
      entry_it->second->status = OBJ_CACHE_NONE;
      entry_it->second->cache_file_name = "";
      m_promoted_lru.lru_remove(entry_it->second);
      m_free_list.push_back(entry_it->second);
      m_cache_map.erase(entry_it);
      return;
    }

    // promoting --> promoted
    if(entry_it->second->status == OBJ_CACHE_PROMOTING && new_status== OBJ_CACHE_PROMOTED) {
      m_promoted_lru.lru_insert_top(entry_it->second);
      entry_it->second->status = new_status;
      return;
    }
    
    assert(0);
  }

  int evict_entry(std::string file_name) {
    RWLock::WLocker map_locker(m_cache_map_lock);
    auto entry_it = m_cache_map.find(file_name);
    if (entry_it == m_cache_map.end()) {
      return 0;
    }
    m_cache_map.erase(entry_it);

    //mark this entry as free
    Entry* entry = entry_it->second;
    if(entry->status == OBJ_CACHE_PROMOTED) {
      m_promoted_lru.lru_remove(entry);
    }
    entry->status = OBJ_CACHE_NONE;
    entry->cache_file_name = "";
    Mutex::Locker free_list_locker(m_free_list_lock);
    m_free_list.push_back(entry);
    return 0;
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

  void get_evict_list(std::list<std::string>* obj_list) {
    RWLock::WLocker locker(m_cache_map_lock);
    // check free ratio, pop entries from LRU
    if ((float)m_free_list.size() / (float)m_entry_count < m_watermark) {
      int evict_num = 10; //TODO(): make this configurable
      for(int i = 0; i < evict_num; i++) {
        Entry* entry = reinterpret_cast<Entry*>(m_promoted_lru.lru_expire());
        if (entry == nullptr) {
	  continue;
        }
        std::string file_name = entry->cache_file_name;
        obj_list->push_back(file_name);

      }
    }
  }

  // for unit test
  uint64_t get_free_entry_num() {
    return m_free_list.size();
  } 

  uint64_t get_promoting_entry_num() {
    uint64_t index = 0;
    for(auto temp = m_cache_map.begin(); temp != m_cache_map.end(); temp++) {
      if(temp->second->status == OBJ_CACHE_PROMOTING) {
        index++;
      }
    }
    return index;
  }
 
  uint64_t get_promoted_entry_num() {
    return m_promoted_lru.lru_get_size();
  }

  std::string get_evict_entry() {
    Entry* entry = reinterpret_cast<Entry*>(m_promoted_lru.lru_get_next_expire());
    if(entry == NULL) {
      return "";
    }
    return entry->cache_file_name;
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
