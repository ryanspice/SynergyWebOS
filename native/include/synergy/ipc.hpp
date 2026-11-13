#pragma once
#include "synergy/config.hpp"
#include <functional>
#include <sys/types.h>
namespace synergy {
class Broker {
    int lock_=-1,socket_=-1;
    std::filesystem::path path_;
    uid_t controller_;
    ino_t inode_=0;
public:
    Broker(const std::filesystem::path&,uid_t controller);
    ~Broker();
    Broker(const Broker&)=delete;Broker& operator=(const Broker&)=delete;
    void poll_once(const std::function<Json(const Json&)>&,int timeout_ms=25,int wake_fd=-1);
};
Json request(const std::filesystem::path&,const Json&,int timeout_ms=2000);
}
