#ifndef SIMPLELRU_H
#define SIMPLELRU_H

#include "common/Mutex.h"
#include <unordered_map>
#include <list>
#include "policy.hpp"

class CacheFile;

class SimpleLRU : public LRUPolicy
{
private:
  Mutex lock;
  uint64_t m_max_size;
  uint64_t m_current_size;
  std::unordered_map<std::string, 
      std::list<std::pair<std::string, CacheFile*> >::iterator, std::hash<std::string>> contents;

  std::list<std::pair<std::string, CacheFile*> > lru;

  void trim_cache() 
  {
    while (contents.size() > m_max_size) {
      contents.erase(lru.back().first);
      lru.pop_back();
    }
  }


public:

  SimpleLRU(uint64_t m_size) 
    : lock("SimpleLRU::lock"), 
      m_max_size(m_size), 
      m_current_size(0)
  {
    contents.rehash(m_max_size);
  }

  ~SimpleLRU() {
  }

  uint64_t get_current_size() {
    return contents.size();
  }

  uint64_t get_max_size() {
    return m_max_size;
  }

  // delete key
  bool remove(std::string key, CacheFile** out) 
  {
    Mutex::Locker l(lock);
    std::unordered_map<std::string, 
        std::list<std::pair<std::string, CacheFile*> >::iterator, 
        std::hash<std::string>>::iterator i = contents.find(key);

    if (i == contents.end()) {
      return false;
    }

    *out = i->second->second;

    lru.erase(i->second);
    contents.erase(i);
  }

  bool remove(CacheFile** out) {
    Mutex::Locker l(lock);
    if(contents.size() == 0) {
      return false;
    }

    *out = lru.back().second;

    contents.erase(lru.back().first);
    lru.pop_back();
  }

  void get_back(std::string& out) {
      Mutex::Locker l(lock);
      assert(contents.size() != 0);
      out = lru.back().first;
  }

  void insert(std::string key, CacheFile* value) {
    Mutex::Locker l(lock);
    lru.emplace_front(key, value);
    contents[key] = lru.begin();
  }

  void touch(std::string key) {
    Mutex::Locker l(lock);
    std::unordered_map<std::string, 
        std::list<std::pair<std::string, 
        CacheFile*> >::iterator, std::hash<std::string>>::iterator i = contents.find(key);

    assert(i != contents.end());
    
    lru.splice(lru.begin(), lru, i->second);
  }

  bool lookup_and_touch(std::string key, CacheFile** out) 
  {
    Mutex::Locker l(lock);
    std::unordered_map<std::string, 
        std::list<std::pair<std::string, 
        CacheFile*> >::iterator, std::hash<std::string>>::iterator i = contents.find(key);

    // hit
    if (i != contents.end()) {
      *out = i->second->second;
      lru.splice(lru.begin(), lru, i->second);
      return true;
    }

    return false;
  }

  bool lookup_and_touch(std::string& key) 
  {
    Mutex::Locker l(lock);
    std::unordered_map<std::string, 
        std::list<std::pair<std::string, 
        CacheFile*> >::iterator, std::hash<std::string>>::iterator i = contents.find(key);

    // hit
    if (i != contents.end()) {
      lru.splice(lru.begin(), lru, i->second);
      return true;
    }

    return false;
  }


  // if miss, directly return
  // if hit, return corresponding value
  bool lookup(std::string key, CacheFile** out) {
    Mutex::Locker l(lock);
    auto it = contents.find(key);
    if(it == contents.end()) {
      *out = it->second->second;
      return true;
    }
    return false;
  }

  // just return hit or miss
  bool lookup(std::string key) {
    //Mutex::Locker l(lock);
    return contents.find(key) != contents.end();
  }

  // ========== just for testing ================

  void print_key() {
    for(auto it = lru.begin(); it != lru.end(); it++) {
      std::cout<<it->first<<" ";
    }
    std::cout<<std::endl;
  }

  void print_key_and_value() {}


};

#endif
