// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_LIBRBD_CACHE_FILE_TYPES
#define CEPH_LIBRBD_CACHE_FILE_TYPES

#include "include/buffer_fwd.h"
#include "include/encoding.h"
#include "include/int_types.h"
#include <deque>
#include <list>
#include <type_traits>
#include <utility>

namespace ceph { struct Formatter; }

namespace rbd {
namespace cache {
namespace file {

/**
 * Persistent on-disk cache structures
 */

namespace meta_store {

struct Header {

  // TODO using ring-buffer -- use separate files as alternative (?)
  uint8_t journal_sequence = 0;

  void encode(bufferlist& bl) const;
  void decode(bufferlist::iterator& it);
  void dump(Formatter *f) const;

  static void generate_test_instances(std::list<Header *> &o);
};

} // namespace meta_store


} // namespace file
} // namespace cache
} // namespace rbd

WRITE_CLASS_ENCODER(librbd::cache::file::meta_store::Header);
WRITE_CLASS_ENCODER(librbd::cache::file::journal_store::Event);
WRITE_CLASS_ENCODER(librbd::cache::file::journal_store::EventBlock);

#endif // CEPH_LIBRBD_CACHE_FILE_TYPES
