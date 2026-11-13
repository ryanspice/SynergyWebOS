#include "synergy/gamepad_wire.hpp"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
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
using Bytes=std::vector<std::uint8_t>;
using synergy::bridge::GamepadState;
constexpr std::size_t QueueLimit=64*1024;
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
    u_long value=1;if(ioctlsocket(s,FIONBIO,&value)!=0)throw std::runtime_error("Cannot set nonblocking socket");
#else
    int value=fcntl(s,F_GETFL,0);if(value<0||fcntl(s,F_SETFL,value|O_NONBLOCK)<0)throw std::runtime_error("Cannot set nonblocking socket");
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
        const auto left=deadline-now_ms();if(left<=0)throw std::runtime_error("Socket operation deadline");
        PollFd item{socket,events,0};
        int result=poll_sockets(&item,1,static_cast<int>(std::min<std::int64_t>(left,250)));
        if(result<0&&!would_block())throw std::runtime_error("Socket poll failed");
        if(result>0){
            if(item.revents&events)return;
            if(item.revents&(POLLERR|POLLHUP|POLLNVAL))throw std::runtime_error("Socket closed");
        }
    }
    throw std::runtime_error("Bridge stopped");
}
unsigned number(const std::string& text,unsigned low,unsigned high){
    std::size_t used=0;unsigned long value=std::stoul(text,&used);
    if(used!=text.size()||value<low||value>high)throw std::runtime_error("Invalid numeric argument");
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
        if(i+1>=argc)throw std::runtime_error("Option requires a value");
        const std::string value=argv[++i];
        if(key=="--ca")o.ca=value;else if(key=="--cert")o.cert=value;
        else if(key=="--key")o.key=value;else if(key=="--client-sha256")o.pin=value;
        else if(key=="--listen")o.listen=value;else if(key=="--fixture")o.fixture=value;
        else if(key=="--port")o.port=number(value,1,65535);
        else if(key=="--upstream-port")o.upstream_port=number(value,1,65535);
        else throw std::runtime_error("Unknown option");
    }
    if(o.ca.empty()||o.cert.empty()||o.key.empty()||o.pin.size()!=64)throw std::runtime_error("CA, server certificate/key and exact client SHA-256 pin required");
    for(char c:o.pin)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))throw std::runtime_error("Invalid client certificate pin");
    in_addr address{};if(inet_pton(AF_INET,o.listen.c_str(),&address)!=1)throw std::runtime_error("Listen address must be numeric IPv4");
    const auto ip=ntohl(address.s_addr);
    const bool loop=(ip>>24)==127;
    if(!loop&&(ip>>24)!=10&&(ip>>20)!=0xac1&&(ip>>16)!=0xc0a8)throw std::runtime_error("Listen address must be private LAN or loopback");
    if(!o.fixture.empty()&&!loop)throw std::runtime_error("Synthetic fixture capture is loopback-only");
#ifndef _WIN32
    if(o.fixture.empty())throw std::runtime_error("Real capture requires Windows XInput; Linux supports loopback fixtures only");
#endif
    return o;
}

// Observes bytes only after their corresponding read/write completed. It keeps
// at most 14 payload bytes and never retains a clipboard body.
struct Framer {
    std::array<std::uint8_t,4> prefix{};
    std::array<std::uint8_t,14> head{};
    std::size_t prefix_used=0,head_used=0;
    std::uint32_t remaining=0,length=0,capture=0;
    bool active=false;
    bool boundary()const{return prefix_used==0;}
    void consume(std::span<const std::uint8_t> data){
        for(auto byte:data){
            if(prefix_used<4){
                prefix[prefix_used++]=byte;
                if(prefix_used==4){
                    length=0;for(auto value:prefix)length=(length<<8)|value;
                    if(length<4||length>1024*1024)throw std::runtime_error("Invalid classic frame size");
                    remaining=length;head_used=0;
                }
                continue;
            }
            if(head_used<head.size())head[head_used++]=byte;
            if(--remaining)continue;
            if(std::memcmp(head.data(),"CINN",4)==0){
                if(length!=14)throw std::runtime_error("Malformed focus enter");
                capture=0;for(std::size_t i=8;i<12;++i)capture=(capture<<8)|head[i];active=true;
            }else if(std::memcmp(head.data(),"COUT",4)==0||std::memcmp(head.data(),"CBYE",4)==0){
                if(length!=4)throw std::runtime_error("Malformed focus leave");
                active=false;
            }
            prefix_used=0;
        }
    }
};
struct Queue {
    std::deque<Bytes> chunks;
    std::size_t offset=0,bytes=0;
    bool empty()const{return chunks.empty();}
    void push(Bytes data){
        if(chunks.size()>=256||bytes+data.size()>QueueLimit)throw std::runtime_error("Relay queue budget exceeded");
        bytes+=data.size();chunks.push_back(std::move(data));
    }
    std::span<const std::uint8_t> front()const{return std::span<const std::uint8_t>(chunks.front()).subspan(offset);}
    void consume(std::size_t n){bytes-=n;offset+=n;if(offset==chunks.front().size()){chunks.pop_front();offset=0;}}
};
struct PadMessage {Bytes bytes;std::int64_t captured;};
std::array<GamepadState,4> capture(const Options& o){
    std::array<GamepadState,4> result{};
    if(!o.fixture.empty()){
        std::ifstream file(o.fixture);if(!file)throw std::runtime_error("Cannot read synthetic controller fixture");
        std::string line;unsigned lines=0;
        while(std::getline(file,line)){
            if(++lines>4||line.size()>256)throw std::runtime_error("Invalid fixture size");
            std::istringstream row(line);int slot,connected,buttons,lx,ly,rx,ry,lt,rt;std::string extra;
            if(!(row>>slot>>connected>>buttons>>lx>>ly>>rx>>ry>>lt>>rt)||(row>>extra)||slot<0||slot>3||connected<0||connected>1||buttons<0||buttons>65535||
               lx<-32768||lx>32767||ly<-32768||ly>32767||rx<-32768||rx>32767||ry<-32768||ry>32767||lt<0||lt>255||rt<0||rt>255)
                throw std::runtime_error("Invalid controller fixture state");
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
        if(error!=ERROR_SUCCESS)throw std::runtime_error("XInput capture failed");
        auto& out=result[slot];const auto& in=state.Gamepad;
        out.connected=true;out.buttons=static_cast<std::uint16_t>(in.wButtons&0xf3ffu);
        out.lx=in.sThumbLX;out.ly=flip(in.sThumbLY);out.rx=in.sThumbRX;out.ry=flip(in.sThumbRY);
        out.lt=in.bLeftTrigger;out.rt=in.bRightTrigger;
    }
#endif
    return result;
}
Bytes framed(std::span<const std::uint8_t> payload){
    const auto n=static_cast<std::uint32_t>(payload.size());
    Bytes data{static_cast<std::uint8_t>(n>>24),static_cast<std::uint8_t>(n>>16),static_cast<std::uint8_t>(n>>8),static_cast<std::uint8_t>(n)};
    data.insert(data.end(),payload.begin(),payload.end());return data;
}
using Context=std::unique_ptr<SSL_CTX,decltype(&SSL_CTX_free)>;
using Stream=std::unique_ptr<SSL,decltype(&SSL_free)>;
Context context(const Options& o){
    Context ctx(SSL_CTX_new(TLS_server_method()),SSL_CTX_free);
    if(!ctx)throw std::runtime_error("Cannot create TLS context");
    SSL_CTX_set_default_passwd_cb(ctx.get(),[](char*,int,int,void*){return 0;});
    if(SSL_CTX_set_min_proto_version(ctx.get(),TLS1_2_VERSION)!=1||
       SSL_CTX_load_verify_locations(ctx.get(),o.ca.c_str(),nullptr)!=1||
       SSL_CTX_use_certificate_chain_file(ctx.get(),o.cert.c_str())!=1||
       SSL_CTX_use_PrivateKey_file(ctx.get(),o.key.c_str(),SSL_FILETYPE_PEM)!=1||SSL_CTX_check_private_key(ctx.get())!=1)
        throw std::runtime_error("Invalid TLS pairing files");
    SSL_CTX_set_verify(ctx.get(),SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT,nullptr);
    return ctx;
}
void authenticate(SSL* ssl,const Options& o,Socket socket){
    const auto deadline=now_ms()+5000;
    for(;;){
        ERR_clear_error();int rc=SSL_accept(ssl);if(rc==1)break;
        const int error=SSL_get_error(ssl,rc);
        if(error!=SSL_ERROR_WANT_READ&&error!=SSL_ERROR_WANT_WRITE)throw std::runtime_error("TV TLS authentication failed");
        wait_socket(socket,error==SSL_ERROR_WANT_READ?POLLIN:POLLOUT,deadline);
    }
    X509* cert=SSL_get0_peer_certificate(ssl);
    std::array<unsigned char,32> digest{},expected{};unsigned size=0;
    if(!cert||SSL_get_verify_result(ssl)!=X509_V_OK||X509_digest(cert,EVP_sha256(),digest.data(),&size)!=1||size!=32)
        throw std::runtime_error("TV client certificate required");
    auto nibble=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c-'A'+10;};
    for(std::size_t i=0;i<32;++i)expected[i]=static_cast<unsigned char>((nibble(o.pin[2*i])<<4)|nibble(o.pin[2*i+1]));
    if(CRYPTO_memcmp(expected.data(),digest.data(),digest.size())!=0)throw std::runtime_error("TV certificate pin mismatch");
}
void relay(SSL* ssl,Socket tv,Socket core,const Options& o){
    Queue to_tv,to_core;Framer observed,delivered,client_frames;
    std::deque<PadMessage> pads;
    std::array<GamepadState,4> previous{};
    std::array<std::int64_t,4> refreshed{};
    bool initialized=false;std::uint32_t previous_capture=0;std::uint64_t sequence=0;
    enum class Write {None,Classic,Pad};Write pending=Write::None;
    short read_want=POLLIN,write_want=POLLOUT;
    std::int64_t next_sample=0,tv_progress=now_ms(),core_progress=now_ms(),partial_since=0,client_partial_since=0;
    std::array<std::uint8_t,16384> buffer{};
    while(running){
        const auto now=now_ms();
        const bool focus=observed.active&&delivered.active&&observed.capture==delivered.capture;
        if(!focus){
            if(pending==Write::Pad)throw std::runtime_error("Focus changed during backpressured controller write");
            pads.clear();initialized=false;
        }
        if(focus&&now>=next_sample){
            if(!initialized||previous_capture!=observed.capture){previous={};refreshed={};initialized=false;previous_capture=observed.capture;}
            auto states=capture(o);
            for(std::size_t slot=0;slot<4;++slot){
                if(!initialized||states[slot]!=previous[slot]||(states[slot].connected&&now-refreshed[slot]>=100)){
                    if(pads.size()>=64||sequence==std::numeric_limits<std::uint64_t>::max())throw std::runtime_error("Controller queue budget exceeded");
                    synergy::bridge::GamepadPacket p{static_cast<std::uint8_t>(slot),observed.capture,++sequence,states[slot]};
                    const auto encoded=synergy::bridge::encode_gamepad(p);
                    if(!encoded)throw std::runtime_error("Invalid captured controller state");
                    pads.push_back({framed(*encoded),now});previous[slot]=states[slot];refreshed[slot]=now;
                }
            }
            initialized=true;next_sample=now+8;
        }
        if(!pads.empty()&&now-pads.front().captured>=250)throw std::runtime_error("Controller delivery freshness deadline");
        if(!observed.boundary()&&!partial_since)partial_since=now;
        if(observed.boundary())partial_since=0;
        if(partial_since&&now-partial_since>=5000)throw std::runtime_error("Partial classic frame deadline");
        if(!client_frames.boundary()&&!client_partial_since)client_partial_since=now;
        if(client_frames.boundary())client_partial_since=0;
        if(client_partial_since&&now-client_partial_since>=5000)throw std::runtime_error("Partial client frame deadline");
        if(to_tv.empty()&&pads.empty())tv_progress=now;
        if(to_core.empty())core_progress=now;
        if(now-tv_progress>=3000||now-core_progress>=3000)throw std::runtime_error("Relay backpressure deadline");
        if(pending==Write::None){
            if(focus&&delivered.boundary()&&!pads.empty())pending=Write::Pad;
            else if(!to_tv.empty())pending=Write::Classic;
        }
        if(pending!=Write::None){
            const auto data=pending==Write::Pad?std::span<const std::uint8_t>(pads.front().bytes):to_tv.front();
            ERR_clear_error();const int rc=SSL_write(ssl,data.data(),static_cast<int>(data.size()));
            if(rc>0){
                if(static_cast<std::size_t>(rc)!=data.size())throw std::runtime_error("Unexpected partial TLS write");
                if(pending==Write::Classic){delivered.consume(data);to_tv.consume(static_cast<std::size_t>(rc));}
                else pads.pop_front();
                pending=Write::None;write_want=POLLOUT;tv_progress=now_ms();
            }else{
                const int error=SSL_get_error(ssl,rc);
                if(error!=SSL_ERROR_WANT_READ&&error!=SSL_ERROR_WANT_WRITE)throw std::runtime_error("TV TLS write failed");
                write_want=error==SSL_ERROR_WANT_READ?POLLIN:POLLOUT;
            }
        }
        if(!to_core.empty()){
            const auto data=to_core.front();
            const int rc=static_cast<int>(send(core,reinterpret_cast<const char*>(data.data()),static_cast<int>(data.size()),0));
            if(rc>0){to_core.consume(static_cast<std::size_t>(rc));core_progress=now_ms();}
            else if(rc==0||!would_block())throw std::runtime_error("Upstream write failed");
        }
        const bool can_read_tv=pending==Write::None&&to_core.bytes<=QueueLimit-buffer.size();
        const bool buffered=can_read_tv&&SSL_pending(ssl)>0;
        const short tv_events=static_cast<short>((can_read_tv?read_want:0)|(pending!=Write::None?write_want:0));
        const short core_events=static_cast<short>((to_tv.bytes<=QueueLimit-buffer.size()?POLLIN:0)|(!to_core.empty()?POLLOUT:0));
        std::array<PollFd,2> fds{{{tv,tv_events,0},{core,core_events,0}}};
        const bool focused_now=observed.active&&delivered.active&&observed.capture==delivered.capture;
        const bool immediate=pending==Write::None&&(!to_tv.empty()||(focused_now&&delivered.boundary()&&!pads.empty()));
        const int timeout=(buffered||immediate||focused_now!=focus)?0:focus?static_cast<int>(std::max<std::int64_t>(0,next_sample-now_ms())):250;
        const int rc=poll_sockets(fds.data(),2,timeout);
        if(rc<0){if(would_block())continue;throw std::runtime_error("Relay poll failed");}
        for(const auto& fd:fds)if(fd.revents&(POLLERR|POLLHUP|POLLNVAL))throw std::runtime_error("Relay peer closed");
        if(can_read_tv&&(buffered||(fds[0].revents&read_want))){
            ERR_clear_error();const int count=SSL_read(ssl,buffer.data(),static_cast<int>(buffer.size()));
            if(count>0){
                client_frames.consume(std::span<const std::uint8_t>(buffer.data(),static_cast<std::size_t>(count)));
                to_core.push(Bytes(buffer.begin(),buffer.begin()+count));read_want=POLLIN;
            }
            else{
                const int error=SSL_get_error(ssl,count);
                if(error!=SSL_ERROR_WANT_READ&&error!=SSL_ERROR_WANT_WRITE)throw std::runtime_error("TV TLS read closed");
                read_want=error==SSL_ERROR_WANT_READ?POLLIN:POLLOUT;
            }
        }
        if((fds[1].revents&POLLIN)&&to_tv.bytes<=QueueLimit-buffer.size()){
            const int count=static_cast<int>(recv(core,reinterpret_cast<char*>(buffer.data()),static_cast<int>(buffer.size()),0));
            if(count>0){const std::span<const std::uint8_t> data(buffer.data(),static_cast<std::size_t>(count));observed.consume(data);to_tv.push(Bytes(data.begin(),data.end()));}
            else if(count==0||!would_block())throw std::runtime_error("Upstream closed");
        }
    }
}
void session(SSL_CTX* ctx,Socket tv,const Options& o){
    nonblocking(tv);Stream ssl(SSL_new(ctx),SSL_free);
    if(!ssl||static_cast<std::uint64_t>(tv)>static_cast<std::uint64_t>(std::numeric_limits<int>::max())||SSL_set_fd(ssl.get(),static_cast<int>(tv))!=1)
        throw std::runtime_error("Cannot create TV TLS stream");
    authenticate(ssl.get(),o,tv);
    OwnedSocket core(socket(AF_INET,SOCK_STREAM,IPPROTO_TCP));
    if(core.value==InvalidSocket)throw std::runtime_error("Cannot open upstream socket");
    nonblocking(core.value);sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(static_cast<std::uint16_t>(o.upstream_port));
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(core.value,reinterpret_cast<const sockaddr*>(&address),static_cast<AddressLength>(sizeof(address)))!=0&&!would_block())throw std::runtime_error("Loopback upstream connect failed");
    wait_socket(core.value,POLLOUT,now_ms()+5000);
    int error=0;
#ifdef _WIN32
    int length=sizeof(error);
#else
    socklen_t length=sizeof(error);
#endif
    if(getsockopt(core.value,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length)!=0||error)throw std::runtime_error("Loopback upstream unavailable");
    relay(ssl.get(),tv,core.value,o);
}
}
int main(int argc,char** argv){
    if(argc==2&&(std::string(argv[1])=="--help"||std::string(argv[1])=="--version")){
        std::cout<<"Synergy input bridge 0.4.1\n--ca PEM --cert PEM --key PEM --client-sha256 HEX [--listen PRIVATE_IPV4] [--port 24801] [--upstream-port 24800] [--fixture FILE]\nXInput forwarding while TV focus is captured; PC-side controller suppression is not provided.\n";return 0;
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
        if(listener.value==InvalidSocket)throw std::runtime_error("Cannot open listener");
        int yes=1;setsockopt(listener.value,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&yes),static_cast<AddressLength>(sizeof(yes)));
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(static_cast<std::uint16_t>(o.port));
        if(inet_pton(AF_INET,o.listen.c_str(),&address.sin_addr)!=1||bind(listener.value,reinterpret_cast<const sockaddr*>(&address),static_cast<AddressLength>(sizeof(address)))!=0||listen(listener.value,1)!=0)
            throw std::runtime_error("Cannot bind private listener");
        nonblocking(listener.value);
        std::cout<<"Bridge ready; input capture is focus-gated. PC controller suppression is not provided.\n"<<std::flush;
        while(running){
            PollFd item{listener.value,POLLIN,0};int rc=poll_sockets(&item,1,250);
            if(rc<0){if(would_block())continue;throw std::runtime_error("Listener poll failed");}
            if(!(item.revents&POLLIN))continue;
            OwnedSocket tv(accept(listener.value,nullptr,nullptr));if(tv.value==InvalidSocket)continue;
            try{session(ctx.get(),tv.value,o);}catch(const std::exception& error){std::cerr<<"Session closed: "<<error.what()<<'\n';}
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
