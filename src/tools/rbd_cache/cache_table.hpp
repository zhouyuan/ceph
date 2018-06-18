#ifndef CACHE_FILE_HPP
#define CACHE_FILE_HPP

#include <string>
#include <iostream> //TODO
#include <stdio.h> //TODO
#include <cstdio> // TODO

#include "policy.hpp"
#include "simple_cache.hpp"

// TODO maybe need to modify class name

enum {
  NONE = 0, 
  PROMOTING,
  PROMOTED,
  IN_USING,
  EVICTING,
  EVICTED,
};

class CacheFile {
public:
  CacheFile(std::string file_name)
    : m_status(NONE),
      m_file_name(file_name)
  {}

  ~CacheFile() {
    if(m_status != EVICTED) {
      delete_corresponding_file();
    }
  }

  void set_none() { m_status = NONE; }
  void set_promoting() { m_status = PROMOTING; }
  void set_promoted() { m_status = PROMOTED; }
  void set_in_using() { m_status = IN_USING; }
  void set_evicting() { m_status = EVICTING; } 
  void set_evicted() { m_status = EVICTED; } 

  int get_status() { return m_status; } 

  void set_status(int status) {
    switch(status) {
      case NONE:
        set_none();
        break;
      case PROMOTING:
        set_promoting();
        break;
      case PROMOTED:
        set_promoted();
        break;
      case IN_USING:
        set_in_using();
        break;
      case EVICTING:
        set_evicting();
        break;
      case EVICTED:
        set_evicted();
        break;
      default:
        // TODO
        assert(0);
    }
  }

  // for testing 
  void print_status() {
    switch(m_status) {
      case NONE:
        std::cout<<m_file_name<<" status : none " <<std::endl;
        break;
      case PROMOTING:
        std::cout<<m_file_name<<" status : promoting "<<std::endl;
        break;
      case PROMOTED:
        std::cout<<m_file_name << " status : promoted " <<std::endl;
        break;
     case IN_USING:
        std::cout<<m_file_name << " status : in using " <<std::endl;
        break;
     case EVICTING:
        std::cout<<m_file_name << " status : evicting " <<std::endl;
        break;
     case EVICTED :
        std::cout<<m_file_name << " status : evicted " << std::endl;
        break;
    }
  }
  void delete_corresponding_file() {
    int ret;
    ret = ::remove(m_file_name.c_str());
    if(ret != 0) {
      std::cout<<"delete file: failed."<<std::endl;
      //lderr(m_cct) << "fail to delete cache file." << dendl;
    }
  }

  int m_status;
  std::string m_file_name;
};


class CacheTable {
public:
  enum {
    SIMPLE_LRU = 0, 
    RANDOM_LRU,
    XLIST_LRU,
  };

  CacheTable(uint32_t block_num, int policy_type, float evict_level = 0.9)
    : m_evict_level(evict_level),
      m_policy_type(SIMPLE_LRU) // TODO  param determine lru type
  {
    switch(m_policy_type) {
      case SIMPLE_LRU :
        m_policy = new SimpleLRU(block_num);
        break;
      case RANDOM_LRU :
        // TODO 
        break;
      case XLIST_LRU :
        // TODO
        break;
    }

  }

  ~CacheTable(){
  }

  int get_LRU_type() {
    return m_policy_type;
  }

  void print_LRU_type() {
     // TODO
  }
  
  void remove(std::string key) {
    //assert(m_policy->get_status(key) != IN_USING);
    CacheFile* deleting_file;
    if(m_policy->remove(key, &deleting_file)) {
      delete deleting_file;
    }
  }

  bool insert(std::string _cache_file_name) {
    bool ret;
    ret = m_policy->lookup(_cache_file_name);
    if(ret) {
      return false;
    }

    CacheFile* _cache_file = new CacheFile(_cache_file_name);
    m_policy->insert(_cache_file_name, _cache_file);

    return true;
  }

  // if true, express have encough free space.
  bool if_lower_evict_level() {
    uint64_t temp_current_size = m_policy->get_current_size();
    uint64_t temp_max_size = m_policy->get_max_size();
    float temp_current_evict_level = temp_current_size / temp_max_size;
    return temp_current_evict_level < m_evict_level;
  }

  // just lookup hit or miss.
  bool lookup(std::string key) {
    return m_policy->lookup(key);
  }

  bool lookup_and_touch(std::string& key) {
    return m_policy->lookup_and_touch(key);
  }

  bool touch(std::string key) {
    m_policy->touch(key);
    return true;
  }

  bool set_status(std::string key, int status) {
    CacheFile* cache_file_out;
    if(m_policy->lookup(key, &cache_file_out)) { 
      cache_file_out->set_status(status);
      return true;
    }
    return false;
  }


  int get_status(std::string key) {
    CacheFile* temp_cache_file; 
    if(m_policy->lookup(key, &temp_cache_file)) {
      return temp_cache_file->get_status();
    }
    return -1;
  }


  bool get_the_lowest_priority_key(std::string& key) {
    if(m_policy->get_current_size() == 0) {
      return false;
    }
    m_policy->get_back(key);
    return true;
  }

//========== just for testing =========================

  void print_key() {
    m_policy->print_key();
  }

  void print_key_and_value() {
    m_policy->print_key_and_value();
  }


private:

  LRUPolicy* m_policy;
  float m_evict_level;
  int m_policy_type;

};

#endif
