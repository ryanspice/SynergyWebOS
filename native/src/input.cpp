#include "synergy/input.hpp"
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <initializer_list>
#include <utility>
#ifdef SYNERGY_WEBOS
#include <dlfcn.h>
#endif
namespace synergy {
    namespace {
#ifdef SYNERGY_WEBOS
        void request_device_scan(){
            // This firmware's input manager discovers virtual devices on an
            // explicit scan, not on their udev add notification. Request one
            // scan after both devices exist, including after TCP reconnects.
            // The zero-argument/int-result ABI was inspected and exercised on
            // the target's libim 1.0.0; it is not an official SDK header API.
            void* library=dlopen("/usr/lib/libim.so.1.0.0",RTLD_NOW|RTLD_LOCAL);
            if(!library)throw std::runtime_error("Cannot load webOS input discovery runtime");
            using RequestScan=int (*)();
            auto scan=reinterpret_cast<RequestScan>(dlsym(library,"IM_RequestToCheckInput"));
            if(!scan){
                dlclose(library);
                throw std::runtime_error("webOS input discovery API unavailable");
            }
            int result=scan();
            dlclose(library);
            if(result!=0)throw std::runtime_error("webOS input device scan request failed");
        }
#endif
        std::string device_path(){
            for(auto* s:{
                "/dev/uinput","/dev/input/uinput"}
            ){
                struct stat st{
                }
                ;
                if(stat(s,&st)==0&&S_ISCHR(st.st_mode))return s;
            }
            return {
            }
            ;
        }
        class Device {
            int fd_=-1;
            bool created_=false;
            void control(unsigned long op,int n){
                if(ioctl(fd_,op,n)<0)throw std::runtime_error("uinput capability ioctl failed");
            }
            void write_record(const void* data,size_t count){
                auto deadline=monotonic_ms()+100;
                for(;;){
                    ssize_t n=write(fd_,data,count);
                    if(n==static_cast<ssize_t>(count))return;
                    if(n<0&&errno==EINTR)continue;
                    if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)&&monotonic_ms()<deadline){
                        pollfd p{
                            fd_,POLLOUT,0}
                        ;
                        poll(&p,1,10);
                        continue;
                    }
                    throw std::runtime_error("uinput write failed or short record");
                }
            }
            public:
            Device(const std::string& path,const char* name,bool mouse,int width=1,int height=1,bool gamepad=false){
                fd_=open(path.c_str(),O_WRONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
                if(fd_<0)throw std::runtime_error("Cannot open uinput: permission/driver unavailable");
                try{
                    control(UI_SET_EVBIT,EV_KEY);
                    if(gamepad){
                        for(int k:{BTN_SOUTH,BTN_EAST,BTN_NORTH,BTN_WEST,BTN_TL,BTN_TR,
                                   BTN_SELECT,BTN_START,BTN_THUMBL,BTN_THUMBR})control(UI_SET_KEYBIT,k);
                        control(UI_SET_EVBIT,EV_ABS);
                        for(int axis:{ABS_X,ABS_Y,ABS_RX,ABS_RY,ABS_Z,ABS_RZ,ABS_HAT0X,ABS_HAT0Y})
                            control(UI_SET_ABSBIT,axis);
                    }
                    else if(mouse){
                        for(int k:{
                            BTN_LEFT,BTN_RIGHT,BTN_MIDDLE,BTN_SIDE,BTN_EXTRA}
                        )control(UI_SET_KEYBIT,k);
                        control(UI_SET_PROPBIT,INPUT_PROP_POINTER);
                        control(UI_SET_EVBIT,EV_ABS);
                        control(UI_SET_ABSBIT,ABS_X);
                        control(UI_SET_ABSBIT,ABS_Y);
                        control(UI_SET_EVBIT,EV_REL);
                        for(int r:{
                            REL_WHEEL,REL_HWHEEL}
                        )control(UI_SET_RELBIT,r);
                    }
                    else for(int k=1;k<256;++k)control(UI_SET_KEYBIT,k);
                    uinput_user_dev d{
                    }
                    ;
                    std::strncpy(d.name,name,sizeof(d.name)-1);
                    // Preserve the established client's USB identity. This
                    // classification alone does not trigger webOS discovery.
                    d.id.bustype=BUS_USB;
                    d.id.vendor=0x1d6b;
                    d.id.product=0x0104;
                    d.id.version=1;
                    if(gamepad){
                        for(int axis:{ABS_X,ABS_Y,ABS_RX,ABS_RY}){
                            d.absmin[axis]=-32768;d.absmax[axis]=32767;
                        }
                        d.absmax[ABS_Z]=d.absmax[ABS_RZ]=255;
                        d.absmin[ABS_HAT0X]=d.absmin[ABS_HAT0Y]=-1;
                        d.absmax[ABS_HAT0X]=d.absmax[ABS_HAT0Y]=1;
                    }
                    else if(mouse){
                        d.absmin[ABS_X]=d.absmin[ABS_Y]=0;
                        // A one-pixel configured screen still needs a nonzero
                        // evdev axis range; positions on that axis stay zero.
                        d.absmax[ABS_X]=std::max(width-1,1);
                        d.absmax[ABS_Y]=std::max(height-1,1);
                    }
                    write_record(&d,sizeof d);
                    if(ioctl(fd_,UI_DEV_CREATE)<0)throw std::runtime_error("uinput device creation failed");
                    created_=true;
                }
                catch(...){
                    if(created_)ioctl(fd_,UI_DEV_DESTROY);
                    close(fd_);
                    fd_=-1;
                    throw;
                }
            }
            ~Device(){
                if(fd_>=0){
                    if(created_)ioctl(fd_,UI_DEV_DESTROY);
                    close(fd_);
                }
            }
            Device(const Device&)=delete;
            Device& operator=(const Device&)=delete;
            // Writes whole input_event records, normally in one syscall. uinput
            // may accept a prefix of complete records; resume after it.
            void write_events(const input_event* events,size_t count){
                auto deadline=monotonic_ms()+100;
                size_t done=0;
                while(done<count){
                    ssize_t n=write(fd_,events+done,(count-done)*sizeof(input_event));
                    if(n>0&&static_cast<size_t>(n)%sizeof(input_event)==0){
                        done+=static_cast<size_t>(n)/sizeof(input_event);
                        continue;
                    }
                    if(n<0&&errno==EINTR)continue;
                    if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)&&monotonic_ms()<deadline){
                        pollfd p{
                            fd_,POLLOUT,0}
                        ;
                        poll(&p,1,10);
                        continue;
                    }
                    throw std::runtime_error("uinput write failed or short record");
                }
            }
            static input_event event(unsigned short type,unsigned short code,int value){
                input_event e{
                }
                ;
                e.type=type;
                e.code=code;
                e.value=value;
                return e;
            }
            // Emits up to two events followed by SYN_REPORT as one frame.
            void report(std::initializer_list<input_event> events){
                std::array<input_event,3> frame{
                }
                ;
                size_t n=0;
                for(const auto& e:events)if(n<2)frame[n++]=e;
                if(!n)return;
                frame[n++]=event(EV_SYN,SYN_REPORT,0);
                write_events(frame.data(),n);
            }
        }
        ;
        class UInput final:public Input {
            Device keyboard_,mouse_;
            std::string path_;
            std::array<std::unique_ptr<Device>,4> gamepads_{};
            int width_,height_,x_=0,y_=0;
            public:
            explicit UInput(const std::string& p,int width,int height):keyboard_(p,"SynergyWebOS Native Keyboard",false),mouse_(p,"SynergyWebOS Native Mouse",true,width,height),path_(p),width_(width),height_(height){
#ifdef SYNERGY_WEBOS
                request_device_scan();
#endif
            }
            void key(int c,int value)override{
                if(c<=0||c>=256||value<0||value>2)throw std::runtime_error("Invalid key event");
                keyboard_.report({Device::event(EV_KEY,static_cast<unsigned short>(c),value)});
            }
            void button(int c,int value)override{
                if(c<BTN_LEFT||c>BTN_EXTRA||(value!=0&&value!=1))throw std::runtime_error("Invalid button event");
                mouse_.report({Device::event(EV_KEY,static_cast<unsigned short>(c),value)});
            }
            bool position(int x,int y)override{
                x_=std::clamp(x,0,width_-1);
                y_=std::clamp(y,0,height_-1);
                mouse_.report({Device::event(EV_ABS,ABS_X,x_),Device::event(EV_ABS,ABS_Y,y_)});
                return true;
            }
            void move(int x,int y)override{
                if(!x&&!y)return;
                position(static_cast<int>(std::clamp(static_cast<long long>(x_)+x,0LL,static_cast<long long>(width_-1))),
                         static_cast<int>(std::clamp(static_cast<long long>(y_)+y,0LL,static_cast<long long>(height_-1))));
            }
            void nudge()override{
                int x=x_,y=y_;
                if(width_>1)position(x+(x<width_-1?1:-1),y);
                else if(height_>1)position(x,y+(y<height_-1?1:-1));
                else position(x,y);
                position(x,y);
            }
            void wheel(int x,int y)override{
                axes(REL_HWHEEL,x,REL_WHEEL,y);
            }
            bool gamepad(uint8_t slot,const bridge::GamepadState& incoming)override{
                if(slot>=gamepads_.size()||(incoming.buttons&~0xf3ffu))throw std::runtime_error("Invalid gamepad state");
                if(!incoming.connected&&(incoming.buttons||incoming.lx||incoming.ly||incoming.rx||incoming.ry||incoming.lt||incoming.rt))
                    throw std::runtime_error("Disconnected gamepad must be neutral");
                if(!gamepads_[slot]){
                    if(!incoming.connected)return true;
                    const auto name="SynergyWebOS Gamepad "+std::to_string(static_cast<unsigned>(slot)+1);
                    auto device=std::make_unique<Device>(path_,name.c_str(),false,1,1,true);
#ifdef SYNERGY_WEBOS
                    request_device_scan();
#endif
                    gamepads_[slot]=std::move(device);
                }
                std::array<input_event,19> events{};
                size_t n=0;
                // Full button state makes a later neutral report safe even if
                // an earlier multi-record write was only partially accepted.
                for(const auto& [mask,code]:std::array<std::pair<unsigned,unsigned short>,10>{{
                    {0x1000,BTN_SOUTH},{0x2000,BTN_EAST},{0x4000,BTN_WEST},{0x8000,BTN_NORTH},
                    {0x0100,BTN_TL},{0x0200,BTN_TR},{0x0020,BTN_SELECT},{0x0010,BTN_START},
                    {0x0040,BTN_THUMBL},{0x0080,BTN_THUMBR}}})
                    events[n++]=Device::event(EV_KEY,code,(incoming.buttons&mask)?1:0);
                const int hat_x=((incoming.buttons&8)?1:0)-((incoming.buttons&4)?1:0);
                const int hat_y=((incoming.buttons&2)?1:0)-((incoming.buttons&1)?1:0);
                for(const auto& [axis,value]:std::array<std::pair<unsigned short,int>,8>{{
                    {ABS_X,incoming.lx},{ABS_Y,incoming.ly},{ABS_RX,incoming.rx},{ABS_RY,incoming.ry},
                    {ABS_Z,incoming.lt},{ABS_RZ,incoming.rt},{ABS_HAT0X,hat_x},{ABS_HAT0Y,hat_y}}})
                    events[n++]=Device::event(EV_ABS,axis,value);
                events[n++]=Device::event(EV_SYN,SYN_REPORT,0);
                gamepads_[slot]->write_events(events.data(),n);
                return true;
            }
            void axes(unsigned short ax,int x,unsigned short ay,int y){
                if(x&&y)mouse_.report({Device::event(EV_REL,ax,x),Device::event(EV_REL,ay,y)});
                else if(x)mouse_.report({Device::event(EV_REL,ax,x)});
                else if(y)mouse_.report({Device::event(EV_REL,ay,y)});
            }
        }
        ;
        class NullInput final:public Input {
            public:void key(int,int)override{
            }
            void button(int,int)override{
            }
            void move(int,int)override{
            }
            void wheel(int,int)override{
            }
            bool gamepad(uint8_t,const bridge::GamepadState&)override{return true;}
        }
        ;
        class Metered final:public Input {
            std::unique_ptr<Input> inner_;
            std::function<void()> event_;
            void count(){
                if(event_)event_();
            }
            public:
            Metered(std::unique_ptr<Input> in,std::function<void()> event):inner_(std::move(in)),event_(std::move(event)){
            }
            void key(int a,int b)override{
                inner_->key(a,b);
                count();
            }
            void button(int a,int b)override{
                inner_->button(a,b);
                count();
            }
            void move(int a,int b)override{
                inner_->move(a,b);
                if(a||b)count();
            }
            bool position(int a,int b)override{
                if(!inner_->position(a,b))return false;
                count();
                return true;
            }
            void nudge()override{
                inner_->nudge();
                count();
                count();
            }
            void wheel(int a,int b)override{
                inner_->wheel(a,b);
                if(a||b)count();
            }
            bool gamepad(uint8_t slot,const bridge::GamepadState& state)override{
                if(!inner_->gamepad(slot,state))return false;
                count();return true;
            }
        }
        ;
    }
    std::unique_ptr<Input> make_input(bool dry,std::function<void()> on_event,int width,int height){
        if(width<1||width>32767||height<1||height>32767)throw std::runtime_error("Invalid screen dimensions (1..32767)");
        std::unique_ptr<Input> in;
        if(dry)in=std::make_unique<NullInput>();
        else {
#ifndef SYNERGY_WEBOS
            throw std::runtime_error("Desktop builds cannot inject input; use a verified SYNERGY_WEBOS ARM build");
#else
            if(geteuid()!=0)throw std::runtime_error("Real input requires the explicitly provisioned root daemon");
            auto p=device_path();
            if(p.empty())throw std::runtime_error("No uinput character device; no module/node changes were made");
            in=std::make_unique<UInput>(p,width,height);
#endif
        }
        return std::make_unique<Metered>(std::move(in),std::move(on_event));
    }
    Json probe_input(){
        auto j=okay();
        auto p=device_path();
        j.set("uid",Json::number(geteuid()));
        j.set("root",Json::boolean(geteuid()==0));
        j.set("uinput_path",Json::string(p));
        bool writable=false;
        if(!p.empty()){
            int fd=open(p.c_str(),O_WRONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
            if(fd>=0){
                writable=true;
                close(fd);
            }
        }
        j.set("uinput_writable",Json::boolean(writable));
#ifdef SYNERGY_WEBOS
        j.set("webos_build",Json::boolean(true));
#else
        j.set("webos_build",Json::boolean(false));
#endif
        j.set("input_backend",Json::string("absolute uinput pointer; relative wheel"));
        j.set("luna_send",Json::boolean(access("/usr/bin/luna-send",X_OK)==0));
        j.set("devices_created",Json::boolean(false));
        return j;
    }
}
