#include "synergy/client.hpp"
#include "synergy/input.hpp"
#include "synergy/tls.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <cerrno>
#include <array>
#include <algorithm>
#include <deque>
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
        void wait_socket(int fd,short events,int64_t deadline,const std::atomic<bool>& run,int wake,const std::function<void()>& heartbeat={}){
            while(run.load()){
                if(heartbeat)heartbeat();
                if(monotonic_ms()>=deadline)throw std::runtime_error("Socket deadline expired");
                std::array<pollfd,2> p{{{fd,events,0},{wake,POLLIN,0}}};
                auto timeout=std::max<int64_t>(0,deadline-monotonic_ms());
                if(heartbeat)timeout=std::min<int64_t>(timeout,100);
                int n=poll(p.data(),p.size(),static_cast<int>(timeout));
                if(n<0&&errno!=EINTR)throw std::runtime_error("Socket poll failed");
                if(n>0){
                    if(!run.load()||p[1].revents)throw std::runtime_error("Stopped");
                    if(p[0].revents&events)return;
                    if(p[0].revents&(POLLERR|POLLHUP|POLLNVAL))throw std::runtime_error("Socket disconnected");
                }
            }
            throw std::runtime_error("Stopped");
        }
        void send_frame(int fd,const Bytes& payload,const std::atomic<bool>& run,int wake){
            auto b=frame(payload);
            size_t at=0;
            auto deadline=monotonic_ms()+3000;
            while(at<b.size()){
                wait_socket(fd,POLLOUT,deadline,run,wake);
                auto n=send(fd,b.data()+at,b.size()-at,MSG_NOSIGNAL);
                if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))continue;
                if(n<=0)throw std::runtime_error("Socket send failed");
                at+=static_cast<size_t>(n);
            }
        }
    }
    Client::Client(Platform& platform):platform_(platform),wake_(eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK)){
        if(wake_<0)throw std::runtime_error("Cannot create client cancellation wakeup");
    }
    Client::~Client(){stop();close(wake_);}
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
        uint64_t pending;
        while(read(wake_,&pending,sizeof pending)<0&&errno==EINTR){}
#ifndef SYNERGY_WEBOS
        if(!c.dry_run)throw std::runtime_error("Real input is disabled in desktop builds");
#endif
        {
            std::lock_guard lock(mutex_);
            active_=c;
            have_active_=true;
        }
        events_=0;
        ingress_peak_=0;
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
        const uint64_t wake=1;
        ssize_t written;
        do {written=write(wake_,&wake,sizeof wake);} while(written<0&&errno==EINTR);
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
        j.set("peak_ingress_bytes",Json::number(static_cast<double>(ingress_peak_.load())));
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
            while(running()&&monotonic_ms()<until){
                pollfd wake{wake_,POLLIN,0};
                const int result=poll(&wake,1,static_cast<int>(std::max<int64_t>(0,until-monotonic_ms())));
                if(result>0)break;
                if(result<0&&errno!=EINTR)break;
            }
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
        wait_socket(socket_fd.n,POLLOUT,monotonic_ms()+5000,running_,wake_);
        int error=0;
        socklen_t es=sizeof error;
        if(getsockopt(socket_fd.n,SOL_SOCKET,SO_ERROR,&error,&es)!=0||error)throw std::runtime_error("Desktop connection refused/unavailable");
        int yes=1;
        setsockopt(socket_fd.n,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof yes);
        std::unique_ptr<TlsClient> tls;
        Session* active_protocol=nullptr;
        if(config.input_bridge)tls=std::make_unique<TlsClient>(socket_fd.n,config,[&](short events,int64_t deadline){
            wait_socket(socket_fd.n,events,deadline,running_,wake_,[&]{
                if(active_protocol)active_protocol->expire_gamepads(monotonic_ms());
            });
        });
        auto input=make_input(config.dry_run,[this]{
            ++events_;}
        ,config.width,config.height);
        platform_.cancel();
        auto gen=platform_.generation();
        Session protocol(config,*input,[&](const Bytes& p){
            if(tls){auto data=frame(p);tls->send(data);}
            else send_frame(socket_fd.n,p,running_,wake_);}
        ,[&](Action a){
            platform_.action(a,config.dry_run,gen);}
        ,[&]{
            platform_.reset_cycle(gen);}
        );
        protocol.defer_repeats(true);
        active_protocol=&protocol;
        state("handshaking");
        FrameDecoder decoder(true);
        std::optional<int64_t> partial_at;
        auto handshake_started=monotonic_ms();
        auto last_frame=monotonic_ms();
        std::array<uint8_t,16384> buffer{
        }
        ;
        struct Packet {Bytes payload;int64_t received;};
        std::deque<Packet> backlog;
        size_t backlog_bytes=0;
        int64_t repeat_received=0;
        constexpr int64_t SliceMs=4;
        constexpr size_t MaxBacklog=4096;
        constexpr size_t MaxIngressBytes=256*1024;
        while(running()){
            auto now=monotonic_ms();
            protocol.expire_gamepads(now);
            if(!protocol.ready()&&now-handshake_started>5000)throw std::runtime_error("Server handshake deadline exceeded");
            if(now-last_frame>(protocol.ready()?config.idle_timeout_ms:5000))throw std::runtime_error("Server idle/handshake timeout");
            if(partial_at&&now-*partial_at>5000)throw std::runtime_error("Incomplete frame deadline exceeded");
            const bool busy=protocol.pending()||!backlog.empty();
            auto deadline=std::min(handshake_started+5001,last_frame+5001);
            if(protocol.ready())deadline=last_frame+config.idle_timeout_ms+1;
            if(partial_at)deadline=std::min(deadline,*partial_at+5001);
            deadline=protocol.next_gamepad_deadline(deadline);
            if(!busy&&!decoder.partial())deadline=protocol.next_deadline(deadline);
            const short read_event=tls?tls->receive_event():POLLIN;
            std::array<pollfd,2> p{{{socket_fd.n,read_event,0},{wake_,POLLIN,0}}};
            const bool buffered=tls&&tls->pending();
            const int timeout=(busy||buffered)?0:static_cast<int>(std::max<int64_t>(0,deadline-now));
            int n=poll(p.data(),p.size(),timeout);
            if(n<0){
                if(errno==EINTR)continue;
                throw std::runtime_error("Socket read poll failed");
            }
            if(!running()||p[1].revents)break;
            if(buffered||(n>0&&(p[0].revents&read_event))){
                auto count=tls?tls->receive(buffer):recv(socket_fd.n,buffer.data(),buffer.size(),0);
                if(count<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))count=-1;
                else if(count<=0)throw std::runtime_error("Desktop disconnected");
                if(count>0){
                    now=monotonic_ms();
                    const auto completed=decoder.consume(std::span<const uint8_t>(buffer.data(),static_cast<size_t>(count)),[&](Bytes&& payload){
                        if(backlog.size()>=MaxBacklog)throw std::runtime_error("Receive frame budget exceeded");
                        backlog_bytes+=payload.capacity();
                        ingress_peak_=std::max(ingress_peak_.load(),backlog_bytes);
                        backlog.push_back({std::move(payload),now});
                    },MaxIngressBytes-backlog_bytes);
                    ingress_peak_=std::max(ingress_peak_.load(),backlog_bytes+decoder.retained_bytes());
                    if(!decoder.partial())partial_at.reset();
                    else if(!partial_at||completed)partial_at=now;
                    if(completed)last_frame=now;
                }
            }
            else if(p[0].revents&(POLLERR|POLLHUP|POLLNVAL))throw std::runtime_error("Desktop disconnected");
            const auto slice_end=monotonic_ms()+SliceMs;
            while(running()&&(protocol.pending()||!backlog.empty())&&monotonic_ms()<slice_end){
                if(protocol.pending())protocol.pump(repeat_received,1);
                else {
                    auto packet=std::move(backlog.front());backlog.pop_front();
                    // Keep the packet charged until handle() and its reply finish.
                    const auto bytes=packet.payload.capacity();
                    bool before=protocol.active();
                    if(!protocol.handle(packet.payload,packet.received,monotonic_ms()))return;
                    backlog_bytes-=bytes;
                    if(protocol.pending())repeat_received=packet.received;
                    if(before&&!protocol.active()){
                        platform_.cancel();gen=platform_.generation();
                    }
                    state(protocol.active()?"active":protocol.ready()?"connected":"handshaking");
                }
            }
            // Packet timestamps drive ordered hold state. Never expire a hold
            // ahead of an already-received release or unfinished wire frame.
            if(running()&&!protocol.pending()&&backlog.empty()&&!decoder.partial())protocol.tick(monotonic_ms());
        }
    }
}
