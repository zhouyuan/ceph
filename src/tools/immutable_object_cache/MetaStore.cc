// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "MetaStore.h"

#include <unordered_map>

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_immutable_obj_cache
#undef dout_prefix
#define dout_prefix *_dout << "ceph::cache::MetaStore: " << this << " " \
                           << __func__ << ": "


namespace ceph {
namespace immutable_obj_cache {

MetaStore::MetaStore(CephContext *cct, std::string cache_root_dir)
      : m_cct(cct), m_cache_root_dir(cache_root_dir) {

 // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
  rocksdb::Options options;
  options.IncreaseParallelism();
  options.OptimizeLevelStyleCompaction();

  // create the DB if it's not already present
  options.create_if_missing = true;

  //std::string kDBPath = cache_root_dir + "/rocksdb/";
  std::string kDBPath = cache_root_dir;
  rocksdb::Status s = rocksdb::DB::Open(options, kDBPath, &db);
  if (!s.ok()) {
    ceph_assert(0);
  }
}

MetaStore::~MetaStore() {
  delete db;
}

int MetaStore::put(std::string k, std::string v) {
  db->Put(rocksdb::WriteOptions(), k, v);
}

int MetaStore::remove(std::string k) {
  db->Delete(rocksdb::WriteOptions(), k);
}

std::string MetaStore::get(std::string k) {
  std::string value;
  db->Get(rocksdb::ReadOptions(), k, &value);
  return value;
}

int MetaStore::load_all(std::unordered_map<std::string, SimplePolicy::Entry*> cache_map) {
  std::string value;
  rocksdb::Iterator* it = db->NewIterator(rocksdb::ReadOptions());
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
     value = it->value().ToString();
     SimplePolicy::Entry* entry = new SimplePolicy::Entry();
     cache_map[value] = entry;
    }
  return 0;
}

}  // namespace immutable_obj_cache
}  // namespace ceph
