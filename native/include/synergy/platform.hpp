#pragma once
#include "synergy/protocol.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
namespace synergy {
struct ProcessResult {std::string output;int exit_code=-1;bool timed_out=false,too_large=false;};
ProcessResult bounded_process(const std::string&,const std::vector<std::string>&,int timeout_ms=2000,size_t limit=131072);
class Platform {
    struct Work{std::string kind;bool dry;uint64_t generation;};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Work> work_;
    bool stopping_=false;
    std::thread worker_;
    std::atomic<uint64_t> generation_{1};
    std::string status_="No TV action requested";
    std::vector<std::string> cycle_;
    int cycle_index_=-1;
    int64_t cycle_at_=0;
    uint64_t cycle_generation_=0;
    Json luna(const char*,const Json&);
    void run();
    void update(const std::string&);
    std::vector<std::string> running();
public:
    Platform();~Platform();
    uint64_t generation()const{return generation_.load();}
    void cancel(){++generation_;}
    bool post(std::string kind,bool dry=false,uint64_t generation=0);
    void reset_cycle(uint64_t generation);
    void action(Action,bool dry,uint64_t generation);
    Json snapshot()const;
};
}
