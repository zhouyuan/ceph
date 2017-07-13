// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

namespace librbd {
namespace cache {
namespace file {

class Controller {

    bool LOG_STATS_ENABLE = false;
    cpp_redis::redis_client* redisclient = nullptr;

public:
    Controller(bool enable_stats) {

        if (enable_stats) {
            LOG_STATS_ENABLE = enable_stats;
            redisclient = new cpp_redis::redis_client();
            redisclient->connect("127.0.0.1", 6379, [](cpp_redis::redis_client&) {
                //TODO: disable stats if redis not avaiable
                std::cout << "client disconnected (disconnection handler)" << std::endl;
            });
        }
    }

    ~Controller() {
        if (redisclient) {
            delete redisclient;
        }
    }
    void lookup_from_redis(std::string key) {
        if( (!LOG_STATS_ENABLE) || (!redisclient) )
            return;

        redisclient->get(key, [](cpp_redis::reply& reply) {
        // if (reply.is_string())
        //   do_something_with_string(reply.as_string())
        });
        redisclient->commit();
    }

    void updates_to_redis(std::string key, std::string value) {
        if( (!LOG_STATS_ENABLE) || (!redisclient) )
            return;

        redisclient->set(key, value, [](cpp_redis::reply& reply) {
        // if (reply.is_string())
        //   do_something_with_string(reply.as_string())
        });
        redisclient->commit();
    }

};

} // namespace file
} // namespace cache
} // namespace librbd


