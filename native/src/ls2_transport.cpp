#include "synergy/ls2_transport.hpp"
#include "synergy/ls2_runtime.hpp"
#include <cstring>
#include <memory>
namespace synergy {
namespace {
struct Call {
    Ls2Runtime& api;
    bool done=false,timed_out=false;
    Json result;
    static bool reply(LSHandle*,LSMessage* message,void* data)noexcept{
        auto& call=*static_cast<Call*>(data);
        try{
            const char* raw=call.api.LSMessageGetPayload_(message);
            const size_t n=raw?strnlen(raw,131073):0;
            if(!n||n>131072)throw std::runtime_error("LS2 reply exceeded bounds");
            call.result=Json::parse(std::string(raw,n),131072);
            if(!call.result.get("errorText").empty())call.result=failure(call.result.get("errorText").str());
        }catch(const std::exception& e){call.result=failure(e.what());}
        catch(...){call.result=failure("Invalid LS2 reply");}
        call.done=true;return true;
    }
    static gboolean timeout(void* data)noexcept{
        auto& call=*static_cast<Call*>(data);call.timed_out=true;call.done=true;return G_SOURCE_REMOVE;
    }
};
class ClientConnection {
public:
    Ls2Runtime api;
    GMainContext* context=nullptr;
    LSHandle* handle=nullptr;
    ClientConnection(){
        context=api.g_main_context_new_();
        if(!context)throw std::runtime_error("Cannot create LS2 context");
        try{
            Ls2Error error(api);
            if(!api.LSRegister_(Ls2ClientName,&handle,&error.value))throw std::runtime_error("LS2 app registration: "+error.message());
            if(!api.LSGmainContextAttach_(handle,context,&error.value))throw std::runtime_error(error.message());
        }catch(...){close();throw;}
    }
    void close(){
        if(handle){Ls2Error error(api);api.LSUnregister_(handle,&error.value);handle=nullptr;}
        if(context){api.g_main_context_unref_(context);context=nullptr;}
    }
    ~ClientConnection(){close();}
};
}
Json ls2_request(const Json& request_json,int timeout_ms){
    if(timeout_ms<1||timeout_ms>5000)throw std::runtime_error("Invalid LS2 deadline");
    const auto payload=request_json.dump();
    if(payload.size()>16384)throw std::runtime_error("LS2 request too large");
    // One connection per control worker avoids duplicate-name races between
    // configure and start, and is torn down when that worker exits.
    thread_local std::unique_ptr<ClientConnection> connection;
    if(!connection)connection=std::make_unique<ClientConnection>();
    auto& api=connection->api;
    auto* context=connection->context;
    auto* handle=connection->handle;
    GSource* timer=nullptr;
    LSMessageToken token=LSMESSAGE_TOKEN_INVALID;
    const auto cleanup=[&]{
        if(timer){api.g_source_destroy_(timer);api.g_source_unref_(timer);}
        if(token!=LSMESSAGE_TOKEN_INVALID){Ls2Error error(api);api.LSCallCancel_(handle,token,&error.value);}
    };
    try{
        Ls2Error error(api);
        Call call{api,false,false,{}};
        timer=api.g_timeout_source_new_(static_cast<guint>(timeout_ms));
        if(!timer)throw std::runtime_error("Cannot allocate LS2 deadline source");
        api.g_source_set_callback_(timer,&Call::timeout,&call,nullptr);
        if(!api.g_source_attach_(timer,context))throw std::runtime_error("Cannot attach LS2 deadline source");
        if(!api.LSCallOneReply_(handle,"luna://dev.canopy.synergywebos.control/control/request",payload.c_str(),&Call::reply,&call,&token,&error.value))throw std::runtime_error(error.message());
        while(!call.done)api.g_main_context_iteration_(context,TRUE);
        if(call.timed_out)call.result=failure("LS2 control deadline exceeded");
        auto result=std::move(call.result);
        cleanup();return result;
    }catch(...){cleanup();throw;}
}
}
