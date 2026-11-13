#include "synergy/client.hpp"
#include "synergy/input.hpp"
#include "synergy/ipc.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <cmath>
namespace synergy {
    namespace {
        volatile sig_atomic_t stopped=0;
        volatile sig_atomic_t shutdown_fd=-1;
        void signal_stop(int){
            const int saved_errno=errno;
            stopped=1;
            const int fd=shutdown_fd;
            if(fd>=0){
                const char byte=1;
                ssize_t written;
                do { written=write(fd,&byte,1); } while(written<0&&errno==EINTR);
            }
            errno=saved_errno;
        }
        struct ShutdownWake {
            int fds[2]{-1,-1};
            struct sigaction old_term{},old_int{};
            ShutdownWake(){
                if(pipe2(fds,O_CLOEXEC|O_NONBLOCK)!=0)throw std::runtime_error("Cannot create daemon shutdown wakeup");
                struct sigaction action{};
                action.sa_handler=signal_stop;
                sigemptyset(&action.sa_mask);
                shutdown_fd=fds[1];
                if(sigaction(SIGTERM,&action,&old_term)!=0){
                    shutdown_fd=-1;close(fds[0]);close(fds[1]);
                    throw std::runtime_error("Cannot install daemon termination handler");
                }
                if(sigaction(SIGINT,&action,&old_int)!=0){
                    sigaction(SIGTERM,&old_term,nullptr);
                    shutdown_fd=-1;close(fds[0]);close(fds[1]);
                    throw std::runtime_error("Cannot install daemon interrupt handler");
                }
            }
            ~ShutdownWake(){
                sigaction(SIGTERM,&old_term,nullptr);
                sigaction(SIGINT,&old_int,nullptr);
                shutdown_fd=-1;
                close(fds[0]);close(fds[1]);
            }
        };
        const std::filesystem::path Hook="/var/lib/webosbrew/init.d/60-synergy-native";
        bool legacy_running(){
            std::ifstream pidfile("/var/lib/webosbrew/junction.pid");
            long pid=0;
            pidfile>>pid;
            if(!pidfile||pid<=1||pid>std::numeric_limits<int>::max())return false;
            std::ifstream f("/proc/"+std::to_string(pid)+"/cmdline",std::ios::binary);
            std::string s(8192,'\0');
            f.read(s.data(),static_cast<std::streamsize>(s.size()));
            s.resize(static_cast<size_t>(f.gcount()));
            return s.find("client.py")!=std::string::npos&&(s.find("synergywebos")!=std::string::npos||s.find("junction")!=std::string::npos);
        }
        bool managed_hook(){
            try{
                return read_private_file(Hook,2048).find("# synergy-native managed")!=std::string::npos;
            }
            catch(...){
                return false;
            }
        }
        void autostart(bool enable,const std::filesystem::path& root){
#ifndef SYNERGY_WEBOS
            (void)enable;
            (void)root;
            throw std::runtime_error("Autostart writes are disabled in desktop builds");
#else
            if(geteuid()!=0||root!="/var/lib/webosbrew/synergy-native")throw std::runtime_error("Autostart requires provisioned root runtime");
            if(enable){
                for(auto* old:{
                    "/var/lib/webosbrew/init.d/60-junction","/var/lib/webosbrew/init.d/60-synergy-webos"}
                )if(std::filesystem::exists(old))throw std::runtime_error("Legacy boot hook exists; disable it explicitly in 0.3.4 before enabling native autostart");
                auto launcher=root/"start-native.sh";
                auto content=read_private_file(launcher,4096);
                if(content.find("# synergy-native provisioned launcher")==std::string::npos)throw std::runtime_error("Root runtime has not been provisioned");
                if(std::filesystem::exists(Hook)&&!managed_hook())throw std::runtime_error("Refusing to replace unmanaged startup hook");
                atomic_private_write(Hook,"#!/bin/sh\n# synergy-native managed v0.4.0\n( sleep 8; exec /var/lib/webosbrew/synergy-native/start-native.sh --start-from-config ) >/dev/null 2>&1 &\nexit 0\n");
                if(chmod(Hook.c_str(),0700)!=0)throw std::runtime_error("Could not set hook mode");
            }
            else if(std::filesystem::exists(Hook)){
                if(!managed_hook())throw std::runtime_error("Refusing to delete unmanaged startup hook");
                if(unlink(Hook.c_str())!=0)throw std::runtime_error("Cannot remove startup hook");
            }
#endif
        }
        void validate_profile(const Json& j){
            if(!j.object_type()||j.dump().size()>4096)throw std::runtime_error("Profile must be a small JSON object");
            const std::set<std::string> keys{
                "corner","cornerPixels","switchDelayMs","switchDoubleTapMs","note"}
            ;
            for(auto [k,v]:j.members()){
                if(!keys.contains(k))throw std::runtime_error("Unknown profile field");
                if(k=="corner"){
                    auto s=v.str();
                    if(s!="top-left"&&s!="top-right"&&s!="bottom-left"&&s!="bottom-right")throw std::runtime_error("Invalid corner");
                }
                else if(k=="note"){
                    if(!v.is(json_type_string)||v.str().size()>500)throw std::runtime_error("Invalid profile note");
                }
                else {
                    double n=v.num(-1);
                    if(!(v.is(json_type_int)||v.is(json_type_double))||n<0||n>5000||std::floor(n)!=n)throw std::runtime_error("Invalid profile value");
                }
            }
        }
    }
    int daemon_main(int argc,char** argv){
        auto root=default_runtime();
        uid_t controller=geteuid();
        bool uid_given=false,start=false,once=false;
        for(int i=1;i<argc;++i){
            std::string a=argv[i];
            auto value=[&]{
                if(++i>=argc)throw std::runtime_error("Missing argument value");
                return std::string(argv[i]);
            }
            ;
            if(a=="--runtime-dir")root=value();
            else if(a=="--controller-uid"){
                controller=static_cast<uid_t>(checked_integer(value(),0,std::numeric_limits<int>::max()));
                uid_given=true;
            }
            else if(a=="--start-from-config")start=true;
            else if(a=="--once")once=true;
            else if(a=="--version"){
                std::cout<<SYNERGY_VERSION<<'\n';
                return 0;
            }
            else if(a=="--help"){
                std::cout<<"synergy-daemon "<<SYNERGY_VERSION<<"\n--runtime-dir ABS --controller-uid UID [--start-from-config] [--once; requires start]\nNo input until an explicit start request. Desktop builds are dry-run only.\n";
                return 0;
            }
            else throw std::runtime_error("Unknown daemon option: "+a);
        }
        if(once&&!start)throw std::runtime_error("--once requires --start-from-config");
#ifdef SYNERGY_WEBOS
        if(geteuid()!=0||!uid_given)throw std::runtime_error("Launch the daemon through the explicit root provisioning route with --controller-uid");
#else
        (void)uid_given;
#endif
        Broker broker(root,controller);
        Platform platform;
        Client client(platform);
        Config saved;
        auto config_file=root/"config.json";
        std::string config_error;
        if(std::filesystem::exists(config_file))try{
            saved=Config::from_json(Json::parse(read_private_file(config_file)));
        }
        catch(const std::exception& e){
            config_error=e.what();
        }
        ShutdownWake shutdown;
        signal(SIGPIPE,SIG_IGN);
        auto begin=[&](bool run_once){
            if(!config_error.empty())throw std::runtime_error("Saved config invalid; preserved on disk. Repair it explicitly before Start.");
            if(!saved.dry_run&&legacy_running())throw std::runtime_error("Legacy Python client is running; stop it explicitly before native real input");
            client.start(saved,run_once);
        }
        ;
        if(start)begin(once);
        auto dispatch=[&](const Json& j)->Json{
            auto op=j.get("op").str();
            if(op.empty())throw std::runtime_error("Missing operation");
            const std::set<std::string> fields{
                "op","config","profile","id","once"}
            ;
            for(auto [k,v]:j.members()){
                (void)v;
                if(!fields.contains(k))throw std::runtime_error("Unknown request field");
            }
            if(op=="status"||op=="snapshot"||op=="logs"){
                auto result=okay();
                result.set("version",Json::string(SYNERGY_VERSION));
                result.set("schema",Json::number(synergy::ControlSchema));
                result.set("pid",Json::number(getpid()));
                result.set("controller_uid",Json::number(controller));
                result.set("client",client.snapshot());
                result.set("saved_config",saved.json());
                result.set("config_error",Json::string(config_error));
                result.set("platform",platform.snapshot());
                result.set("autostart",Json::boolean(managed_hook()));
                result.set("legacy_running",Json::boolean(legacy_running()));
                return result;
            }
            if(op=="probe")return probe_input();
            if(op=="configure"){
                if(!config_error.empty())throw std::runtime_error("Malformed existing config is preserved. Move it aside explicitly after review; then restart daemon.");
                auto candidate=Config::from_json(j.get("config"));
                candidate.validate(false);
                atomic_private_write(config_file,candidate.json().dump(true)+"\n");
                saved=std::move(candidate);
                return okay();
            }
            if(op=="start"||op=="restart"){
                if(!j.get("once").empty()&&!j.get("once").is(json_type_boolean))throw std::runtime_error("once must be boolean");
                saved.validate();
                if(op=="restart")client.stop();
                begin(j.get("once").flag());
                return okay();
            }
            if(op=="stop"){
                client.stop();
                return okay();
            }
            if(op=="autostart-enable"||op=="autostart-disable"){
                if(op=="autostart-enable")saved.validate();
                autostart(op=="autostart-enable",root);
                return okay();
            }
            if(op=="profile-save"){
                auto profile=j.get("profile");
                validate_profile(profile);
                atomic_private_write(root/"handoff-profile.json",profile.dump(true)+"\n");
                auto result=okay();
                result.set("applied_to_desktop",Json::boolean(false));
                return result;
            }
            if(op=="profile-load"){
                auto result=okay();
                auto path=root/"handoff-profile.json";
                result.set("profile",std::filesystem::exists(path)?Json::parse(read_private_file(path,4096)):Json::object());
                return result;
            }
            throw std::runtime_error("Operation not allowed");
        }
        ;
        std::cout<<"SynergyWebOS native daemon "<<SYNERGY_VERSION<<"; local UID-scoped control; no TCP listener\n"<<std::flush;
        while(!stopped){
            // Normal idle service blocks until local control or a shutdown
            // signal. --once is a diagnostic mode that also watches completion
            // of the client worker without a new IPC request.
            broker.poll_once(dispatch,once?25:-1,shutdown.fds[0]);
            if(start&&once&&!client.running())break;
        }
        client.stop();
        return 0;
    }
}
int main(int argc,char** argv){
    try{
        return synergy::daemon_main(argc,argv);
    }
    catch(const std::exception& e){
        std::cerr<<"synergy-daemon: "<<e.what()<<'\n';
        return 2;
    }
}
