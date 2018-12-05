// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "common/ceph_argparse.h"
#include "common/config.h"
#include "common/debug.h"
#include "common/errno.h"
#include "global/global_init.h"
#include "global/signal_handler.h"
#include "CacheController.h"

#include <vector>

rbd::cache::CacheController *cachectl = nullptr;

void usage() {
  std::cout << "usage: cache controller [options...]" << std::endl;
  std::cout << "options:\n";
  std::cout << "  -m monaddress[:port]      connect to specified monitor\n";
  std::cout << "  --keyring=<path>          path to keyring for local cluster\n";
  std::cout << "  --log-file=<logfile>       file to log debug output\n";
  std::cout << "  --debug-rbd-cachecontroller=<log-level>/<memory-level>  set rbd-mirror debug level\n";
  generic_server_usage();
}

static void handle_signal(int signum)
{
  if (cachectl)
    cachectl->handle_signal(signum);
}

int main(int argc, const char **argv)
{
  std::vector<const char*> args;
  env_to_vec(args);
  argv_to_vec(argc, argv, args);

  auto cct = global_init(nullptr, args, CEPH_ENTITY_TYPE_CLIENT,
			 CODE_ENVIRONMENT_DAEMON,
			 CINIT_FLAG_UNPRIVILEGED_DAEMON_DEFAULTS);

  for (auto i = args.begin(); i != args.end(); ++i) {
    if (ceph_argparse_flag(args, i, "-h", "--help", (char*)NULL)) {
      usage();
      return EXIT_SUCCESS;
    }
  }

  if (g_conf->daemonize) {
    global_init_daemonize(g_ceph_context);
  }
  g_ceph_context->enable_perf_counter();

  common_init_finish(g_ceph_context);

  init_async_signal_handler();
  register_async_signal_handler(SIGHUP, sighup_handler);
  register_async_signal_handler_oneshot(SIGINT, handle_signal);
  register_async_signal_handler_oneshot(SIGTERM, handle_signal);

  std::vector<const char*> cmd_args;
  argv_to_vec(argc, argv, cmd_args);

  // disable unnecessary librbd cache
  g_ceph_context->_conf->set_val_or_die("rbd_cache", "false");

  cachectl = new rbd::cache::CacheController(g_ceph_context, cmd_args);
  int r = cachectl->init();
  if (r < 0) {
    std::cerr << "failed to initialize: " << cpp_strerror(r) << std::endl;
    goto cleanup;
  }

  cachectl->run();

 cleanup:
  unregister_async_signal_handler(SIGHUP, sighup_handler);
  unregister_async_signal_handler(SIGINT, handle_signal);
  unregister_async_signal_handler(SIGTERM, handle_signal);
  shutdown_async_signal_handler();

  delete cachectl;

  return r < 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
