#ifndef RBD_CACHE_SIMPLE_POLICY_HPP
#define RBD_CACHE_SIMPLE_POLICY_HPP

#include "Policy.hpp"
#include "include/lru.h"
#include "common/RWLock.h"

#include <vector>
#include <unordered_map>
#include <string>

namespace rbd {
namespace cache {


class SimplePolicy : public Policy {
public:
  SimplePolicy(uint64_t block_num, float level)
    : m_level(level),
      m_lock("rbd::cache::SimplePolicy::m_lock"),
      m_entry_count(block_num)
  {

    for(uint64_t i = 0; i < m_entry_count; i++) {
      Entry* entry = new Entry();
      m_free_lru.lru_insert_bot(entry);
    }

  }

  ~SimplePolicy() {
    for(uint64_t i = 0; i < m_entry_count; i++) {
      Entry* entry = reinterpret_cast<Entry*>(m_free_lru.lru_get_next_expire());
      delete entry;
    }
  }

  CACHESTATUS lookup_object(std::string cache_file_name) {

    //TODO(): check race condition
    RWLock::WLocker locker(m_lock);

    auto entry_it = m_oid_to_entry.find(cache_file_name);
    if(entry_it == m_oid_to_entry.end()) {
      Entry* entry = reinterpret_cast<Entry*>(m_free_lru.lru_get_next_expire());
      assert(entry != nullptr);
      entry->status = OBJ_CACHE_PROMOTING;

      m_oid_to_entry[cache_file_name] = entry;
      m_free_lru.lru_remove(entry);
      m_evicting_lru.lru_insert_top(entry);

      return OBJ_CACHE_NONE;
    }

    Entry* entry = entry_it->second;

    LRU* lru;
    if(entry->status == OBJ_CACHE_PROMOTED) {
      lru = &m_promoted_lru;
    }

    // touch it
    lru->lru_touch(entry);

    return entry->status;
  }

  int evict_object(std::string& out_cache_file_name) {
    RWLock::WLocker locker(m_lock);

    // still have enough free space, don't need to evict lru.
    uint64_t temp_current_size = m_oid_to_entry.size();
    float temp_current_evict_level = temp_current_size / m_entry_count;
    if(temp_current_evict_level < m_level) {
      return 0;
    }

    // when all entries are USING, PROMOTING or EVICTING, just busy waiting.
    if(m_promoted_lru.lru_get_size() == 0) {
      return 0;
    }

    assert(m_promoted_lru.lru_get_size() != 0);

    // evict one item from promoted lru
    Entry *entry = reinterpret_cast<Entry*>(m_promoted_lru.lru_get_next_expire());
    assert(entry != nullptr);

    assert(entry->status == OBJ_CACHE_PROMOTED);

    out_cache_file_name = entry->cache_file_name;
    entry->status = OBJ_CACHE_EVICTING;

    m_promoted_lru.lru_remove(entry);
    m_evicting_lru.lru_insert_top(entry);

    return 1;
  }

  // TODO(): simplify the logic
  void update_status(std::string _file_name, CACHESTATUS _status) {
    RWLock::WLocker locker(m_lock);

    Entry* entry;
    auto entry_it = m_oid_to_entry.find(_file_name);

    // just check.
    if(_status == OBJ_CACHE_PROMOTING) {
      assert(m_oid_to_entry.find(_file_name) == m_oid_to_entry.end());
    }

    // miss this object.
    if(entry_it == m_oid_to_entry.end() && _status == OBJ_CACHE_PROMOTING) {
      entry = reinterpret_cast<Entry*>(m_free_lru.lru_get_next_expire());
      if(entry == nullptr) {
        assert(0); // namely evict thread have some problems.
      }

      entry->status = OBJ_CACHE_PROMOTING;

      m_oid_to_entry[_file_name] = entry;
      m_free_lru.lru_remove(entry);
      m_evicting_lru.lru_insert_top(entry);

      return;
    }

    assert(entry_it != m_oid_to_entry.end());

    entry = entry_it->second;

    // promoting action have been finished, so update it.
    if(entry->status == OBJ_CACHE_PROMOTING && _status== OBJ_CACHE_PROMOTED) {
      m_evicting_lru.lru_remove(entry);
      m_promoted_lru.lru_insert_top(entry);
      entry->status = OBJ_CACHE_PROMOTED;
      return;
    }

    // will delete this cache file
    if(entry->status == OBJ_CACHE_PROMOTED && _status == OBJ_CACHE_EVICTING) {
      m_promoted_lru.lru_remove(entry);
      m_evicting_lru.lru_insert_top(entry);
      entry->status = OBJ_CACHE_EVICTING;
      return;
    }


    if(_status == OBJ_CACHE_EVICTED) {
      m_oid_to_entry.erase(entry_it);
      m_evicting_lru.lru_remove(entry);
      m_free_lru.lru_insert_bot(entry);
      return;
    }

    assert(0);
  }

  // get entry status
  CACHESTATUS get_status(std::string _file_name) {
    RWLock::RLocker locker(m_lock);
    auto entry_it = m_oid_to_entry.find(_file_name);
    if(entry_it == m_oid_to_entry.end()) {
      return OBJ_CACHE_NONE;
    }

    return entry_it->second->status;
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

  std::unordered_map<std::string, Entry*> m_oid_to_entry;

  LRU m_free_lru;
  LRU m_evicting_lru; // include promoting status or evicting status
  LRU m_promoted_lru; // include promoted, using status.

  float m_level;
  RWLock m_lock;
  uint64_t m_entry_count;
};

} // namespace cache
} // namespace rbd
#endif
