#include "synergy/ls2_runtime.hpp"
#include "synergy/ls2_transport.hpp"
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <signal.h>
#include <thread>
#include <unistd.h>
namespace synergy {
namespace {
class Server {
    Ls2Runtime& api_;
    LSHandle* handle_;
    GMainContext* context_;
    bool verified_,mutations_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_=false;
    size_t credits_=0;
    struct Job {
        Server* owner;LSMessage* message;Json request,result;std::string op;
        ~Job(){owner->api_.LSMessageUnref_(message);}
    };
    std::deque<std::unique_ptr<Job>> work_;
    std::unique_ptr<Job> urgent_;
    std::vector<std::pair<GSource*,Job*>> replies_;
    std::thread worker_;
    void reply(LSMessage* message,const Json& result)noexcept{
        try{
            auto text=result.dump();
            if(text.size()>131072)text=failure("Bridge response too large").dump();
            Ls2Error error(api_);
            if(!api_.LSMessageReply_(handle_,message,text.c_str(),&error.value))std::cerr<<"LS2_REPLY_FAILED\n";
        }catch(...){std::cerr<<"LS2_REPLY_FAILED\n";}
    }
    static gboolean deliver(void* data)noexcept{
        std::unique_ptr<Job> job(static_cast<Job*>(data));auto& self=*job->owner;
        self.reply(job->message,job->result);
        {
            std::lock_guard lock(self.mutex_);
            const auto it=std::find_if(self.replies_.begin(),self.replies_.end(),[&](const auto& record){return record.second==job.get();});
            if(it!=self.replies_.end()){self.api_.g_source_unref_(it->first);self.replies_.erase(it);}
            --self.credits_;
        }
        return G_SOURCE_REMOVE;
    }
    void publish(std::unique_ptr<Job> job){
        auto* source=api_.g_idle_source_new_();
        if(!source){std::lock_guard lock(mutex_);--credits_;return;}
        api_.g_source_set_callback_(source,&Server::deliver,job.get(),nullptr);
        // Keep the record installed before another context iteration can deliver.
        std::lock_guard lock(mutex_);
        replies_.push_back({source,job.get()});
        if(!api_.g_source_attach_(source,context_)){
            replies_.pop_back();api_.g_source_unref_(source);--credits_;return;
        }
        job.release();
    }
    void run(){
        for(;;){
            std::unique_ptr<Job> job;
            {
                std::unique_lock lock(mutex_);cv_.wait(lock,[&]{return stopping_||urgent_||!work_.empty();});
                if(stopping_)return;
                if(urgent_)job=std::move(urgent_);
                else {job=std::move(work_.front());work_.pop_front();}
            }
            try{job->result=request(default_runtime(),job->request,1000);}
            catch(const std::exception& e){job->result=failure(e.what());}
            catch(...){job->result=failure("Bridge broker failure");}
            publish(std::move(job));
        }
    }
public:
    Server(Ls2Runtime& api,LSHandle* handle,GMainContext* context,bool verified,bool mutations):
        api_(api),handle_(handle),context_(context),verified_(verified),mutations_(mutations),worker_(&Server::run,this){}
    ~Server(){
        {std::lock_guard lock(mutex_);stopping_=true;}
        cv_.notify_one();worker_.join();
        // The GLib loop has stopped; no delivery callback can race this cleanup.
        for(auto& [source,job]:replies_){api_.g_source_destroy_(source);api_.g_source_unref_(source);delete job;}
    }
    static bool receive(LSHandle*,LSMessage* message,void* data)noexcept{
        auto& self=*static_cast<Server*>(data);
        try{
            const char* raw_sender=self.api_.LSMessageGetSenderServiceName_(message);
            const std::string_view sender=raw_sender?raw_sender:"";
            const bool named=sender==Ls2ClientName;
            // Log identity admission only; never payload values or input content.
            std::cout<<"LS2_CALL named_app="<<named<<" admitted="<<(self.verified_&&named)<<'\n'<<std::flush;
            if(!self.verified_||!named){self.reply(message,failure("LS2 caller denied"));return true;}
            const char* raw=self.api_.LSMessageGetPayload_(message);
            const auto count=raw?strnlen(raw,16385):0;
            if(!count||count>16384){self.reply(message,failure("Bridge request too large or empty"));return true;}
            auto json=Json::parse(std::string(raw,count),16384);
            const auto op=json.get("op").str();
            static const std::set<std::string> read_ops{"status","snapshot","logs","probe","profile-load"};
            static const std::set<std::string> mutation_ops{"configure","start","restart","stop","profile-save"};
            if(!json.object_type()||(!read_ops.contains(op)&&!(self.mutations_&&mutation_ops.contains(op)))){
                self.reply(message,failure("Bridge operation not permitted"));return true;
            }
            std::vector<std::unique_ptr<Job>> cancelled;
            {
                std::lock_guard lock(self.mutex_);
                if(op=="stop"&&self.urgent_){self.reply(message,okay());return true;}
                if(self.stopping_||self.credits_>=(op=="stop"?10U:9U)|| (op!="stop"&&self.work_.size()>=8)){
                    self.reply(message,failure("Bridge queue is full"));return true;
                }
                // Allocate before retaining the LS2 message. Once retained, the
                // fully initialized owner releases it on every exception path.
                auto job=std::make_unique<Job>();job->owner=&self;job->message=message;
                self.api_.LSMessageRef_(message);
                job->request=std::move(json);job->op=op;
                ++self.credits_;
                if(op=="stop"){
                    self.urgent_=std::move(job);
                    for(auto it=self.work_.begin();it!=self.work_.end();){
                        if((*it)->op=="start"||(*it)->op=="restart"){
                            (*it)->result=failure("Start cancelled by Stop");cancelled.push_back(std::move(*it));it=self.work_.erase(it);
                        }else ++it;
                    }
                }else self.work_.push_back(std::move(job));
            }
            for(auto& job:cancelled)self.publish(std::move(job));
            self.cv_.notify_one();
        }catch(const std::exception& e){self.reply(message,failure(e.what()));}
        catch(...){self.reply(message,failure("Invalid bridge request"));}
        return true; // handled even when sending a reply failed; never redispatch
    }
};
struct Quit {Ls2Runtime* api;GMainLoop* loop;static gboolean run(void* p){auto& q=*static_cast<Quit*>(p);q.api->g_main_loop_quit_(q.loop);return G_SOURCE_REMOVE;}};
}
int ls2_service_main(int argc,char** argv){
    bool verified=false,mutations=false;
    for(int i=1;i<argc;++i){
        const std::string arg=argv[i];
        if(arg=="--verified-executable-role")verified=true;
        else if(arg=="--allow-input-control")mutations=true;
        else throw std::runtime_error("Unknown bridge option");
    }
    if(geteuid()!=0)throw std::runtime_error("Bridge requires its explicitly installed private runtime identity");
    if(mutations&&!verified)throw std::runtime_error("Input control requires verified caller binding");
    Ls2Runtime api;Ls2Error error(api);LSHandle* handle=nullptr;
    if(!api.LSRegister_("dev.canopy.synergywebos.control",&handle,&error.value))throw std::runtime_error(error.message());
    auto* context=api.g_main_context_new_();auto* loop=api.g_main_loop_new_(context,FALSE);
    if(!context||!loop)throw std::runtime_error("Cannot create bridge event loop");
    GSource* term=nullptr;GSource* interrupt=nullptr;
    try{
        {
            Server server(api,handle,context,verified,mutations);
            LSMethod methods[]={{"request",&Server::receive,LUNA_METHOD_FLAGS_NONE},{nullptr,nullptr,LUNA_METHOD_FLAGS_NONE}};
            if(!api.LSRegisterCategory_(handle,"/control",methods,nullptr,nullptr,&error.value)||!api.LSCategorySetData_(handle,"/control",&server,&error.value)||!api.LSGmainContextAttach_(handle,context,&error.value))throw std::runtime_error(error.message());
            Quit quit{&api,loop};
            term=api.g_unix_signal_source_new_(SIGTERM);interrupt=api.g_unix_signal_source_new_(SIGINT);
            if(!term||!interrupt)throw std::runtime_error("Cannot create bridge shutdown sources");
            api.g_source_set_callback_(term,&Quit::run,&quit,nullptr);api.g_source_set_callback_(interrupt,&Quit::run,&quit,nullptr);
            if(!api.g_source_attach_(term,context)||!api.g_source_attach_(interrupt,context))throw std::runtime_error("Cannot attach bridge shutdown sources");
            std::cout<<"LS2_BRIDGE_READY verified="<<verified<<" input_control="<<mutations<<'\n'<<std::flush;
            api.g_main_loop_run_(loop);
        }
    }catch(...){
        if(term){api.g_source_destroy_(term);api.g_source_unref_(term);}if(interrupt){api.g_source_destroy_(interrupt);api.g_source_unref_(interrupt);}
        {Ls2Error cleanup(api);api.LSUnregister_(handle,&cleanup.value);}api.g_main_loop_unref_(loop);api.g_main_context_unref_(context);throw;
    }
    api.g_source_destroy_(term);api.g_source_unref_(term);api.g_source_destroy_(interrupt);api.g_source_unref_(interrupt);
    {Ls2Error cleanup(api);api.LSUnregister_(handle,&cleanup.value);}api.g_main_loop_unref_(loop);api.g_main_context_unref_(context);
    return 0;
}
}
int main(int argc,char** argv){try{return synergy::ls2_service_main(argc,argv);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
