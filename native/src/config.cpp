#include "synergy/config.hpp"
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <cstring>
#include <set>
#include <limits>
namespace synergy {
    namespace {
        struct Fd {
            int n;
            explicit Fd(int fd):n(fd){
            }
            ~Fd(){
                if(n>=0)::close(n);
            }
        }
        ;
        void fail(const std::string& s){
            throw std::runtime_error(s+": "+std::strerror(errno));
        }
        void require_regular(int fd){
            struct stat st{
            }
            ;
            if(fstat(fd,&st)!=0)fail("stat file");
            if(!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_nlink!=1||(st.st_mode&0077))throw std::runtime_error("State file must be private, regular, single-linked and owned by the daemon UID");
        }
    }
    int checked_integer(const std::string& s,int lo,int hi){
        int n=0;
        auto r=std::from_chars(s.data(),s.data()+s.size(),n);
        if(r.ec!=std::errc()||r.ptr!=s.data()+s.size()||n<lo||n>hi)throw std::runtime_error("Invalid integer");
        return n;
    }
    bool numeric_lan_address(const std::string& s){
        in_addr a{
        }
        ;
        if(inet_pton(AF_INET,s.c_str(),&a)==1){
            auto v=ntohl(a.s_addr);
            return (v>>24)==10||(v>>24)==127||(v>>20)==0xac1||(v>>16)==0xc0a8||(v>>16)==0xa9fe;
        }
        in6_addr b{
        }
        ;
        if(inet_pton(AF_INET6,s.c_str(),&b)!=1)return false;
        return IN6_IS_ADDR_LOOPBACK(&b)||(b.s6_addr[0]&0xfe)==0xfc;
        // ULA or loopback. Scoped link-local deliberately unsupported.
    }
    bool safe_app_id(const std::string& s){
        if(s.empty()||s.size()>128||s.front()=='.'||s.back()=='.')return false;
        for(unsigned char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;
        return true;
    }
    void Config::validate(bool connecting)const{
        if((connecting||!server.empty())&&!numeric_lan_address(server))throw std::runtime_error("Desktop must be a numeric private-LAN or loopback IP (no hostnames/public Internet)");
        if(port<1||port>65535||width<1||width>32767||height<1||height>32767)throw std::runtime_error("Invalid port or screen dimensions (1..32767)");
        if(name.empty()||name.size()>64)throw std::runtime_error("Screen name must be 1..64 ASCII letters, digits, '.', '_' or '-'");
        for(unsigned char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))throw std::runtime_error("Unsafe screen name");
        if(home_target!="system"&&home_target!="off")throw std::runtime_error("Home target must be system or off");
        if(home_delay_ms<0||home_delay_ms>5000||idle_timeout_ms<3000||idle_timeout_ms>60000)throw std::runtime_error("Invalid timeout");
        if(connecting&&!dry_run&&!allow_plaintext)throw std::runtime_error("Real input requires explicit allow_plaintext on a trusted LAN; TLS is not implemented");
    }
    Json Config::json()const{
        auto j=Json::object();
        j.set("server",Json::string(server));
        j.set("port",Json::number(port));
        j.set("name",Json::string(name));
        j.set("width",Json::number(width));
        j.set("height",Json::number(height));
        j.set("dry_run",Json::boolean(dry_run));
        j.set("allow_plaintext",Json::boolean(allow_plaintext));
        j.set("cursor_keepalive",Json::boolean(cursor_keepalive));
        j.set("home_target",Json::string(home_target));
        j.set("home_delay_ms",Json::number(home_delay_ms));
        j.set("idle_timeout_ms",Json::number(idle_timeout_ms));
        return j;
    }
    Config Config::from_json(const Json& j){
        if(!j.object_type())throw std::runtime_error("Config must be an object");
        Config c;
        const std::set<std::string> allowed{
            "server","port","name","width","height","dry_run","allow_plaintext","cursor_keepalive","home_target","home_delay_ms","idle_timeout_ms"}
        ;
        for(const auto& [k,v]:j.members()){
            (void)v;
            if(!allowed.contains(k))throw std::runtime_error("Unknown config field: "+k);
        }
        auto text=[&](const char* k,std::string& v){
            auto x=j.get(k);
            if(x.empty())return;
            if(!x.is(json_type_string)||x.str().find('\0')!=std::string::npos)throw std::runtime_error(std::string("Invalid string: ")+k);
            v=x.str();
        }
        ;
        auto number=[&](const char* k,int& v){
            auto x=j.get(k);
            if(x.empty())return;
            if(!(x.is(json_type_int)||x.is(json_type_double))||!std::isfinite(x.num())||std::floor(x.num())!=x.num()||x.num()<0||x.num()>65535)throw std::runtime_error(std::string("Invalid integer: ")+k);
            v=static_cast<int>(x.num());
        }
        ;
        auto flag=[&](const char* k,bool& v){
            auto x=j.get(k);
            if(x.empty())return;
            if(!x.is(json_type_boolean))throw std::runtime_error(std::string("Invalid boolean: ")+k);
            v=x.flag();
        }
        ;
        text("server",c.server);
        text("name",c.name);
        text("home_target",c.home_target);
        number("port",c.port);
        number("width",c.width);
        number("height",c.height);
        number("home_delay_ms",c.home_delay_ms);
        number("idle_timeout_ms",c.idle_timeout_ms);
        flag("dry_run",c.dry_run);
        flag("allow_plaintext",c.allow_plaintext);
        flag("cursor_keepalive",c.cursor_keepalive);
        c.validate(false);
        return c;
    }
    std::filesystem::path default_runtime(){
#ifdef SYNERGY_WEBOS
        return "/var/lib/webosbrew/synergy-native";
#else
        return std::filesystem::path("/tmp")/("synergy-native-"+std::to_string(geteuid()));
#endif
    }
    void validate_runtime(const std::filesystem::path& p,bool create){
        if(!p.is_absolute()||p.lexically_normal()!=p||p=="/"||p.string().size()>75)throw std::runtime_error("Runtime path must be absolute, normalized, and <=75 bytes");
        // Each existing ancestor must be a real directory. World-writable sticky /tmp is allowed as an ancestor only.
        std::filesystem::path part="/";
        for(const auto& c:p.relative_path()){
            part/=c;
            struct stat s{
            }
            ;
            if(lstat(part.c_str(),&s)!=0){
                if(errno!=ENOENT||!create||part!=p)fail("Runtime parent missing");
                if(mkdir(part.c_str(),0755)!=0)fail("Create runtime");
                if(lstat(part.c_str(),&s)!=0)fail("stat runtime");
            }
            if(!S_ISDIR(s.st_mode)||S_ISLNK(s.st_mode))throw std::runtime_error("Runtime path contains a non-directory/symlink");
            if(part==p){
                if(s.st_uid!=geteuid()||(s.st_mode&0022))throw std::runtime_error("Runtime must be owned by daemon UID and not group/world writable");
            }
            else if((s.st_mode&0022)&&!(s.st_mode&S_ISVTX))throw std::runtime_error("Unsafe runtime ancestor");
        }
    }
    std::string read_private_file(const std::filesystem::path& p,size_t limit){
        Fd fd(open(p.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));
        if(fd.n<0)fail("Read state");
        require_regular(fd.n);
        std::string s;
        char b[4096];
        for(;;){
            auto n=read(fd.n,b,sizeof b);
            if(n<0){
                if(errno==EINTR)continue;
                fail("Read state");
            }
            if(!n)break;
            if(s.size()+static_cast<size_t>(n)>limit)throw std::runtime_error("State file too large");
            s.append(b,static_cast<size_t>(n));
        }
        return s;
    }
    void atomic_private_write(const std::filesystem::path& p,const std::string& s){
        validate_runtime(p.parent_path(),false);
        struct stat old{
        }
        ;
        if(lstat(p.c_str(),&old)==0&&(!S_ISREG(old.st_mode)||old.st_uid!=geteuid()||old.st_nlink!=1||(old.st_mode&0077)))throw std::runtime_error("Refusing unsafe state destination");
        auto tmp=p.string()+".tmp-"+std::to_string(getpid());
        Fd fd(open(tmp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600));
        if(fd.n<0)fail("Create temporary state");
        try{
            size_t at=0;
            while(at<s.size()){
                auto n=write(fd.n,s.data()+at,s.size()-at);
                if(n<0){
                    if(errno==EINTR)continue;
                    fail("Write state");
                }
                if(n==0)throw std::runtime_error("Short state write");
                at+=static_cast<size_t>(n);
            }
            if(fsync(fd.n)!=0)fail("Sync state");
            if(rename(tmp.c_str(),p.c_str())!=0)fail("Replace state");
            Fd dir(open(p.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC));
            if(dir.n<0||fsync(dir.n)!=0)fail("Sync state directory");
        }
        catch(...){
            unlink(tmp.c_str());
            throw;
        }
    }
    int64_t monotonic_ms(){
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    Json okay(){
        auto j=Json::object();
        j.set("ok",Json::boolean(true));
        return j;
    }
    Json failure(const std::string& s){
        auto j=Json::object();
        j.set("ok",Json::boolean(false));
        j.set("error",Json::string(s));
        return j;
    }
}
