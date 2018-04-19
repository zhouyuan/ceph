// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "Types.h"
#include "include/buffer_fwd.h"

namespace rbd {
namespace cache {
namespace file {

namespace meta_store {

void Header::encode(bufferlist& bl) const {
  ENCODE_START(1, 1, bl);
  ceph::encode(journal_sequence, bl);
  ENCODE_FINISH(bl);
}

void Header::decode(bufferlist::iterator& it) {
  DECODE_START(1, it);
  ceph::decode(journal_sequence, it);
  DECODE_FINISH(it);
}

void Header::dump(Formatter *f) const {
  // TODO
}

void Header::generate_test_instances(std::list<Header *> &o) {
  // TODO
}

} // namespace meta_store

} // namespace file
} // namespace cache
} // namespace rbd
