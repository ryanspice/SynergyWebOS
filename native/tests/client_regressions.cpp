// Actual Client/Session, loopback only, synthetic Input. No real input devices.
#include "synergy/client.hpp"
#include "synergy/input.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SYNERGY_TEST_ASAN 1
#endif
#endif

namespace {
thread_local bool client_thread=false;
std::atomic<unsigned> repeats=0,read_polls=0;
std::atomic<unsigned> input_factory_calls=0;
std::atomic<int> input_factory_width=0,input_factory_height=0;
std::atomic<size_t> received=0;
std::atomic<bool> done=false,server_ready=false,failed=false;
std::atomic<bool> released=false;
int delay_us=0;
struct SlowInput final:synergy::Input {
    void key(int code,int value)override{
        if(value==2){++repeats;std::this_thread::sleep_for(std::chrono::microseconds(delay_us));}
        if(code==30&&value==0)released=true;
    }
    void button(int,int)override{}
    void move(int,int)override{}
    void wheel(int,int)override{}
};
synergy::Bytes bytes(std::string_view s){return {s.begin(),s.end()};}
void put16(synergy::Bytes& p,unsigned n){p.push_back(static_cast<uint8_t>(n>>8));p.push_back(static_cast<uint8_t>(n));}
void send_all(int fd,const synergy::Bytes& p){
    size_t at=0;
    while(at<p.size()){
        auto n=send(fd,p.data()+at,p.size()-at,MSG_NOSIGNAL);
        if(n<=0)throw std::runtime_error("fixture send");
        at+=static_cast<size_t>(n);
    }
}
void transmit(int fd,const synergy::Bytes& p){send_all(fd,synergy::frame(p));}
void exact(int fd,void* p,size_t n){
    auto* b=static_cast<char*>(p);
    while(n){auto got=recv(fd,b,n,0);if(got<=0)throw std::runtime_error("fixture receive");b+=got;n-=static_cast<size_t>(got);}
}
void consume(int fd){uint32_t n=0;exact(fd,&n,4);n=ntohl(n);if(n>1024)throw std::runtime_error("fixture response");synergy::Bytes p(n);exact(fd,p.data(),p.size());}
synergy::Bytes key(const char* cmd,unsigned id,unsigned count=0){
    auto p=bytes(cmd);put16(p,id);put16(p,0);
    if(std::string_view(cmd)=="DKRP")put16(p,count);
    put16(p,30);return p;
}
template<class F>void wait(F predicate,int timeout=10000){
    const auto until=synergy::monotonic_ms()+timeout;
    while(!predicate()){
        if(failed||synergy::monotonic_ms()>until)throw std::runtime_error("fixture deadline/failure");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
long rss(){std::ifstream f("/proc/self/statm");long total=0,resident=0;f>>total>>resident;return resident*sysconf(_SC_PAGESIZE);}
}
extern "C" ssize_t __real_recv(int,void*,size_t,int);
extern "C" ssize_t __wrap_recv(int fd,void* b,size_t n,int flags){
    const auto got=__real_recv(fd,b,n,flags);
    if(client_thread&&got>0)received+=static_cast<size_t>(got);
    return got;
}
extern "C" int __real_poll(pollfd*,nfds_t,int);
extern "C" int __wrap_poll(pollfd* p,nfds_t n,int timeout){
    if(client_thread&&n>=1&&p[0].events==POLLIN)++read_polls;
    return __real_poll(p,n,timeout);
}
namespace synergy {
std::unique_ptr<Input> make_input(bool dry_run,std::function<void()> on_event,int width,int height){
    (void)on_event;
    if(!dry_run)throw std::runtime_error("client regression fixture requires dry run");
    ++input_factory_calls;
    input_factory_width=width;
    input_factory_height=height;
    client_thread=true;
    return std::make_unique<SlowInput>();
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const std::string mode=argv[1];
    delay_us=mode=="slice"?20000:mode=="alt"?21000:mode=="memory"?250:1000;
    const unsigned repeat_count=(mode=="memory"||mode=="overflow"||mode=="byte-limit")?65535:256;
    // Allocate the fixture's reusable frame before the baseline memory sample.
    synergy::Bytes large(1024*1024,'x');std::copy_n("DCLP",4,large.begin());
    const auto large_frame=synergy::frame(large);
    int listener=socket(AF_INET,SOCK_STREAM,0);
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(listener<0||bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof address)||listen(listener,1))return 2;
    socklen_t length=sizeof address;
    if(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length))return 2;
    std::exception_ptr error;
    std::thread server([&]{
        int fd=accept(listener,nullptr,nullptr);
        try{
            if(fd<0)throw std::runtime_error("fixture accept");
            timeval timeout{10,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
            auto hello=bytes("Synergy");put16(hello,1);put16(hello,4);transmit(fd,hello);consume(fd);
            transmit(fd,bytes("QINF"));consume(fd);
            auto enter=bytes("CINN");put16(enter,0);put16(enter,0);enter.insert(enter.end(),{0,0,0,1});put16(enter,0);transmit(fd,enter);consume(fd);
            if(mode!="idle"){
                transmit(fd,key("DKDN",'a'));consume(fd);
                synergy::Bytes packet;
                auto append=[&](const synergy::Bytes& p){auto f=synergy::frame(p);packet.insert(packet.end(),f.begin(),f.end());};
                if(mode=="timer"||mode=="alt")append(key("DKDN",mode=="alt"?0xefe9:0xefe7));
                append(key("DKRP",'a',repeat_count));
                if(mode=="timer"||mode=="alt")append(key("DKUP",mode=="alt"?0xefe9:0xefe7));
                if(mode=="timer"||mode=="alt"||mode=="release"){
                    append(key("DKUP",'a'));append(bytes("COUT"));
                }
                if(mode=="overflow")for(int i=0;i<5000;++i)append(bytes("CNOP"));
                if(mode=="byte-limit")packet.insert(packet.end(),{0,4,176,0,'D','S','O','P'}); // 300 KiB declaration; no body needed
                send_all(fd,packet);
            }
            server_ready=true;
            if(mode=="memory")for(int i=0;i<8;++i)send_all(fd,large_frame);
            wait([]{return done.load();});
        }catch(...){error=std::current_exception();failed=true;}
        if(fd>=0)close(fd);
    });
    synergy::Platform platform;synergy::Client client(platform);
    synergy::Config config;config.server="127.0.0.1";config.port=ntohs(address.sin_port);config.cursor_keepalive=false;config.home_delay_ms=50;
    const long baseline=rss();
    client.start(config,true);
    try{
        wait([]{return server_ready.load();});
        if(input_factory_calls!=1||input_factory_width!=config.width||input_factory_height!=config.height)
            throw std::runtime_error("client did not use the four-argument synthetic input factory exactly once");
        if(mode=="slice")wait([]{return repeats>=1;});
        if(mode=="memory")wait([&]{return received>=8*large_frame.size();});
        if(mode=="timer"||mode=="alt"||mode=="release")wait([]{return released.load();});
        if(mode=="overflow"||mode=="byte-limit")wait([&]{return !client.running();});
        if(mode=="idle")std::this_thread::sleep_for(std::chrono::milliseconds(2200));
        const long peak=rss();const auto before=repeats.load();
        const auto client_error=client.snapshot().get("error").str();
        const auto ingress_peak=client.snapshot().get("peak_ingress_bytes").num();
#if defined(__SANITIZE_ADDRESS__) || defined(SYNERGY_TEST_ASAN)
        constexpr bool rss_is_comparable=false;
#else
        constexpr bool rss_is_comparable=true;
#endif
        const auto begin=std::chrono::steady_clock::now();
        client.stop();
        const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        done=true;server.join();close(listener);
        if(error)std::rethrow_exception(error);
        std::cout<<"mode="<<mode<<" fake_repeat_us="<<delay_us<<" repeats_before_stop="<<before<<" repeats_after_stop="<<repeats
                 <<" stop_join_ms="<<ms<<" client_received_bytes="<<received<<" rss_baseline="<<baseline<<" rss_sample="<<peak
                 <<" read_polls="<<read_polls<<" ingress_peak_bytes="<<ingress_peak<<" rss_gate="<<rss_is_comparable<<" client_error="<<client_error<<" platform_message="<<platform.snapshot().get("message").str()<<" real_input=false\n";
        if(mode=="slice")return ms<150&&repeats<=before+1?0:1;
        if(mode=="memory")return before<65535&&received>=8*large_frame.size()&&ingress_peak>=11&&ingress_peak<=256*1024&&(!rss_is_comparable||peak-baseline<4*1024*1024)?0:1;
        if(mode=="timer"||mode=="alt"||mode=="release")return repeats==256&&!platform.snapshot().get("message").str().starts_with("DRY RUN: home")?0:1;
        if(mode=="idle")return read_polls<15&&ms<100?0:1;
        if(mode=="overflow")return released&&client_error=="Receive frame budget exceeded"?0:1;
        if(mode=="byte-limit")return released&&client_error=="Receive byte budget exceeded"?0:1;
    }catch(const std::exception& e){done=true;client.stop();server.join();close(listener);std::cerr<<e.what()<<'\n';return 2;}
    return 2;
}
