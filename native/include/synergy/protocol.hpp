#pragma once
#include "synergy/config.hpp"
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <vector>
namespace synergy {
using Bytes=std::vector<uint8_t>;
constexpr int ControlSchema=1;
class FrameDecoder {
    Bytes buffer_;
public:
    static constexpr size_t MaxFrame=1024*1024;
    std::vector<Bytes> feed(std::span<const uint8_t>);
    bool partial()const{return !buffer_.empty();}
    void clear(){buffer_.clear();}
};
Bytes frame(const Bytes&);
struct Input {
    virtual ~Input()=default;
    virtual void key(int,int)=0;
    virtual void button(int,int)=0;
    virtual void move(int,int)=0;
    virtual void wheel(int,int)=0;
};
enum class Action {Home,CycleForward,CycleBackward};
int linux_key(uint16_t id,uint16_t button);
class Session {
    Config config_;
    Input& input_;
    std::function<void(const Bytes&)> send_;
    std::function<void(Action)> action_;
    std::function<void()> cycle_reset_;
    bool hello_=false,info_=false,active_=false,tab_consumed_=false,home_fired_=false,alt_fired_=false;
    std::set<int> keys_,buttons_,meta_,alt_,synthetic_;
    std::optional<int64_t> home_due_,alt_due_;
    std::optional<std::pair<int,int>> position_;
    int wheel_x_=0,wheel_y_=0;
    int64_t last_motion_=0;
    void command(const char*);
    void modifiers(uint16_t,int64_t);
    void key_event(uint16_t,uint16_t,uint16_t,int,int64_t);
    void set_key(int,bool);
public:
    Session(Config,Input&,std::function<void(const Bytes&)>,std::function<void(Action)>,std::function<void()> cycle_reset={});
    ~Session(){release();}
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    bool handle(const Bytes&,int64_t now);
    void tick(int64_t now);
    void release()noexcept;
    bool ready()const{return info_;}
    bool active()const{return active_;}
    size_t pressed()const{return keys_.size()+buttons_.size();}
};
}
