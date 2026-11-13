#include "synergy/platform.hpp"
#include <array>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

namespace synergy {
    ProcessResult bounded_process(const std::string& exe,const std::vector<std::string>& args,int timeout_ms,size_t limit){
        if(exe.empty()||exe.find('\0')!=std::string::npos||timeout_ms<1||timeout_ms>30000||limit<1||limit>1048576)throw std::runtime_error("Invalid process limits");
        for(const auto& a:args)if(a.find('\0')!=std::string::npos)throw std::runtime_error("Invalid process argument");
        ProcessResult result;
#ifdef _WIN32
        (void)exe;
        (void)args;
        (void)timeout_ms;
        (void)limit;
        throw std::runtime_error("Native Luna transport is available only on webOS/Linux");
#else
        int pipes[2];
        if(pipe(pipes)!=0)throw std::runtime_error("Cannot create Luna pipe");
        fcntl(pipes[0],F_SETFD,FD_CLOEXEC);
        fcntl(pipes[1],F_SETFD,FD_CLOEXEC);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions,pipes[1],STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions,pipes[0]);
        posix_spawn_file_actions_addclose(&actions,pipes[1]);
        posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
        posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);
        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
        posix_spawnattr_setflags(&attr,POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attr,0);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(exe.c_str()));
        for(auto& a:args)argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        pid_t pid=-1;
        char fixed_path[]="PATH=/usr/bin:/bin";
        char locale[]="LC_ALL=C";
        char* child_env[]={fixed_path,locale,nullptr};
        int err=posix_spawn(&pid,exe.c_str(),&actions,&attr,argv.data(),child_env);
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attr);
        close(pipes[1]);
        if(err!=0){
            close(pipes[0]);
            throw std::runtime_error("Native service executable unavailable or blocked");
        }
        fcntl(pipes[0],F_SETFL,fcntl(pipes[0],F_GETFL)|O_NONBLOCK);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);
        bool exited=false,eof=false;
        int status=0;
        std::array<char,4096> buffer{
        }
        ;
        while(!exited||!eof){
            auto now=std::chrono::steady_clock::now();
            if(now>=deadline){
                result.timed_out=true;
                kill(-pid,SIGKILL);
                break;
            }
            if(result.too_large){
                kill(-pid,SIGKILL);
                break;
            }
            pollfd fd{
                eof?-1:pipes[0],POLLIN|POLLHUP,0
            }
            ;
            // POLLHUP remains ready forever. After EOF, poll only as a bounded
            // wait for child exit; keep checking waitpid and the same deadline.
            int wait=std::min(20,static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline-now).count()));
            int polled=poll(&fd,1,std::max(1,wait));
            if(polled<0&&errno!=EINTR){
                kill(-pid,SIGKILL);
                break;
            }
            if(polled>0){
                for(;;){
                    ssize_t n=read(pipes[0],buffer.data(),buffer.size());
                    if(n==0){
                        eof=true;
                        break;
                    }
                    if(n<0){
                        if(errno!=EAGAIN&&errno!=EINTR)eof=true;
                        break;
                    }
                    size_t amount=static_cast<size_t>(n);
                    if(result.output.size()+amount>limit){
                        result.too_large=true;
                        break;
                    }
                    result.output.append(buffer.data(),amount);
                }
            }
            if(!exited){
                pid_t w=waitpid(pid,&status,WNOHANG);
                if(w==pid)exited=true;
                else if(w<0&&errno!=EINTR){
                    exited=true;
                    status=127<<8;
                }
            }
        }
        close(pipes[0]);
        if(!exited){
            while(waitpid(pid,&status,0)<0&&errno==EINTR){
            }
        }
        result.exit_code=WIFEXITED(status)?WEXITSTATUS(status):-1;
        return result;
#endif
    }
    Platform::Platform():worker_(&Platform::run,this){
    }
    Platform::~Platform(){
        {
            std::lock_guard lock(mutex_);
            stopping_=true;
            work_.clear();
        }
        cv_.notify_all();
        if(worker_.joinable())worker_.join();
    }
    void Platform::update(const std::string& s){
        std::lock_guard lock(mutex_);
        status_=s;
    }
    bool Platform::post(std::string kind,bool dry,uint64_t gen){
        if(kind!="home"&&kind!="cycle+"&&kind!="cycle-"&&kind!="cycle-reset")throw std::runtime_error("Unknown platform action");
        std::lock_guard lock(mutex_);
        if(stopping_||work_.size()>=8)return false;
        work_.push_back({
            std::move(kind),dry,gen}
        );
        cv_.notify_one();
        return true;
    }
    void Platform::reset_cycle(uint64_t gen){
        if(!post("cycle-reset",false,gen))update("TV action queue full; cycle cache not reset");
    }
    void Platform::action(Action a,bool dry,uint64_t gen){
        if(!post(a==Action::Home?"home":a==Action::CycleForward?"cycle+":"cycle-",dry,gen))update("TV action queue full; request rejected");
    }
    Json Platform::snapshot()const{
        std::lock_guard lock(mutex_);
        auto j=Json::object();
        j.set("message",Json::string(status_));
        j.set("queued",Json::number(static_cast<double>(work_.size())));
        return j;
    }
    Json Platform::luna(const char* uri,const Json& payload){
#ifndef SYNERGY_WEBOS
        (void)uri;
        (void)payload;
        throw std::runtime_error("TV services are disabled in desktop builds");
#else
        const std::string u=uri;
        if(u!="luna://com.webos.applicationManager/launch"&&u!="luna://com.webos.applicationManager/dev/running"&&u!="luna://com.webos.applicationManager/getForegroundAppInfo")throw std::runtime_error("Luna method not allowlisted");
        auto r=bounded_process("/usr/bin/luna-send",{
            "-n","1","-f",u,payload.dump()}
        ,2000,131072);
        if(r.exit_code!=0||r.timed_out||r.too_large)throw std::runtime_error("TV service denied, timed out or exceeded response limit");
        auto j=Json::parse(r.output);
        if(!j.object_type()||!j.get("returnValue").flag())throw std::runtime_error("TV service returned failure");
        return j;
#endif
    }
    std::vector<std::string> Platform::running(){
        auto j=luna("luna://com.webos.applicationManager/dev/running",Json::object());
        auto items=j.get("running");
        if(!items.is(json_type_array))items=j.get("apps");
        if(!items.is(json_type_array))throw std::runtime_error("Unexpected running-app response");
        std::vector<std::string> ids;
        for(size_t i=0;i<std::min<size_t>(items.size(),128);++i){
            auto item=items.at(i);
            auto id=item.get("id").str(item.get("appId").str());
            if(!safe_app_id(id)||id==HomeApp||std::find(ids.begin(),ids.end(),id)!=ids.end())continue;
            ids.push_back(id);
        }
        return ids;
    }
    void Platform::run(){
        for(;;){
            Work w;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock,[&]{
                    return stopping_||!work_.empty();}
                );
                if(stopping_)return;
                w=std::move(work_.front());
                work_.pop_front();
            }
            if(w.generation&&w.generation!=generation()){update("Action dropped: capture ended");continue;}
            if(w.kind=="cycle-reset"){
                cycle_.clear();
                cycle_at_=0;
                cycle_generation_=0;
                continue;
            }
            try{
                if(w.dry){
                    update("DRY RUN: "+w.kind+" (no TV service call)");
                    continue;
                }
                std::string target=HomeApp;
                if(w.kind=="cycle+"||w.kind=="cycle-"){
                    auto now=monotonic_ms();
                    if(cycle_.empty()||now-cycle_at_>1500||cycle_generation_!=w.generation){
                        cycle_=running();
                        auto foreground=luna("luna://com.webos.applicationManager/getForegroundAppInfo",Json::object()).get("appId").str();
                        auto it=std::find(cycle_.begin(),cycle_.end(),foreground);
                        cycle_index_=it==cycle_.end()?-1:static_cast<int>(it-cycle_.begin());
                        cycle_generation_=w.generation;
                    }
                    if(cycle_.empty())throw std::runtime_error("No running apps to switch to");
                    int n=static_cast<int>(cycle_.size());
                    cycle_index_=(cycle_index_+(w.kind=="cycle-"?-1:1)+n)%n;
                    target=cycle_[static_cast<size_t>(cycle_index_)];
                    cycle_at_=now;
                }
                if(w.generation&&w.generation!=generation()){update("Action dropped: capture ended");continue;}
                if(!safe_app_id(target))throw std::runtime_error("Invalid application ID");
                auto payload=Json::object();
                payload.set("id",Json::string(target));
                luna("luna://com.webos.applicationManager/launch",payload);
                update("Launch accepted: "+target+" (visual result needs TV verification)");
            }
            catch(const std::exception& e){
                update(e.what());
            }
        }
    }
}
