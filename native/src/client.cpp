#include "synergy/client.hpp"
#include "synergy/input.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <array>
#include <algorithm>
namespace synergy {
    namespace {
        struct Fd {
            int n=-1;
            explicit Fd(int f):n(f){
            }
            ~Fd(){
                if(n>=0)close(n);
            }
            Fd(const Fd&)=delete;
        }
        ;
        void wait_socket(int fd,short events,int64_t deadline,const std::atomic<bool>& run){
            while(run.load()){
                if(monotonic_ms()>=deadline)throw std::runtime_error("Socket deadline expired");
                pollfd p{
                    fd,events,0}
                ;
                int n=poll(&p,1,25);
                if(n<0&&errno!=EINTR)throw std::runtime_error("Socket poll failed");
                if(n>0){
                    if(p.revents&events)return;
                    if(p.revents&(POLLERR|POLLHUP|POLLNVAL))throw std::runtime_error("Socket disconnected");
                }
            }
            throw std::runtime_error("Stopped");
        }
        void send_frame(int fd,const Bytes& payload,const std::atomic<bool>& run){
            auto b=frame(payload);
            size_t at=0;
            auto deadline=monotonic_ms()+3000;
            while(at<b.size()){
                wait_socket(fd,POLLOUT,deadline,run);
                auto n=send(fd,b.data()+at,b.size()-at,MSG_NOSIGNAL);
                if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))continue;
                if(n<=0)throw std::runtime_error("Socket send failed");
                at+=static_cast<size_t>(n);
            }
        }
    }
    void Client::state(std::string s,std::string error){
        std::lock_guard lock(mutex_);
        if(state_!=s||error_!=error){
            log_.push_back(s+(error.empty()?"":": "+error));
            while(log_.size()>100)log_.pop_front();
        }
        state_=std::move(s);
        error_=std::move(error);
    }
    void Client::start(const Config& c,bool once){
        c.validate();
        if(running())throw std::runtime_error("Client already running; Stop or explicitly Restart first");
        if(worker_.joinable())worker_.join();
#ifndef SYNERGY_WEBOS
        if(!c.dry_run)throw std::runtime_error("Real input is disabled in desktop builds");
#endif
        {
            std::lock_guard lock(mutex_);
            active_=c;
            have_active_=true;
        }
        events_=0;
        running_=true;
        try{
            worker_=std::thread(&Client::loop,this,c,once);
        }
        catch(...){
            running_=false;
            throw;
        }
    }
    void Client::stop(){
        running_=false;
        platform_.cancel();
        if(worker_.joinable())worker_.join();
        state("stopped");
    }
    Json Client::snapshot()const{
        std::lock_guard lock(mutex_);
        auto j=Json::object();
        j.set("running",Json::boolean(running()));
        j.set("state",Json::string(state_));
        j.set("error",Json::string(error_));
        j.set("active_config",have_active_?active_.json():Json());
        j.set("input_events",Json::number(static_cast<double>(events_.load())));
        auto log=Json::array();
        for(const auto& l:log_)log.add(Json::string(l));
        j.set("log",log);
        return j;
    }
    void Client::loop(Config config,bool once){
        unsigned backoff=1;
        while(running()){
            auto began=monotonic_ms();
            try{
                state("connecting");
                session(config);
                if(running())state("disconnected","Server closed the session");
            }
            catch(const std::exception& e){
                if(running())state("disconnected",e.what());
            }
            platform_.cancel();
            if(once)break;
            if(monotonic_ms()-began>10000)backoff=1;
            auto until=monotonic_ms()+backoff*1000;
            while(running()&&monotonic_ms()<until)std::this_thread::sleep_for(std::chrono::milliseconds(25));
            backoff=std::min(30u,backoff*2);
        }
        running_=false;
    }
    void Client::session(const Config& config){
        sockaddr_storage storage{
        }
        ;
        socklen_t len=0;
        int family=AF_INET;
        auto* v4=reinterpret_cast<sockaddr_in*>(&storage);
        if(inet_pton(AF_INET,config.server.c_str(),&v4->sin_addr)==1){
            v4->sin_family=AF_INET;
            v4->sin_port=htons(static_cast<uint16_t>(config.port));
            len=sizeof(sockaddr_in);
        }
        else{
            family=AF_INET6;
            auto* v6=reinterpret_cast<sockaddr_in6*>(&storage);
            v6->sin6_family=AF_INET6;
            v6->sin6_port=htons(static_cast<uint16_t>(config.port));
            if(inet_pton(AF_INET6,config.server.c_str(),&v6->sin6_addr)!=1)throw std::runtime_error("Invalid numeric IP");
            len=sizeof(sockaddr_in6);
        }
        Fd socket_fd(socket(family,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0));
        if(socket_fd.n<0)throw std::runtime_error("Cannot create socket");
        int code=connect(socket_fd.n,reinterpret_cast<sockaddr*>(&storage),len);
        if(code<0&&errno!=EINPROGRESS)throw std::runtime_error("Desktop connection failed");
        wait_socket(socket_fd.n,POLLOUT,monotonic_ms()+5000,running_);
        int error=0;
        socklen_t es=sizeof error;
        if(getsockopt(socket_fd.n,SOL_SOCKET,SO_ERROR,&error,&es)!=0||error)throw std::runtime_error("Desktop connection refused/unavailable");
        int yes=1;
        setsockopt(socket_fd.n,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof yes);
        auto input=make_input(config.dry_run,[this]{
            ++events_;}
        );
        platform_.cancel();
        auto gen=platform_.generation();
        Session protocol(config,*input,[&](const Bytes& p){
            send_frame(socket_fd.n,p,running_);}
        ,[&](Action a){
            platform_.action(a,config.dry_run,gen);}
        );
        state("handshaking");
        FrameDecoder decoder;
        std::optional<int64_t> partial_at;
        auto last_frame=monotonic_ms();
        std::array<uint8_t,16384> buffer{
        }
        ;
        while(running()){
            auto now=monotonic_ms();
            protocol.tick(now);
            if(now-last_frame>(protocol.ready()?config.idle_timeout_ms:5000))throw std::runtime_error("Server idle/handshake timeout");
            if(partial_at&&now-*partial_at>5000)throw std::runtime_error("Incomplete frame deadline exceeded");
            pollfd p{
                socket_fd.n,POLLIN,0}
            ;
            int n=poll(&p,1,20);
            if(n<0){
                if(errno==EINTR)continue;
                throw std::runtime_error("Socket read poll failed");
            }
            if(n==0)continue;
            if(!(p.revents&POLLIN)){
                if(p.revents&(POLLERR|POLLHUP|POLLNVAL))throw std::runtime_error("Desktop disconnected");
                continue;
            }
            auto count=recv(socket_fd.n,buffer.data(),buffer.size(),0);
            if(count<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
            if(count<=0)throw std::runtime_error("Desktop disconnected");
            auto frames=decoder.feed(std::span<const uint8_t>(buffer.data(),static_cast<size_t>(count)));
            if(!decoder.partial())partial_at.reset();
            else if(!partial_at||!frames.empty())partial_at=now;
            for(const auto& payload:frames){
                bool before=protocol.active();
                if(!protocol.handle(payload,now))return;
                if(before&&!protocol.active()){
                    platform_.cancel();
                    gen=platform_.generation();
                }
                last_frame=now;
                state(protocol.active()?"active":protocol.ready()?"connected":"handshaking");
            }
        }
    }
}
