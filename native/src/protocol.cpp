#include "synergy/protocol.hpp"
#include <algorithm>
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
        if(data.size()>MaxFrame+4||buffer_.size()+data.size()>2*(MaxFrame+4))throw std::runtime_error("Receive buffer limit");
        buffer_.insert(buffer_.end(),data.begin(),data.end());
        std::vector<Bytes> out;
        size_t pos=0;
        while(buffer_.size()-pos>=4){
            if(buffer_[pos]==0x16&&buffer_[pos+1]==0x03)throw std::runtime_error("TLS server detected; this classic-protocol client does not downgrade TLS");
            auto n=u32(buffer_,pos);
            if(n<4||n>MaxFrame)throw std::runtime_error("Invalid protocol frame length");
            if(buffer_.size()-pos<static_cast<size_t>(n)+4)break;
            out.emplace_back(buffer_.begin()+static_cast<ptrdiff_t>(pos+4),buffer_.begin()+static_cast<ptrdiff_t>(pos+4+n));
            pos+=n+4;
        }
        if(pos)buffer_.erase(buffer_.begin(),buffer_.begin()+static_cast<ptrdiff_t>(pos));
        return out;
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
    int linux_key(uint16_t id,uint16_t button){
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
    Session::Session(Config c,Input& in,std::function<void(const Bytes&)> send,std::function<void(Action)> action):config_(std::move(c)),input_(in),send_(std::move(send)),action_(std::move(action)){
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
            if(wanted&&!have){
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
                if(f[0]==4&&alt_.empty())alt_due_.reset();
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
            if(!is_alt(c)&&keys_.contains(c))input_.key(c,2);
            return;
        }
        for(auto f:families)if(c==f[1]||c==f[2]){
            if(value==1){
                set_key(c,true);
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
                    bool first=alt_.empty();
                    alt_.insert(c);
                    if(first){
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
    void Session::tick(int64_t now){
        if(!active_)return;
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
                synthetic_.erase(c);
            }
            action_(Action::Home);
        }
        if(config_.cursor_keepalive&&now-last_motion_>=2000){
            input_.move(1,0);
            input_.move(-1,0);
            last_motion_=now;
        }
    }
    void Session::release()noexcept{
        for(int c:keys_)try{
            input_.key(c,0);
        }
        catch(...){
        }
        for(int c:buttons_)try{
            input_.button(c,0);
        }
        catch(...){
        }
        keys_.clear();
        buttons_.clear();
        meta_.clear();
        alt_.clear();
        synthetic_.clear();
        home_due_.reset();
        alt_due_.reset();
        position_.reset();
        active_=false;
        tab_consumed_=home_fired_=alt_fired_=false;
        wheel_x_=wheel_y_=0;
    }
    bool Session::handle(const Bytes& p,int64_t now){
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
            active_=true;
            position_={
                {
                    u16(p,4),u16(p,6)}
            }
            ;
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
        if(cmd=="DMMV"||cmd=="DMRM"||cmd=="DMWM"){
            length(p,8);
            if(active_){
                if(cmd=="DMMV"){
                    int x=u16(p,4),y=u16(p,6);
                    if(position_)input_.move(x-position_->first,y-position_->second);
                    position_={
                        {
                            x,y}
                    }
                    ;
                    last_motion_=now;
                }
                else if(cmd=="DMRM"){
                    int x=s16(p,4),y=s16(p,6);
                    input_.move(x,y);
                    if(position_){
                        position_->first=std::clamp(position_->first+x,0,config_.width-1);
                        position_->second=std::clamp(position_->second+y,0,config_.height-1);
                    }
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
            if(p[4]>5||p[4]==0)throw std::runtime_error("Invalid mouse button");
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
            if(active_)key_event(u16(p,4),u16(p,6),u16(p,cmd=="DKRP"?10:8),cmd=="DKDN"?1:cmd=="DKUP"?0:2,now);
            command("CNOP");
            return true;
        }
        throw std::runtime_error("Unsupported Synergy command (connection closed safely)");
    }
}
