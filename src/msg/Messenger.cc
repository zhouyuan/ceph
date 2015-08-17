// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include <arpa/inet.h>
#include <netdb.h>

#include "include/types.h"
#include "Messenger.h"

#include "msg/simple/SimpleMessenger.h"
#include "msg/async/AsyncMessenger.h"
#ifdef HAVE_XIO
#include "msg/xio/XioMessenger.h"
#endif

Messenger *Messenger::create(CephContext *cct, const string &type,
			     entity_name_t name, string lname,
			     uint64_t nonce)
{
  int r = -1;
  if (type == "random")
    r = rand() % 2; // random does not include xio
  if (r == 0 || type == "simple")
    return new SimpleMessenger(cct, name, lname, nonce);
  else if ((r == 1 || type == "async") &&
	   cct->check_experimental_feature_enabled("ms-type-async"))
    return new AsyncMessenger(cct, name, lname, nonce);
#ifdef HAVE_XIO
  else if ((type == "xio") &&
	   cct->check_experimental_feature_enabled("ms-type-xio"))
    return new XioMessenger(cct, name, lname, nonce);
#endif
  lderr(cct) << "unrecognized ms_type '" << type << "'" << dendl;
  return NULL;
}

void Messenger::set_endpoint_addr(const sockaddr_storage &ss, int port,
                                  const entity_name_t &name)
{
  size_t hostlen;
  if (ss.ss_family == AF_INET)
    hostlen = sizeof(struct sockaddr_in);
  else if (ss.ss_family == AF_INET6)
    hostlen = sizeof(struct sockaddr_in6);
  else
    hostlen = 0;

  if (hostlen) {
    char buf[NI_MAXHOST] = { 0 };
    getnameinfo((struct sockaddr *)&ss, hostlen, buf, sizeof(buf),
                NULL, 0, NI_NUMERICHOST);

    trace_endpoint.copy_ip(buf);
  }
  trace_endpoint.set_port(port);
}

/*
 * Pre-calculate desired software CRC settings.  CRC computation may
 * be disabled by default for some transports (e.g., those with strong
 * hardware checksum support).
 */
int Messenger::get_default_crc_flags(md_config_t * conf)
{
  int r = 0;
  if (conf->ms_crc_data)
    r |= MSG_CRC_DATA;
  if (conf->ms_crc_header)
    r |= MSG_CRC_HEADER;
  return r;
}
