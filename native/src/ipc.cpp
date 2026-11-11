#include "synergy/ipc.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <array>
#include <cstring>
#include <cerrno>
namespace synergy {
    namespace {
        constexpr size_t RequestLimit=16384,ResponseLimit=131072;
        struct Fd {
            int n;
            explicit Fd(int f):n(f){
            }
            ~Fd(){
                if(n>=0)close(n);
            }
            Fd(const Fd&)=delete;
        }
        ;
        sockaddr_un address(const std::filesystem::path& p){
            sockaddr_un a{
            }
            ;
            a.sun_family=AF_UNIX;
            auto s=p.string();
            if(s.size()>=sizeof(a.sun_path))throw std::runtime_error("Unix socket path too long");
            std::memcpy(a.sun_path,s.c_str(),s.size()+1);
            return a;
        }
        void wait_io(int fd,short flags,int64_t until){
            for(;;){
                auto left=until-monotonic_ms();
                if(left<=0)throw std::runtime_error("Local control deadline expired");
                pollfd p{
                    fd,flags,0}
                ;
                int n=poll(&p,1,static_cast<int>(std::min<int64_t>(left,100)));
                if(n<0&&errno==EINTR)continue;
                if(n<0)throw std::runtime_error("Local control poll failed");
                if(n==0)continue;
                if(p.revents&flags)return;
                throw std::runtime_error("Local control disconnected");
            }
        }
        void send_all(int fd,const void* p,size_t n,int64_t until){
            auto* b=static_cast<const char*>(p);
            while(n){
                wait_io(fd,POLLOUT,until);
                auto w=send(fd,b,n,MSG_NOSIGNAL);
                if(w<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
                if(w<=0)throw std::runtime_error("Local control send failed");
                b+=w;
                n-=static_cast<size_t>(w);
            }
        }
        void receive_all(int fd,void* p,size_t n,int64_t until){
            auto* b=static_cast<char*>(p);
            while(n){
                wait_io(fd,POLLIN,until);
                auto w=recv(fd,b,n,0);
                if(w<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
                if(w<=0)throw std::runtime_error("Local control closed");
                b+=w;
                n-=static_cast<size_t>(w);
            }
        }
        void write_json(int fd,const Json& j,size_t max,int64_t until){
            auto s=j.dump();
            if(s.size()>max)throw std::runtime_error("Control response too large");
            auto n=static_cast<uint32_t>(s.size());
            const std::array<uint8_t,4> header{
                {
                    uint8_t(n>>24),uint8_t(n>>16),uint8_t(n>>8),uint8_t(n)}
            }
            ;
            send_all(fd,header.data(),header.size(),until);
            send_all(fd,s.data(),s.size(),until);
        }
        Json read_json(int fd,size_t limit,int64_t until){
            std::array<uint8_t,4> h{
            }
            ;
            receive_all(fd,h.data(),4,until);
            uint32_t n=(uint32_t(h[0])<<24)|(uint32_t(h[1])<<16)|(uint32_t(h[2])<<8)|h[3];
            if(n<2||n>limit)throw std::runtime_error("Invalid control frame size");
            std::string s(n,'\0');
            receive_all(fd,s.data(),s.size(),until);
            auto j=Json::parse(s,limit);
            if(!j.object_type())throw std::runtime_error("Control request must be a JSON object");
            return j;
        }
        uid_t peer_uid(int fd){
            ucred credentials{
            }
            ;
            socklen_t size=sizeof credentials;
            if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&credentials,&size)!=0||size!=sizeof credentials)throw std::runtime_error("Cannot verify local peer identity");
            return credentials.uid;
        }
    }
    Broker::Broker(const std::filesystem::path& root,uid_t uid):path_(root/"control.sock"),controller_(uid){
        validate_runtime(root,true);
        try{
            auto lockpath=root/"daemon.lock";
            lock_=open(lockpath.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
            if(lock_<0)throw std::runtime_error("Cannot open daemon lock");
            struct stat st{
            }
            ;
            if(fstat(lock_,&st)!=0||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_nlink!=1||(st.st_mode&0077))throw std::runtime_error("Unsafe daemon lock");
            if(flock(lock_,LOCK_EX|LOCK_NB)!=0)throw std::runtime_error("Another native daemon already owns this runtime");
            if(lstat(path_.c_str(),&st)==0){
                if(!S_ISSOCK(st.st_mode)||st.st_uid!=geteuid())throw std::runtime_error("Refusing to replace non-owned socket path");
                if(unlink(path_.c_str())!=0)throw std::runtime_error("Cannot remove stale socket");
            }
            socket_=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
            if(socket_<0)throw std::runtime_error("Cannot create control socket");
            auto addr=address(path_);
            if(bind(socket_,reinterpret_cast<sockaddr*>(&addr),sizeof addr)!=0)throw std::runtime_error("Cannot bind control socket");
            if(lstat(path_.c_str(),&st)!=0)throw std::runtime_error("Cannot inspect control socket");
            inode_=st.st_ino;
            // Directory is immutable to controller; access is checked with kernel SO_PEERCRED before any request bytes are read.
            if(chmod(path_.c_str(),0666)!=0||listen(socket_,4)!=0)throw std::runtime_error("Cannot publish control socket");
        }
        catch(...){
            if(inode_)unlink(path_.c_str());
            if(socket_>=0)close(socket_);
            if(lock_>=0)close(lock_);
            throw;
        }
    }
    Broker::~Broker(){
        if(socket_>=0)close(socket_);
        struct stat s{
        }
        ;
        if(inode_&&lstat(path_.c_str(),&s)==0&&s.st_ino==inode_&&S_ISSOCK(s.st_mode)&&s.st_uid==geteuid())unlink(path_.c_str());
        if(lock_>=0)close(lock_);
    }
    void Broker::poll_once(const std::function<Json(const Json&)>& handle,int timeout){
        pollfd p{
            socket_,POLLIN,0}
        ;
        int n=poll(&p,1,timeout);
        if(n<=0)return;
        Fd c(accept4(socket_,nullptr,nullptr,SOCK_CLOEXEC|SOCK_NONBLOCK));
        if(c.n<0)return;
        try{
            uid_t uid=peer_uid(c.n);
            if(uid!=0&&uid!=controller_)return;
            auto j=read_json(c.n,RequestLimit,monotonic_ms()+500);
            Json result;
            try{
                result=handle(j);
            }
            catch(const std::exception& e){
                result=failure(e.what());
            }
            write_json(c.n,result,ResponseLimit,monotonic_ms()+1000);
        }
        catch(const std::exception&){
            /* One malformed or abandoned local request must not stop the daemon. */}
    }
    Json request(const std::filesystem::path& root,const Json& j,int timeout){
        auto path=root/"control.sock";
        struct stat st{
        }
        ;
#ifdef SYNERGY_WEBOS
        uid_t expected=0;
#else
        uid_t expected=geteuid();
#endif
        if(lstat(path.c_str(),&st)!=0||!S_ISSOCK(st.st_mode)||st.st_uid!=expected)throw std::runtime_error("Native daemon unavailable or socket owner invalid; UI does not grant root");
        Fd fd(socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0));
        if(fd.n<0)throw std::runtime_error("Cannot create control connection");
        auto a=address(path);
        auto until=monotonic_ms()+timeout;
        if(connect(fd.n,reinterpret_cast<sockaddr*>(&a),sizeof a)!=0&&errno!=EINPROGRESS&&errno!=EAGAIN)throw std::runtime_error("Cannot connect to native daemon");
        wait_io(fd.n,POLLOUT,until);
        int err=0;
        socklen_t len=sizeof err;
        if(getsockopt(fd.n,SOL_SOCKET,SO_ERROR,&err,&len)!=0||err)throw std::runtime_error("Native daemon connection failed");
        if(peer_uid(fd.n)!=expected)throw std::runtime_error("Unexpected daemon UID");
        write_json(fd.n,j,RequestLimit,until);
        return read_json(fd.n,ResponseLimit,until);
    }
}
