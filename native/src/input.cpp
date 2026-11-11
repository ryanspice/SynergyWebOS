#include "synergy/input.hpp"
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <utility>
namespace synergy {
    namespace {
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
            Device(const std::string& path,const char* name,bool mouse){
                fd_=open(path.c_str(),O_WRONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
                if(fd_<0)throw std::runtime_error("Cannot open uinput: permission/driver unavailable");
                try{
                    control(UI_SET_EVBIT,EV_KEY);
                    if(mouse){
                        for(int k:{
                            BTN_LEFT,BTN_RIGHT,BTN_MIDDLE,BTN_SIDE,BTN_EXTRA}
                        )control(UI_SET_KEYBIT,k);
                        control(UI_SET_EVBIT,EV_REL);
                        for(int r:{
                            REL_X,REL_Y,REL_WHEEL,REL_HWHEEL}
                        )control(UI_SET_RELBIT,r);
                    }
                    else for(int k=1;k<256;++k)control(UI_SET_KEYBIT,k);
                    uinput_user_dev d{
                    }
                    ;
                    std::strncpy(d.name,name,sizeof(d.name)-1);
                    d.id.bustype=BUS_VIRTUAL;
                    d.id.vendor=0;
                    d.id.product=0;
                    d.id.version=1;
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
            void emit(unsigned short type,unsigned short code,int value){
                input_event e{
                }
                ;
                e.type=type;
                e.code=code;
                e.value=value;
                write_record(&e,sizeof e);
            }
            void sync(){
                emit(EV_SYN,SYN_REPORT,0);
            }
        }
        ;
        class UInput final:public Input {
            Device keyboard_,mouse_;
            public:
            explicit UInput(const std::string& p):keyboard_(p,"SynergyWebOS Native Keyboard",false),mouse_(p,"SynergyWebOS Native Mouse",true){
            }
            void key(int c,int value)override{
                if(c<=0||c>=256||value<0||value>2)throw std::runtime_error("Invalid key event");
                keyboard_.emit(EV_KEY,static_cast<unsigned short>(c),value);
                keyboard_.sync();
            }
            void button(int c,int value)override{
                if(c<BTN_LEFT||c>BTN_EXTRA||(value!=0&&value!=1))throw std::runtime_error("Invalid button event");
                mouse_.emit(EV_KEY,static_cast<unsigned short>(c),value);
                mouse_.sync();
            }
            void move(int x,int y)override{
                if(x)mouse_.emit(EV_REL,REL_X,x);
                if(y)mouse_.emit(EV_REL,REL_Y,y);
                if(x||y)mouse_.sync();
            }
            void wheel(int x,int y)override{
                if(x)mouse_.emit(EV_REL,REL_HWHEEL,x);
                if(y)mouse_.emit(EV_REL,REL_WHEEL,y);
                if(x||y)mouse_.sync();
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
            void wheel(int a,int b)override{
                inner_->wheel(a,b);
                if(a||b)count();
            }
        }
        ;
    }
    std::unique_ptr<Input> make_input(bool dry,std::function<void()> on_event){
        std::unique_ptr<Input> in;
        if(dry)in=std::make_unique<NullInput>();
        else {
#ifndef SYNERGY_WEBOS
            throw std::runtime_error("Desktop builds cannot inject input; use a verified SYNERGY_WEBOS ARM build");
#else
            if(geteuid()!=0)throw std::runtime_error("Real input requires the explicitly provisioned root daemon");
            auto p=device_path();
            if(p.empty())throw std::runtime_error("No uinput character device; no module/node changes were made");
            in=std::make_unique<UInput>(p);
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
        j.set("input_backend",Json::string("relative uinput; absolute cursor placement not implemented"));
        j.set("luna_send",Json::boolean(access("/usr/bin/luna-send",X_OK)==0));
        j.set("devices_created",Json::boolean(false));
        return j;
    }
}
