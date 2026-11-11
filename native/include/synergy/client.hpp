#pragma once
#include "synergy/platform.hpp"
#include <atomic>
#include <mutex>
#include <thread>
#include <deque>
namespace synergy {
class Client {
    Platform& platform_;
    mutable std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> events_{0};
    std::thread worker_;
    Config active_;
    std::string state_="stopped",error_;
    std::deque<std::string> log_;
    bool have_active_=false;
    void state(std::string,std::string error={});
    void loop(Config,bool once);
    void session(const Config&);
public:
    explicit Client(Platform& p):platform_(p){}
    ~Client(){stop();}
    void start(const Config&,bool once=false);
    void stop();
    bool running()const{return running_.load();}
    Json snapshot()const;
};
}
