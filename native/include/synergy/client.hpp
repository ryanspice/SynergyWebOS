#pragma once
#include "synergy/platform.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <deque>
namespace synergy {
class Client {
    // Per-attempt recovery observation.  Stability is measured from the moment
    // the server protocol first became ready, never from the attempt start, so
    // a slow failed connect or handshake cannot reset the retry progression.
    struct Attempt {
        std::optional<int64_t> ready_at;
    };
    Platform& platform_;
    mutable std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> events_{0};
    std::atomic<size_t> ingress_peak_{0};
    std::atomic<uint64_t> attempts_{0};
    std::atomic<int64_t> retry_delay_ms_{0};
    std::thread worker_;
    Config active_;
    std::string state_="stopped",error_;
    std::deque<std::string> log_;
    bool have_active_=false;
    int wake_=-1;
    void state(std::string,std::string error={});
    void loop(Config,bool once);
    void session(const Config&,Attempt&);
public:
    explicit Client(Platform&);
    ~Client();
    void start(const Config&,bool once=false);
    void stop();
    bool running()const{return running_.load();}
    Json snapshot()const;
};
}
