#ifndef POLICY_HPP
#define POLICY_HPP

#include <string> 

class CacheFile;

class LRUPolicy {
public:
  LRUPolicy(){}; 

  virtual ~LRUPolicy(){};

  virtual uint64_t get_current_size() = 0;

  virtual uint64_t get_max_size() = 0;

  virtual bool remove(std::string, CacheFile**) = 0;

  virtual bool remove(CacheFile**) = 0;

  virtual void get_back(std::string&) = 0;

  virtual void insert(std::string, CacheFile*) = 0;

  virtual void touch(std::string key) = 0;

  virtual bool lookup_and_touch(std::string, CacheFile** out) = 0;

  virtual bool lookup_and_touch(std::string&) = 0;

  virtual bool lookup(std::string, CacheFile**) = 0;

  virtual bool lookup(std::string) = 0;

  // testing 
  virtual void print_key() = 0;
  virtual void print_key_and_value() = 0;

};

#endif
