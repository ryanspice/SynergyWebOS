#include "bridge_wire.hpp"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <xinput.h>
using Socket=SOCKET;
using PollFd=WSAPOLLFD;
using AddressLength=int;
constexpr Socket InvalidSocket=INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
using Socket=int;
using PollFd=pollfd;
using AddressLength=socklen_t;
constexpr Socket InvalidSocket=-1;
#endif

namespace {
using bridge_wire::Bytes;
using bridge_wire::Framer;
using bridge_wire::PadCoalescer;
using bridge_wire::PadSample;
using bridge_wire::Queue;
using bridge_wire::QueueLimit;
using bridge_wire::framed;
using synergy::bridge::GamepadState;
using synergy::bridge::Slots;
constexpr std::int64_t ControllerRefreshMs=100;

enum class Reason { Configuration, Tls, Frame, Transport, Backpressure, Capture, Stopped };
constexpr const char* label(Reason reason){
    switch(reason){
        case Reason::Configuration:return "CONFIG";
        case Reason::Tls:return "TLS";
        case Reason::Frame:return "FRAME";
        case Reason::Transport:return "TRANSPORT";
        case Reason::Backpressure:return "BACKPRESSURE";
        case Reason::Capture:return "CAPTURE";
        case Reason::Stopped:return "STOPPED";
    }
    return "UNKNOWN";
}
struct BridgeError : std::runtime_error {
    Reason reason;
    BridgeError(Reason r,const char* what):std::runtime_error(what),reason(r){}
};
[[noreturn]] void fail(Reason reason,const char* message){throw BridgeError(reason,message);}
void report(const char* prefix,const std::exception& error){
    if(const auto* bridge=dynamic_cast<const BridgeError*>(&error))
        std::cerr<<prefix<<" ["<<label(bridge->reason)<<"] "<<bridge->what()<<'\n';
    else
        std::cerr<<prefix<<" "<<error.what()<<'\n';
}

std::atomic<bool> running=true;
std::int64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
void close_socket(Socket s){if(s!=InvalidSocket){
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}}
struct OwnedSocket {
    Socket value=InvalidSocket;
    explicit OwnedSocket(Socket s):value(s){}
    ~OwnedSocket(){close_socket(value);}
    OwnedSocket(const OwnedSocket&)=delete;
    OwnedSocket& operator=(const OwnedSocket&)=delete;
};
void nonblocking(Socket s){
#ifdef _WIN32
    u_long value=1;if(ioctlsocket(s,FIONBIO,&value)!=0)fail(Reason::Transport,"Cannot set nonblocking socket");
#else
    int value=fcntl(s,F_GETFL,0);if(value<0||fcntl(s,F_SETFL,value|O_NONBLOCK)<0)fail(Reason::Transport,"Cannot set nonblocking socket");
#endif
}
bool would_block(){
#ifdef _WIN32
    const int error=WSAGetLastError();return error==WSAEWOULDBLOCK||error==WSAEINPROGRESS||error==WSAEINTR;
#else
    return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINPROGRESS||errno==EINTR;
#endif
}
int poll_sockets(PollFd* fds,int count,int timeout){
#ifdef _WIN32
    return WSAPoll(fds,static_cast<ULONG>(count),timeout);
#else
    return poll(fds,static_cast<nfds_t>(count),timeout);
#endif
}
void wait_socket(Socket socket,short events,std::int64_t deadline){
    while(running){
        const auto left=deadline-now_ms();if(left<=0)fail(Reason::Transport,"Socket operation deadline");
        PollFd item{socket,events,0};
        int result=poll_sockets(&item,1,static_cast<int>(std::min<std::int64_t>(left,250)));
        if(result<0&&!would_block())fail(Reason::Transport,"Socket poll failed");
        if(result>0){
            if(item.revents&events)return;
            if(item.revents&(POLLERR|POLLHUP|POLLNVAL))fail(Reason::Transport,"Socket closed");
        }
    }
    fail(Reason::Stopped,"Bridge stopped");
}
unsigned number(const std::string& text,unsigned low,unsigned high){
    std::size_t used=0;unsigned long value=std::stoul(text,&used);
    if(used!=text.size()||value<low||value>high)fail(Reason::Configuration,"Invalid numeric argument");
    return static_cast<unsigned>(value);
}
struct Options {
    std::string ca,cert,key,pin,listen="127.0.0.1",fixture;
    unsigned port=24801,upstream_port=24800;
};
Options options(int argc,char** argv){
    Options o;
    for(int i=1;i<argc;++i){
        const std::string key=argv[i];
        if(i+1>=argc)fail(Reason::Configuration,"Option requires a value");
        const std::string value=argv[++i];
        if(key=="--ca")o.ca=value;else if(key=="--cert")o.cert=value;
        else if(key=="--key")o.key=value;else if(key=="--client-sha256")o.pin=value;
        else if(key=="--listen")o.listen=value;else if(key=="--fixture")o.fixture=value;
        else if(key=="--port")o.port=number(value,1,65535);
        else if(key=="--upstream-port")o.upstream_port=number(value,1,65535);
        else fail(Reason::Configuration,"Unknown option");
    }
    if(o.ca.empty()||o.cert.empty()||o.key.empty()||o.pin.size()!=64)fail(Reason::Configuration,"CA, server certificate/key and exact client SHA-256 pin required");
    for(char c:o.pin)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))fail(Reason::Configuration,"Invalid client certificate pin");
    in_addr address{};if(inet_pton(AF_INET,o.listen.c_str(),&address)!=1)fail(Reason::Configuration,"Listen address must be numeric IPv4");
    const auto ip=ntohl(address.s_addr);
    const bool loop=(ip>>24)==127;
    if(!loop&&(ip>>24)!=10&&(ip>>20)!=0xac1&&(ip>>16)!=0xc0a8)fail(Reason::Configuration,"Listen address must be private LAN or loopback");
    if(!o.fixture.empty()&&!loop)fail(Reason::Configuration,"Synthetic fixture capture is loopback-only");
#ifndef _WIN32
    if(o.fixture.empty())fail(Reason::Configuration,"Real capture requires Windows XInput; Linux supports loopback fixtures only");
#endif
    return o;
}

// Fixture mode is re-read while focused because the loopback tests atomically
// replace the fixture to drive capture changes.  This intentional live-reload
// behavior is why the file is not cached between 8 ms samples.
std::array<GamepadState,4> capture(const Options& o){
    std::array<GamepadState,4> result{};
    if(!o.fixture.empty()){
        std::ifstream file(o.fixture);if(!file)fail(Reason::Capture,"Cannot read synthetic controller fixture");
        std::string line;unsigned lines=0;
        while(std::getline(file,line)){
            if(++lines>4||line.size()>256)fail(Reason::Capture,"Invalid fixture size");
            std::istringstream row(line);int slot,connected,buttons,lx,ly,rx,ry,lt,rt;std::string extra;
            if(!(row>>slot>>connected>>buttons>>lx>>ly>>rx>>ry>>lt>>rt)||(row>>extra)||slot<0||slot>3||connected<0||connected>1||buttons<0||buttons>65535||
               lx<-32768||lx>32767||ly<-32768||ly>32767||rx<-32768||rx>32767||ry<-32768||ry>32767||lt<0||lt>255||rt<0||rt>255)
                fail(Reason::Capture,"Invalid controller fixture state");
            GamepadState state{};state.connected=connected!=0;state.buttons=static_cast<std::uint16_t>(buttons);
            state.lx=static_cast<std::int16_t>(lx);state.ly=static_cast<std::int16_t>(ly);state.rx=static_cast<std::int16_t>(rx);state.ry=static_cast<std::int16_t>(ry);
            state.lt=static_cast<std::uint8_t>(lt);state.rt=static_cast<std::uint8_t>(rt);
            result[static_cast<std::size_t>(slot)]=state;
        }
        return result;
    }
#ifdef _WIN32
    auto flip=[](SHORT value){return static_cast<std::int16_t>(value==std::numeric_limits<SHORT>::min()?32767:-static_cast<int>(value));};
    for(DWORD slot=0;slot<4;++slot){
        XINPUT_STATE state{};
        const DWORD error=XInputGetState(slot,&state);
        if(error==ERROR_DEVICE_NOT_CONNECTED)continue;
        if(error!=ERROR_SUCCESS)fail(Reason::Capture,"XInput capture failed");
        auto& out=result[slot];const auto& in=state.Gamepad;
        out.connected=true;out.buttons=static_cast<std::uint16_t>(in.wButtons&0xf3ffu);
        out.lx=in.sThumbLX;out.ly=flip(in.sThumbLY);out.rx=in.sThumbRX;out.ry=flip(in.sThumbRY);
        out.lt=in.bLeftTrigger;out.rt=in.bRightTrigger;
    }
#endif
    return result;
}

using Context=std::unique_ptr<SSL_CTX,decltype(&SSL_CTX_free)>;
using Stream=std::unique_ptr<SSL,decltype(&SSL_free)>;
Context context(const Options& o){
    Context ctx(SSL_CTX_new(TLS_server_method()),SSL_CTX_free);
    if(!ctx)fail(Reason::Tls,"Cannot create TLS context");
    SSL_CTX_set_default_passwd_cb(ctx.get(),[](char*,int,int,void*){return 0;});
    if(SSL_CTX_set_min_proto_version(ctx.get(),TLS1_2_VERSION)!=1||
       SSL_CTX_load_verify_locations(ctx.get(),o.ca.c_str(),nullptr)!=1||
       SSL_CTX_use_certificate_chain_file(ctx.get(),o.cert.c_str())!=1||
       SSL_CTX_use_PrivateKey_file(ctx.get(),o.key.c_str(),SSL_FILETYPE_PEM)!=1||SSL_CTX_check_private_key(ctx.get())!=1)
        fail(Reason::Tls,"Invalid TLS pairing files");
    SSL_CTX_set_verify(ctx.get(),SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT,nullptr);
    return ctx;
}
short tls_retry_event(int error,const char* failure){
    if(error==SSL_ERROR_WANT_READ)return POLLIN;
    if(error==SSL_ERROR_WANT_WRITE)return POLLOUT;
    fail(Reason::Tls,failure);
}
void authenticate(SSL* ssl,const Options& o,Socket socket){
    const auto deadline=now_ms()+5000;
    for(;;){
        ERR_clear_error();int rc=SSL_accept(ssl);if(rc==1)break;
        const int error=SSL_get_error(ssl,rc);
        wait_socket(socket,tls_retry_event(error,"TV TLS authentication failed"),deadline);
    }
    X509* cert=SSL_get0_peer_certificate(ssl);
    std::array<unsigned char,32> digest{},expected{};unsigned size=0;
    if(!cert||SSL_get_verify_result(ssl)!=X509_V_OK||X509_digest(cert,EVP_sha256(),digest.data(),&size)!=1||size!=32)
        fail(Reason::Tls,"TV client certificate required");
    auto nibble=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c-'A'+10;};
    for(std::size_t i=0;i<32;++i)expected[i]=static_cast<unsigned char>((nibble(o.pin[2*i])<<4)|nibble(o.pin[2*i+1]));
    if(CRYPTO_memcmp(expected.data(),digest.data(),digest.size())!=0)fail(Reason::Tls,"TV certificate pin mismatch");
}

void relay(SSL* ssl,Socket tv,Socket core,const Options& o){
    Queue to_tv,to_core;Framer observed,delivered,client_frames;
    PadCoalescer pads;
    std::array<GamepadState,4> previous{};
    std::array<std::int64_t,4> refreshed{};
    bool initialized=false;std::uint32_t previous_capture=0;std::uint64_t sequence=0;

    // A controller packet handed to OpenSSL is frozen here.  Its exact bytes
    // are retried until completion; focus loss never cancels or replaces it.
    struct Inflight { PadSample sample; Bytes bytes; };
    std::optional<Inflight> inflight;

    enum class Write { None,Classic,Pad };Write pending=Write::None;
    bool write_ready=true;short read_want=POLLIN,write_want=POLLOUT;
    std::int64_t next_sample=0;
    std::int64_t tv_progress=now_ms(),core_progress=now_ms();
    std::int64_t partial_since=0,client_partial_since=0;
    std::int64_t last_core_byte=now_ms(),last_tv_byte=now_ms();
    std::array<std::uint8_t,16384> buffer{};
    while(running){
        const auto now=now_ms();
        const bool focus=observed.active&&delivered.active&&observed.capture==delivered.capture;

        if(!focus){
            // Only unsent samples are discarded; an in-flight TLS write keeps
            // its buffer stable until it completes or the transport fails.
            pads.focus_lost();initialized=false;previous_capture=0;
        }else if(previous_capture!=observed.capture){
            pads.focus_lost();previous={};refreshed={};initialized=false;previous_capture=observed.capture;
        }

        if(focus&&now>=next_sample){
            auto states=capture(o);
            for(std::size_t slot=0;slot<Slots;++slot){
                if(!initialized||states[slot]!=previous[slot]||(states[slot].connected&&now-refreshed[slot]>=ControllerRefreshMs)){
                    pads.observe(static_cast<std::uint8_t>(slot),observed.capture,states[slot],now);
                    previous[slot]=states[slot];refreshed[slot]=now;
                }
            }
            initialized=true;next_sample=now+bridge_wire::ControllerSampleIntervalMs;
        }

        // Transport progress, partial-frame progress and the overall transfer
        // budget are independent policies.  Bytes arriving reset the stall
        // timer even if the frame has not reached a boundary yet.
        if(!observed.boundary()){
            if(!partial_since)partial_since=last_core_byte;
            if(now-last_core_byte>=bridge_wire::FrameStallMs)fail(Reason::Frame,"Partial classic frame stalled");
            if(now-partial_since>=bridge_wire::FrameTransferDeadlineMs)fail(Reason::Frame,"Classic frame transfer deadline");
        }else partial_since=0;
        if(!client_frames.boundary()){
            if(!client_partial_since)client_partial_since=last_tv_byte;
            if(now-last_tv_byte>=bridge_wire::FrameStallMs)fail(Reason::Frame,"Partial client frame stalled");
            if(now-client_partial_since>=bridge_wire::FrameTransferDeadlineMs)fail(Reason::Frame,"Client frame transfer deadline");
        }else client_partial_since=0;

        if(pending==Write::None&&to_tv.empty()&&pads.empty()&&!inflight)tv_progress=now;
        if(to_core.empty())core_progress=now;
        if(now-tv_progress>=bridge_wire::RelayProgressMs||now-core_progress>=bridge_wire::RelayProgressMs)
            fail(Reason::Backpressure,"Relay transport progress deadline");

        // Select the next TLS write.  An in-flight controller write resumes
        // first so it is never replaced mid-transmission.
        if(pending==Write::None){
            if(inflight){pending=Write::Pad;}
            else if(focus&&delivered.boundary()&&!pads.empty()){
                auto sample=pads.take_next();
                if(sequence==std::numeric_limits<std::uint64_t>::max())fail(Reason::Capture,"Controller sequence exhausted");
                synergy::bridge::GamepadPacket p{static_cast<std::uint8_t>(sample->slot),sample->capture,++sequence,sample->state};
                const auto encoded=synergy::bridge::encode_gamepad(p);
                if(!encoded)fail(Reason::Capture,"Invalid captured controller state");
                inflight=Inflight{*sample,framed(*encoded)};
                write_ready=true;pending=Write::Pad;
            }else if(!to_tv.empty()){
                pending=Write::Classic;write_ready=true;
            }
        }

        // WritePending: attempt once, then wait for the requested readiness
        // instead of retrying a backpressured socket on every loop iteration.
        if(pending!=Write::None&&write_ready){
            const auto data=pending==Write::Pad?std::span<const std::uint8_t>(inflight->bytes):to_tv.front();
            ERR_clear_error();const int rc=SSL_write(ssl,data.data(),static_cast<int>(data.size()));
            if(rc>0){
                if(static_cast<std::size_t>(rc)!=data.size())fail(Reason::Tls,"Unexpected partial TLS write");
                if(pending==Write::Classic){delivered.consume(data);to_tv.consume(static_cast<std::size_t>(rc));}
                else{pads.mark_sent(inflight->sample);inflight.reset();}
                pending=Write::None;write_ready=true;write_want=POLLOUT;tv_progress=now_ms();
            }else{
                write_want=tls_retry_event(SSL_get_error(ssl,rc),"TV TLS write failed");
                write_ready=false;
            }
        }

        if(!to_core.empty()){
            const auto data=to_core.front();
            const int rc=static_cast<int>(send(core,reinterpret_cast<const char*>(data.data()),static_cast<int>(data.size()),0));
            if(rc>0){to_core.consume(static_cast<std::size_t>(rc));core_progress=now_ms();}
            else if(rc==0||!would_block())fail(Reason::Transport,"Upstream write failed");
        }

        const bool can_read_tv=pending==Write::None&&to_core.bytes<=QueueLimit-buffer.size();
        const bool buffered=can_read_tv&&SSL_pending(ssl)>0;
        short tv_events=0;
        if(can_read_tv)tv_events|=read_want;
        if(pending!=Write::None&&!write_ready)tv_events|=write_want;
        const short core_events=static_cast<short>((to_tv.bytes<=QueueLimit-buffer.size()?POLLIN:0)|(!to_core.empty()?POLLOUT:0));
        std::array<PollFd,2> fds{{{tv,tv_events,0},{core,core_events,0}}};
        // Drain immediately when unblocked work remains.  A write that returned
        // WANT_WRITE is excluded so a backpressured socket is never spun on.
        const bool more_work=pending==Write::None&&(!to_tv.empty()||(focus&&delivered.boundary()&&!pads.empty()));
        int timeout=buffered||more_work?0:focus?static_cast<int>(std::max<std::int64_t>(0,std::min<std::int64_t>(next_sample-now_ms(),250))):250;
        const int rc=poll_sockets(fds.data(),2,timeout);
        if(rc<0){if(would_block())continue;fail(Reason::Transport,"Relay poll failed");}
        for(const auto& fd:fds)if(fd.revents&(POLLERR|POLLHUP|POLLNVAL))fail(Reason::Transport,"Relay peer closed");
        if(pending!=Write::None&&(fds[0].revents&write_want))write_ready=true;

        if(can_read_tv&&(buffered||(fds[0].revents&read_want))){
            ERR_clear_error();const int count=SSL_read(ssl,buffer.data(),static_cast<int>(buffer.size()));
            if(count>0){
                const std::span<const std::uint8_t> data(buffer.data(),static_cast<std::size_t>(count));
                client_frames.consume(data);
                to_core.push(Bytes(data.begin(),data.end()));read_want=POLLIN;last_tv_byte=now_ms();
            }else{
                read_want=tls_retry_event(SSL_get_error(ssl,count),"TV TLS read closed");
            }
        }
        if((fds[1].revents&POLLIN)&&to_tv.bytes<=QueueLimit-buffer.size()){
            const int count=static_cast<int>(recv(core,reinterpret_cast<char*>(buffer.data()),static_cast<int>(buffer.size()),0));
            if(count>0){
                const std::span<const std::uint8_t> data(buffer.data(),static_cast<std::size_t>(count));
                observed.consume(data);to_tv.push(Bytes(data.begin(),data.end()));last_core_byte=now_ms();
            }else if(count==0||!would_block())fail(Reason::Transport,"Upstream closed");
        }
    }
}

void session(SSL_CTX* ctx,Socket tv,const Options& o){
    nonblocking(tv);Stream ssl(SSL_new(ctx),SSL_free);
    if(!ssl||static_cast<std::uint64_t>(tv)>static_cast<std::uint64_t>(std::numeric_limits<int>::max())||SSL_set_fd(ssl.get(),static_cast<int>(tv))!=1)
        fail(Reason::Tls,"Cannot create TV TLS stream");
    authenticate(ssl.get(),o,tv);
    OwnedSocket core(socket(AF_INET,SOCK_STREAM,IPPROTO_TCP));
    if(core.value==InvalidSocket)fail(Reason::Transport,"Cannot open upstream socket");
    nonblocking(core.value);sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(static_cast<std::uint16_t>(o.upstream_port));
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(core.value,reinterpret_cast<const sockaddr*>(&address),static_cast<AddressLength>(sizeof(address)))!=0&&!would_block())fail(Reason::Transport,"Loopback upstream connect failed");
    wait_socket(core.value,POLLOUT,now_ms()+5000);
    int error=0;
#ifdef _WIN32
    int length=sizeof(error);
#else
    socklen_t length=sizeof(error);
#endif
    if(getsockopt(core.value,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length)!=0||error)fail(Reason::Transport,"Loopback upstream unavailable");
    relay(ssl.get(),tv,core.value,o);
}
}

int main(int argc,char** argv){
    if(argc==2&&(std::string(argv[1])=="--help"||std::string(argv[1])=="--version")){
        std::cout<<"Synergy input bridge 0.4.2\n--ca PEM --cert PEM --key PEM --client-sha256 HEX [--listen PRIVATE_IPV4] [--port 24801] [--upstream-port 24800] [--fixture FILE]\nXInput forwarding while TV focus is captured; PC-side controller suppression is not provided.\n";return 0;
    }
#ifdef _WIN32
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data)!=0)return 2;
    struct Cleanup{~Cleanup(){WSACleanup();}} cleanup;
#else
    std::signal(SIGPIPE,SIG_IGN);
#endif
    std::signal(SIGINT,[](int){running=false;});std::signal(SIGTERM,[](int){running=false;});
    try{
        const auto o=options(argc,argv);auto ctx=context(o);
        OwnedSocket listener(socket(AF_INET,SOCK_STREAM,IPPROTO_TCP));
        if(listener.value==InvalidSocket)fail(Reason::Transport,"Cannot open listener");
        int yes=1;
#ifdef _WIN32
        // SO_REUSEADDR lets another process hijack the listening endpoint on
        // Windows; services that own their port must use SO_EXCLUSIVEADDRUSE.
        constexpr int option=SO_EXCLUSIVEADDRUSE;
#else
        constexpr int option=SO_REUSEADDR;
#endif
        if(setsockopt(listener.value,SOL_SOCKET,option,reinterpret_cast<const char*>(&yes),static_cast<AddressLength>(sizeof(yes)))!=0)
            fail(Reason::Transport,"Cannot configure listener");
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(static_cast<std::uint16_t>(o.port));
        if(inet_pton(AF_INET,o.listen.c_str(),&address.sin_addr)!=1||bind(listener.value,reinterpret_cast<const sockaddr*>(&address),static_cast<AddressLength>(sizeof(address)))!=0||listen(listener.value,1)!=0)
            fail(Reason::Transport,"Cannot bind private listener");
        nonblocking(listener.value);
        std::cout<<"Bridge ready; input capture is focus-gated. PC controller suppression is not provided.\n"<<std::flush;
        while(running){
            PollFd item{listener.value,POLLIN,0};int rc=poll_sockets(&item,1,250);
            if(rc<0){if(would_block())continue;fail(Reason::Transport,"Listener poll failed");}
            if(!(item.revents&POLLIN))continue;
            OwnedSocket tv(accept(listener.value,nullptr,nullptr));if(tv.value==InvalidSocket)continue;
            try{session(ctx.get(),tv.value,o);}catch(const std::exception& error){report("Session end:",error);}
        }
        return 0;
    }catch(const std::exception& error){report("Bridge error:",error);return 2;}
}
