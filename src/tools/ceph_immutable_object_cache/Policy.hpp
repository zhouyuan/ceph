// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_CACHE_POLICY_HPP
#define CEPH_CACHE_POLICY_HPP

#include <list>
#include <string>

namespace ceph {
namespace immutable_obj_cache {

enum CACHESTATUS {
  OBJ_CACHE_NONE = 0,  // obj missing, but cache still have free space.
  OBJ_CACHE_PROMOTING, // obj hit, but in promoting.
  OBJ_CACHE_PROMOTED,  // obj hit. 
  OBJ_CACHE_ERROR,     // for any other situations, object status will be marked as error.
                       // for example, miss and don't have free space.
};


class Policy {
public:
  Policy(){}
  virtual ~Policy(){};
  virtual CACHESTATUS lookup_object(std::string) = 0;
  virtual int evict_object(std::string&) = 0;
  virtual void update_status(std::string, CACHESTATUS) = 0;
  virtual CACHESTATUS get_status(std::string) = 0;
  virtual void get_evict_list(std::list<std::string>* obj_list) = 0;
};

} // namespace immutable_obj_cache
} // namespace ceph
#endif
