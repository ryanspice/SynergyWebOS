#pragma once
#include "synergy/config.hpp"
#include "synergy/gamepad_wire.hpp"
#include <functional>
#include <array>
#include <optional>
#include <set>
#include <span>
#include <vector>
namespace synergy {
using Bytes=std::vector<uint8_t>;
constexpr int ControlSchema=1;
class FrameDecoder {
    Bytes buffer_;
    std::array<uint8_t,8> prefix_{};
    size_t prefix_used_=0,remaining_=0;
    bool discard_ignored_=false,discarding_=false,first_=true;
public:
    static constexpr size_t MaxFrame=1024*1024;
    explicit FrameDecoder(bool discard_ignored=false):discard_ignored_(discard_ignored){}
    std::vector<Bytes> feed(std::span<const uint8_t>);
    // Emits one owned payload at a time. Budget includes all emitted vector
    // capacities plus any incomplete retained payload in this call.
    size_t consume(std::span<const uint8_t>,const std::function<void(Bytes&&)>&,size_t budget);
    size_t retained_bytes()const{return buffer_.capacity();}
    bool partial()const{return prefix_used_!=0;}
    void clear(){buffer_=Bytes{};prefix_used_=remaining_=0;discarding_=false;first_=true;}
};
Bytes frame(const Bytes&);
struct Input {
    virtual ~Input()=default;
    virtual void key(int,int)=0;
    virtual void button(int,int)=0;
    virtual void move(int,int)=0;
    virtual bool position(int,int){return false;}
    virtual void nudge(){move(1,0);move(-1,0);}
    virtual void wheel(int,int)=0;
    virtual bool gamepad(uint8_t,const bridge::GamepadState&){return false;}
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
    uint32_t capture_=0;
    uint64_t gamepad_sequence_=0;
    std::array<std::optional<int64_t>,4> gamepad_seen_{};
    // A deferred DKRP batch. Its repeats precede every later packet.
    struct Repeat {uint16_t id=0,mods=0,button=0;unsigned remaining=0;} repeat_;
    bool defer_repeats_=false;
    void command(const char*);
    void modifiers(uint16_t,int64_t);
    void key_event(uint16_t,uint16_t,uint16_t,int,int64_t);
    void set_key(int,bool);
public:
    Session(Config,Input&,std::function<void(const Bytes&)>,std::function<void(Action)>,std::function<void()> cycle_reset={});
    ~Session(){release();}
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    bool handle(const Bytes&,int64_t now,int64_t processing_now=-1);
    // With deferral, handle() queues a DKRP batch; pump() delivers at most
    // `limit` repeats so the caller can check Stop/timers between chunks.
    // handle() drains any remainder first, preserving wire order.
    void defer_repeats(bool on){defer_repeats_=on;}
    bool pending()const{return repeat_.remaining>0;}
    unsigned pump(int64_t now,unsigned limit);
    void tick(int64_t now);
    void expire_gamepads(int64_t now);
    int64_t next_deadline(int64_t fallback)const;
    int64_t next_gamepad_deadline(int64_t fallback)const;
    void release()noexcept;
    bool ready()const{return info_;}
    bool active()const{return active_;}
    size_t pressed()const{return keys_.size()+buttons_.size();}
};
}
