// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_CACHE_META_STORE_H
#define CEPH_CACHE_META_STORE_H

#include "common/ceph_context.h"
#include "common/Mutex.h"
#include "include/rados/librados.hpp"
#include "rocksdb/db.h"
#include "rocksdb/options.h"

#include "SimplePolicy.h"

using librados::Rados;
using librados::IoCtx;
class Context;

namespace ceph {
namespace immutable_obj_cache {


class MetaStore {
 public:
  MetaStore(CephContext *cct, std::string kDBPath);
  ~MetaStore();

 int put(std::string k, std::string v);
 int remove(std::string k);
 std::string get(std::string k);
 int load_all(std::unordered_map<std::string, SimplePolicy::Entry*> cache_map);

  CephContext *m_cct;
  rocksdb::DB* db;
  std::string m_cache_root_dir;
};

}  // namespace ceph
}  // namespace immutable_obj_cache
#endif
