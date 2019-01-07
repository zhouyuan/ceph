// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_CACHE_SOCKET_COMMON_H
#define CEPH_CACHE_SOCKET_COMMON_H

#include "include/types.h"
#include "include/encoding.h"
#include "include/int_types.h"

namespace ceph {
namespace immutable_obj_cache {

static const int RBDSC_REGISTER        =  0X11;
static const int RBDSC_READ            =  0X12;
static const int RBDSC_LOOKUP          =  0X13;
static const int RBDSC_REGISTER_REPLY  =  0X14;
static const int RBDSC_READ_REPLY      =  0X15;
static const int RBDSC_LOOKUP_REPLY    =  0X16;
static const int RBDSC_READ_RADOS      =  0X17;



typedef std::function<void(uint64_t, std::string)> ProcessMsg;
typedef std::function<void(std::string)> ClientProcessMsg;
typedef uint8_t rbdsc_req_type;

//TODO(): switch to bufferlist
struct rbdsc_req_type_t {
  rbdsc_req_type type;
  uint64_t vol_size;
  uint64_t offset;
  uint64_t length;
  char pool_name[256];
  char vol_name[256];
  char oid[256];

  uint64_t size() {
    return sizeof(rbdsc_req_type_t);
  }

  std::string to_buffer() {
    std::stringstream ss;
    ss << type;
    ss << vol_size;
    ss << offset;
    ss << length;
    ss << pool_name;
    ss << vol_name;

    return ss.str();
  }
};

static const int RBDSC_MSG_LEN = sizeof(rbdsc_req_type_t);

/*
 *
 * +--------+----------+------------------------------------+
 * |        |          |                                    |
 * | m_head | m_mid    |    reserve bufferlist for data     |
 * |        |          |                                    |
 * +--------+----------+------------------------------------+
 *
 *  m_head : fixed size, and algined
 *
 *  m_mid : its size is variable, and its member also is variable.
 *
 *  m_data : reserved bufferlist. currently, its size is empty.
 */

/*
struct ObjectCacheMsgHeader {
    __le64 seq;
    __le16 type;
    __le16 version;
    __le64 mid_len;
    __le32 data_len;
    __le32 reserved;
} __attribute__ ((packed));
*/

/* if don't encode/decode this class, will make sure how many size for every member.
 */
class ObjectCacheMsgMiddle {
public:
  uint64_t m_image_size;
  uint64_t m_read_offset;
  uint64_t m_read_len;
  std::string m_pool_name;
  std::string m_image_name;
  std::string m_oid;

   ObjectCacheMsgMiddle(){}
   ~ObjectCacheMsgMiddle(){}

   void encode(bufferlist& bl) {
     ceph::encode(m_image_size, bl);
     ceph::encode(m_read_offset, bl);
     ceph::encode(m_read_len, bl);
     ceph::encode(m_pool_name, bl);
     ceph::encode(m_image_name, bl);
     ceph::encode(m_oid, bl);
   }

   void decode(bufferlist& bl) {
     auto i = bl.cbegin();
     ceph::decode(m_image_size, i);
     ceph::decode(m_read_offset, i);
     ceph::decode(m_read_len, i);
     ceph::decode(m_pool_name, i);
     ceph::decode(m_image_name, i);
     ceph::decode(m_oid, i);
   }
};

class ObjectCacheRequest {
public:
    ObjectCacheMsgHeader m_head;
    ObjectCacheMsgMiddle m_mid;

    bufferlist m_head_buffer;
    bufferlist m_mid_buffer;
    bufferlist m_data_buffer;

    ObjectCacheRequest() {}
    ~ObjectCacheRequest() {}

    void encode() {
      m_mid.encode(m_mid_buffer);

      m_head.mid_len = m_mid_buffer.length();
      m_head.data_len = m_data_buffer.length();

      assert(m_head_buffer.length() == 0);
      ::encode(m_head, m_head_buffer);
      assert(sizeof(ObjectCacheMsgHeader) == m_head_buffer.length());
    }

    bufferlist get_head_buffer() {
      return m_head_buffer;
    }

    bufferlist get_mid_buffer() {
      return m_mid_buffer;
    }

    bufferlist get_data_buffer() {
      return m_data_buffer;
    }
};

// currently, just use this interface.
inline ObjectCacheRequest* decode_object_cache_request(
            ObjectCacheMsgHeader* head, bufferlist mid_buffer)
{
  ObjectCacheRequest* req = new ObjectCacheRequest();

  // head
  req->m_head = *head;
  assert(req->m_head.mid_len == mid_buffer.length());

  // mid
  req->m_mid.decode(mid_buffer);

  return req;
}

inline ObjectCacheRequest* decode_object_cache_request(
             ObjectCacheMsgHeader* head, bufferlist& mid_buffer,
             bufferlist& data_buffer)
{
  ObjectCacheRequest* req = decode_object_cache_request(head, mid_buffer);

  // data
  if(data_buffer.length() != 0) {
    req->m_data_buffer = data_buffer;
  }

  return req;
}

inline ObjectCacheRequest* decode_object_cache_request(bufferlist& head,
                bufferlist& mid_buffer, bufferlist& data_buffer)
{
  assert(sizeof(ObjectCacheMsgHeader) == head.length());
  return decode_object_cache_request((ObjectCacheMsgHeader*)(head.c_str()), mid_buffer, data_buffer);
}

} // namespace immutable_obj_cache
} // namespace ceph
#endif
