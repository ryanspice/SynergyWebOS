#include "synergy/protocol.hpp"
#include <algorithm>
#include <cstdio>
#include <array>
#include <map>
#include <string_view>
#include <utility>
namespace synergy {
    namespace {
        uint16_t u16(const Bytes& p,size_t a){
            return static_cast<uint16_t>((static_cast<unsigned>(p.at(a))<<8)|p.at(a+1));
        }
        uint32_t u32(std::span<const uint8_t> p,size_t a){
            return (uint32_t(p[a])<<24)|(uint32_t(p[a+1])<<16)|(uint32_t(p[a+2])<<8)|p[a+3];
        }
        int s16(const Bytes& p,size_t a){
            auto v=u16(p,a);
            return v<32768?v:static_cast<int>(v)-65536;
        }
        void put16(Bytes& p,int n){
            p.push_back(static_cast<uint8_t>((n>>8)&255));
            p.push_back(static_cast<uint8_t>(n&255));
        }
        void put32(Bytes& p,uint32_t n){
            for(int s=24;s>=0;s-=8)p.push_back(static_cast<uint8_t>(n>>s));
        }
        Bytes bytes(std::string_view s){
            return {
                s.begin(),s.end()}
            ;
        }
        void length(const Bytes& p,size_t n){
            if(p.size()!=n)throw std::runtime_error("Malformed "+std::string(p.begin(),p.begin()+std::min<size_t>(4,p.size()))+" frame");
        }
        bool is_alt(int c){
            return c==56||c==100;
        }
        bool is_meta(int c){
            return c==125||c==126;
        }
        const std::array<std::array<int,3>,3> families{
            {
                {
                    {
                        1,42,54}
                }
                ,{
                    {
                        2,29,97}
                }
                ,{
                    {
                        4,56,100}
                }
            }
        }
        ;
    }
    std::vector<Bytes> FrameDecoder::feed(std::span<const uint8_t> data){
        if(data.size()>MaxFrame+4)throw std::runtime_error("Receive buffer limit");
        std::vector<Bytes> out;
        consume(data,[&](Bytes&& payload){out.push_back(std::move(payload));},2*(MaxFrame+4));
        return out;
    }
    size_t FrameDecoder::consume(std::span<const uint8_t> data,const std::function<void(Bytes&&)>& emit,size_t budget){
        if(buffer_.capacity()>budget)throw std::runtime_error("Receive byte budget exceeded");
        size_t completed=0;
        while(!data.empty()){
            if(prefix_used_<8){
                const auto count=std::min(data.size(),(prefix_used_<4?4:8)-prefix_used_);
                std::copy_n(data.begin(),count,prefix_.begin()+static_cast<ptrdiff_t>(prefix_used_));
                prefix_used_+=count;data=data.subspan(count);
                if(prefix_used_<4)continue;
                if(prefix_[0]==0x16&&prefix_[1]==0x03)throw std::runtime_error("TLS server detected; this classic-protocol client does not downgrade TLS");
                const auto size=u32(prefix_,0);
                if(size<4||size>MaxFrame)throw std::runtime_error("Invalid protocol frame length");
                if(prefix_used_<8)continue;
                const std::string_view command(reinterpret_cast<const char*>(prefix_.data()+4),4);
                discarding_=discard_ignored_&&!first_&&(command=="DCLP"||command=="CCLP"||command=="CSEC");
                remaining_=size-4;
                if(!discarding_){
                    if(size>budget)throw std::runtime_error("Receive byte budget exceeded");
                    buffer_.reserve(size);
                    if(buffer_.capacity()>budget)throw std::runtime_error("Receive byte budget exceeded");
                    buffer_.assign(prefix_.begin()+4,prefix_.end());
                }
            }
            const auto count=std::min(remaining_,data.size());
            if(!discarding_)buffer_.insert(buffer_.end(),data.begin(),data.begin()+static_cast<ptrdiff_t>(count));
            remaining_-=count;data=data.subspan(count);
            if(remaining_)continue;
            if(!discarding_){
                budget-=buffer_.capacity();
                emit(std::move(buffer_));
                buffer_=Bytes{};
            }
            ++completed;first_=false;prefix_used_=0;discarding_=false;
        }
        return completed;
    }
    Bytes frame(const Bytes& p){
        if(p.size()<4||p.size()>FrameDecoder::MaxFrame)throw std::runtime_error("Invalid outgoing frame");
        Bytes out(p.size()+4);
        const auto n=static_cast<uint32_t>(p.size());
        out[0]=static_cast<uint8_t>(n>>24);
        out[1]=static_cast<uint8_t>(n>>16);
        out[2]=static_cast<uint8_t>(n>>8);
        out[3]=static_cast<uint8_t>(n);
        std::copy(p.begin(),p.end(),out.begin()+4);
        return out;
    }
    // Synergy semantic controls use Linux input-event codes, before scan-code fallback.
    int linux_key(uint16_t id,uint16_t button){
        static const std::map<uint16_t,int> semantic{
            {0xe0a6,158},{0xe0a7,159},{0xe0a8,173},{0xe0a9,128},
            {0xe0aa,217},{0xe0ab,156},{0xe0ac,172},{0xe0ad,113},
            {0xe0ae,114},{0xe0af,115},{0xe0b0,163},{0xe0b1,165},
            {0xe0b2,166},{0xe0b3,164},{0xe0b4,155},{0xe0b5,226},
            {0xef95,71},{0xef96,75},{0xef97,72},{0xef98,77},
            {0xef99,80},{0xef9a,73},{0xef9b,81},{0xef9c,79},
            {0xef9d,76},{0xef9e,82},{0xef9f,83},
            {0xefaa,55},{0xefab,78},{0xefac,121},{0xefad,74},
            {0xefae,83},{0xefaf,98},{0xefbd,117}
        };
        if(auto it=semantic.find(id);it!=semantic.end())return it->second;
        if(id>=0xefb0&&id<=0xefb9){
            static constexpr int keypad[]={82,79,80,81,75,76,77,71,72,73};
            return keypad[id-0xefb0];
        }
        if(id>=0xefca&&id<=0xefd5)return 183+(id-0xefca);
        // Reserved semantic controls must not become unrelated physical letters.
        if((id&0xff00)==0xe000)return 0;
        static const std::map<uint16_t,int> specials{
            {
                0xef08,14}
            ,{
                0xef09,15}
            ,{
                0xef0d,28}
            ,{
                0xef13,119}
            ,{
                0xef14,70}
            ,{
                0xef1b,1}
            ,{
                0xef50,102}
            ,{
                0xef51,105}
            ,{
                0xef52,103}
            ,{
                0xef53,106}
            ,{
                0xef54,108}
            ,{
                0xef55,104}
            ,{
                0xef56,109}
            ,{
                0xef57,107}
            ,{
                0xef61,99}
            ,{
                0xef63,110}
            ,{
                0xef67,127}
            ,{
                0xef7f,69}
            ,{
                0xef8d,96}
            ,{
                0xefe1,42}
            ,{
                0xefe2,54}
            ,{
                0xefe3,29}
            ,{
                0xefe4,97}
            ,{
                0xefe5,58}
            ,{
                0xefe7,125}
            ,{
                0xefe8,126}
            ,{
                0xefe9,56}
            ,{
                0xefea,100}
            ,{
                0xefeb,125}
            ,{
                0xefec,126}
            ,{
                0xefff,111}
        }
        ;
        if(auto it=specials.find(id);it!=specials.end())return it->second;
        if(id>=0xefbe&&id<=0xefc7)return 59+(id-0xefbe);
        if(id==0xefc8)return 87;
        if(id==0xefc9)return 88;
        if((id&0xff00)==0xef00)return 0;
        static const std::map<uint16_t,int> ext{
            {
                0x1c,96}
            ,{
                0x1d,97}
            ,{
                0x35,98}
            ,{
                0x37,99}
            ,{
                0x38,100}
            ,{
                0x47,102}
            ,{
                0x48,103}
            ,{
                0x49,104}
            ,{
                0x4b,105}
            ,{
                0x4d,106}
            ,{
                0x4f,107}
            ,{
                0x50,108}
            ,{
                0x51,109}
            ,{
                0x52,110}
            ,{
                0x53,111}
            ,{
                0x5b,125}
            ,{
                0x5c,126}
            ,{
                0x5d,127}
        }
        ;
        if((button&0xff00)==0xe000||(button&0xff00)==0x0100){
            auto it=ext.find(button&255);
            return it==ext.end()?0:it->second;
        }
        if(button>0&&button<=0x58)return button;
        if(id>='A'&&id<='Z')id=static_cast<uint16_t>(id+32);
        const std::array<std::pair<std::string_view,int>,4> rows{
            {
                {
                    "1234567890-=",2}
                ,{
                    "qwertyuiop[]",16}
                ,{
                    "asdfghjkl;'`",30}
                ,{
                    "zxcvbnm,./",44}
            }
        }
        ;
        for(auto [s,start]:rows){
            auto p=s.find(static_cast<char>(id));
            if(id<128&&p!=std::string_view::npos)return start+static_cast<int>(p);
        }
        if(id==' ')return 57;
        if(id=='\\')return 43;
        if(id==8)return 14;
        if(id==9)return 15;
        if(id==13)return 28;
        return 0;
    }
    Session::Session(Config c,Input& in,std::function<void(const Bytes&)> send,std::function<void(Action)> action,std::function<void()> cycle_reset):config_(std::move(c)),input_(in),send_(std::move(send)),action_(std::move(action)),cycle_reset_(std::move(cycle_reset)){
    }
    void Session::command(const char* s){
        send_(bytes(s));
    }
    void Session::set_key(int c,bool down){
        if(down){
            if(keys_.insert(c).second)input_.key(c,1);
        }
        else if(keys_.erase(c))input_.key(c,0);
    }
    void Session::modifiers(uint16_t mask,int64_t now){
        for(auto f:families){
            bool wanted=(mask&f[0])!=0;
            bool have=keys_.contains(f[1])||keys_.contains(f[2]);
            if(wanted&&!have&&!(f[0]==4&&alt_fired_)){
                set_key(f[1],true);
                synthetic_.insert(f[1]);
                if(f[0]==4){
                    alt_.insert(f[1]);
                    alt_due_=now+5000;
                }
            }
            if(!wanted){
                for(int i=1;i<=2;++i)if(synthetic_.erase(f[i])){
                    set_key(f[i],false);
                    alt_.erase(f[i]);
                }
                if(f[0]==4&&alt_.empty()){
                    alt_due_.reset();
                    alt_fired_=false;
                }
            }
        }
        // Meta is always consumed; entry masks must never launch Home by themselves.
    }
    void Session::key_event(uint16_t id,uint16_t mods,uint16_t button,int value,int64_t now){
        int c=linux_key(id,button);
        if(!c)return;
        if(is_meta(c)){
            if(value==1){
                bool first=meta_.empty();
                meta_.insert(c);
                if(first&&!home_fired_&&config_.home_target=="system")home_due_=now+config_.home_delay_ms;
            }
            if(value==0){
                meta_.erase(c);
                if(meta_.empty()){
                    home_due_.reset();
                    home_fired_=false;
                }
            }
            tick(now);
            return;
        }
        if(c==15&&(!alt_.empty()||tab_consumed_)){
            if(value==1&&!tab_consumed_&&!alt_.empty()){
                tab_consumed_=true;
                alt_due_.reset();
                action_((mods&1)?Action::CycleBackward:Action::CycleForward);
            }
            if(value==0)tab_consumed_=false;
            return;
        }
        if(value==2){
            const bool media_toggle=id==0xe0ad||(id>=0xe0b0&&id<=0xe0b3);
            if(!media_toggle&&!is_alt(c)&&keys_.contains(c))input_.key(c,2);
            return;
        }
        for(auto f:families)if(c==f[1]||c==f[2]){
            if(value==1){
                // A Home-consumed hold stays physical-only until all Alt keys release.
                if(!is_alt(c)||!alt_fired_)set_key(c,true);
                for(int i=1;i<=2;++i){
                    if(f[i]!=c&&synthetic_.erase(f[i])){
                        set_key(f[i],false);
                        alt_.erase(f[i]);
                    }
                }
                synthetic_.erase(c);
            }
            else {
                for(int i=1;i<=2;++i)if(synthetic_.erase(f[i])){
                    set_key(f[i],false);
                    alt_.erase(f[i]);
                }
                set_key(c,false);
            }
            if(is_alt(c)){
                if(value==1){
                    const bool first=alt_.empty();
                    alt_.insert(c);
                    if(first&&!alt_fired_){
                        if(cycle_reset_)cycle_reset_();
                        alt_due_=now+5000;
                        alt_fired_=false;
                    }
                }
                else{
                    alt_.erase(c);
                    if(alt_.empty()){
                        alt_due_.reset();
                        alt_fired_=false;
                    }
                }
            }
            return;
        }
        if(value==1)modifiers(mods,now);
        set_key(c,value==1);
    }
    int64_t Session::next_deadline(int64_t fallback)const{
        if(!active_)return fallback;
        fallback=next_gamepad_deadline(fallback);
        if(home_due_)fallback=std::min(fallback,*home_due_);
        if(alt_due_)fallback=std::min(fallback,*alt_due_);
        if(config_.cursor_keepalive)fallback=std::min(fallback,last_motion_+2000);
        return fallback;
    }
    int64_t Session::next_gamepad_deadline(int64_t fallback)const{
        for(const auto& seen:gamepad_seen_)if(seen)fallback=std::min(fallback,*seen+500);
        return fallback;
    }
    void Session::tick(int64_t now){
        if(!active_)return;
        expire_gamepads(now);
        if(home_due_&&now>=*home_due_&&!meta_.empty()){
            home_due_.reset();
            home_fired_=true;
            action_(Action::Home);
        }
        if(alt_due_&&now>=*alt_due_&&!alt_.empty()&&!alt_fired_){
            alt_due_.reset();
            alt_fired_=true;
            for(int c:alt_){
                set_key(c,false);
                // Keep inherited provenance so a cleared mask can end this hold.
            }
            action_(Action::Home);
        }
        if(config_.cursor_keepalive&&now-last_motion_>=2000){
            input_.nudge();
            last_motion_=now;
        }
    }
    void Session::expire_gamepads(int64_t now){
        for(size_t slot=0;slot<gamepad_seen_.size();++slot){
            if(gamepad_seen_[slot]&&now-*gamepad_seen_[slot]>=500){
                input_.gamepad(static_cast<uint8_t>(slot),bridge::GamepadState{});
                gamepad_seen_[slot].reset();
            }
        }
    }
    void Session::release()noexcept{
        for(size_t slot=0;slot<gamepad_seen_.size();++slot){
            if(gamepad_seen_[slot]){
                try{input_.gamepad(static_cast<uint8_t>(slot),bridge::GamepadState{});}
                catch(...){std::fprintf(stderr,"synergy: gamepad neutralization failed\n");}
                gamepad_seen_[slot].reset();
            }
        }
        for(int c:keys_)try{
            input_.key(c,0);
        }
        catch(const std::exception& e){
            std::fprintf(stderr,"synergy: key release failed: %s\n",e.what());
        }
        catch(...){
            std::fprintf(stderr,"synergy: key release failed\n");
        }
        for(int c:buttons_)try{
            input_.button(c,0);
        }
        catch(const std::exception& e){
            std::fprintf(stderr,"synergy: button release failed: %s\n",e.what());
        }
        catch(...){
            std::fprintf(stderr,"synergy: button release failed\n");
        }
        keys_.clear();
        buttons_.clear();
        meta_.clear();
        alt_.clear();
        synthetic_.clear();
        home_due_.reset();
        alt_due_.reset();
        position_.reset();
        repeat_.remaining=0;
        active_=false;
        tab_consumed_=home_fired_=alt_fired_=false;
        wheel_x_=wheel_y_=0;
    }
    unsigned Session::pump(int64_t now,unsigned limit){
        unsigned sent=0;
        // Each repeat re-checks live state, so a key released or a capture left
        // mid-batch stops producing events exactly as before.
        while(sent<limit&&repeat_.remaining>0&&active_){
            --repeat_.remaining;
            ++sent;
            key_event(repeat_.id,repeat_.mods,repeat_.button,2,now);
        }
        if(!active_)repeat_.remaining=0;
        return sent;
    }
    bool Session::handle(const Bytes& p,int64_t now,int64_t processing_now){
        if(processing_now<0)processing_now=now;
        while(pending())pump(now,repeat_.remaining);
        if(!hello_){
            if(p.size()!=11||!(std::equal(p.begin(),p.begin()+7,"Synergy")||std::equal(p.begin(),p.begin()+7,"Barrier")))throw std::runtime_error("Expected Synergy/Barrier hello");
            if(u16(p,7)!=1||u16(p,9)<4)throw std::runtime_error("Requires classic Synergy protocol 1.4 or newer compatible server");
            Bytes r(p.begin(),p.begin()+7);
            put16(r,1);
            put16(r,4);
            put32(r,static_cast<uint32_t>(config_.name.size()));
            r.insert(r.end(),config_.name.begin(),config_.name.end());
            send_(r);
            hello_=true;
            return true;
        }
        if(p.size()<4)throw std::runtime_error("Short command");
        const std::string cmd(p.begin(),p.begin()+4);
        if(cmd=="EICV"||cmd=="EBSY"||cmd=="EUNK"||cmd=="EBAD")throw std::runtime_error("Synergy server rejected client: "+cmd);
        if(cmd=="QINF"){
            length(p,4);
            Bytes r=bytes("DINF");
            for(int n:{
                0,0,config_.width,config_.height,0,0,0}
            )put16(r,n);
            send_(r);
            info_=true;
            return true;
        }
        if(cmd=="CALV"){
            length(p,4);
            command("CALV");
            command("CNOP");
            return true;
        }
        if(cmd=="CBYE"){
            length(p,4);
            release();
            return false;
        }
        if(cmd=="CIAK"||cmd=="CROP"||cmd=="CNOP"){
            length(p,4);
            return true;
        }
        if(cmd=="DSOP"){
            if(p.size()<8||u32(p,4)>(FrameDecoder::MaxFrame-8)/4||p.size()!=8+static_cast<size_t>(u32(p,4))*4)throw std::runtime_error("Malformed options");
            command("CNOP");
            return true;
        }
        if(cmd=="DCLP"||cmd=="CCLP"||cmd=="CSEC")return true;
        // deliberately no clipboard or screen-power injection
        if(cmd=="CINN"){
            length(p,14);
            if(!info_)throw std::runtime_error("Enter before screen negotiation");
            release();
            capture_=u32(p,8);
            active_=true;
            position_={
                {
                    u16(p,4),u16(p,6)}
            }
            ;
            const int entry_x=std::clamp(position_->first,0,config_.width-1);
            const int entry_y=std::clamp(position_->second,0,config_.height-1);
            if(input_.position(entry_x,entry_y))position_={{entry_x,entry_y}};
            last_motion_=now;
            modifiers(u16(p,12),now);
            command("CNOP");
            return true;
        }
        if(cmd=="COUT"){
            length(p,4);
            release();
            command("CNOP");
            return true;
        }
        if(cmd=="DGPD"){
            if(!config_.input_bridge)throw std::runtime_error("Gamepad requires authenticated input bridge");
            const auto decoded=bridge::decode_gamepad(p);
            if(!decoded)throw std::runtime_error("Malformed gamepad packet");
            const auto& packet=*decoded;
            if(packet.sequence<=gamepad_sequence_){command("CNOP");return true;}
            gamepad_sequence_=packet.sequence;
            if(!info_||!active_||packet.capture!=capture_||processing_now-now>=250){
                command("CNOP");return true;
            }
            // Mark before delivery so failure cleanup also tries to neutralize
            // a partially written hardware report.
            if(packet.state.connected)gamepad_seen_[packet.slot]=now;
            if(!input_.gamepad(packet.slot,packet.state)&&!config_.dry_run)
                throw std::runtime_error("Gamepad input backend unavailable");
            if(!packet.state.connected)gamepad_seen_[packet.slot].reset();
            command("CNOP");return true;
        }
        if(cmd=="DMMV"||cmd=="DMRM"||cmd=="DMWM"){
            length(p,8);
            if(active_){
                if(cmd=="DMMV"){
                    int x=u16(p,4),y=u16(p,6);
                    const int absolute_x=std::clamp(x,0,config_.width-1);
                    const int absolute_y=std::clamp(y,0,config_.height-1);
                    const bool positioned=input_.position(absolute_x,absolute_y);
                    if(!positioned&&position_)
                        input_.move(x-position_->first,y-position_->second);
                    position_={
                        {
                            positioned?absolute_x:x,positioned?absolute_y:y}
                    }
                    ;
                    last_motion_=now;
                }
                else if(cmd=="DMRM"){
                    int x=s16(p,4),y=s16(p,6);
                    if(position_){
                        int next_x=std::clamp(position_->first+x,0,config_.width-1);
                        int next_y=std::clamp(position_->second+y,0,config_.height-1);
                        if(!input_.position(next_x,next_y))input_.move(x,y);
                        position_->first=next_x;
                        position_->second=next_y;
                    }
                    else input_.move(x,y);
                    last_motion_=now;
                }
                else{
                    wheel_x_+=s16(p,4);
                    wheel_y_+=s16(p,6);
                    int x=wheel_x_/120,y=wheel_y_/120;
                    wheel_x_%=120;
                    wheel_y_%=120;
                    if(x||y)input_.wheel(x,y);
                }
            }
            command("CNOP");
            return true;
        }
        if(cmd=="DMDN"||cmd=="DMUP"){
            length(p,5);
            static const int codes[]={
                0,0x110,0x112,0x111,0x113,0x114}
            ;
            if(p[4]==0)throw std::runtime_error("Invalid mouse button");
            // Unknown, well-formed button IDs are ignored without ending the session.
            if(p[4]>=sizeof(codes)/sizeof(codes[0])){
                command("CNOP");
                return true;
            }
            if(active_){
                int c=codes[p[4]];
                if(cmd=="DMDN"){
                    if(buttons_.insert(c).second)input_.button(c,1);
                }
                else if(buttons_.erase(c))input_.button(c,0);
            }
            command("CNOP");
            return true;
        }
        if(cmd=="DKDN"||cmd=="DKUP"||cmd=="DKRP"){
            length(p,cmd=="DKRP"?12:10);
            if(active_){
                if(cmd=="DKRP"){
                    // The wire's uint16 count bounds delivery; a zero batch does nothing.
                    repeat_={u16(p,4),u16(p,6),u16(p,10),u16(p,8)};
                    if(!defer_repeats_)while(pending())pump(now,repeat_.remaining);
                }
                else key_event(u16(p,4),u16(p,6),u16(p,8),cmd=="DKDN"?1:0,now);
            }
            command("CNOP");
            return true;
        }
        throw std::runtime_error("Unsupported Synergy command (connection closed safely)");
    }
}
