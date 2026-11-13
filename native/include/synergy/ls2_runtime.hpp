#pragma once
#include <luna-service2/lunaservice.h>
#include <glib-unix.h>
#include <dlfcn.h>
#include <stdexcept>
namespace synergy {
// Public-header signatures; platform DSOs are resolved on the TV, never bundled.
class Ls2Runtime {
    void* luna_=nullptr;
    void* glib_=nullptr;
    template<class T>static T get(void* lib,const char* name){
        auto value=reinterpret_cast<T>(dlsym(lib,name));
        if(!value)throw std::runtime_error(std::string("Missing LS2 runtime symbol: ")+name);
        return value;
    }
public:
#define LS2_FN(name) decltype(&name) name##_=nullptr
    LS2_FN(LSErrorInit);LS2_FN(LSErrorFree);LS2_FN(LSRegister);LS2_FN(LSUnregister);
    LS2_FN(LSRegisterCategory);LS2_FN(LSCategorySetData);LS2_FN(LSGmainContextAttach);
    LS2_FN(LSMessageGetSenderServiceName);LS2_FN(LSMessageGetPayload);LS2_FN(LSMessageReply);
    LS2_FN(LSMessageRef);LS2_FN(LSMessageUnref);LS2_FN(LSCallOneReply);LS2_FN(LSCallCancel);
    LS2_FN(g_main_context_new);LS2_FN(g_main_context_unref);LS2_FN(g_main_context_iteration);
    LS2_FN(g_main_loop_new);LS2_FN(g_main_loop_run);LS2_FN(g_main_loop_quit);LS2_FN(g_main_loop_unref);
    LS2_FN(g_timeout_source_new);LS2_FN(g_idle_source_new);LS2_FN(g_source_set_callback);
    LS2_FN(g_source_attach);LS2_FN(g_source_destroy);LS2_FN(g_source_unref);
    LS2_FN(g_unix_signal_source_new);
#undef LS2_FN
    Ls2Runtime(){
        luna_=dlopen("libluna-service2.so.3",RTLD_NOW|RTLD_LOCAL);
        glib_=dlopen("libglib-2.0.so.0",RTLD_NOW|RTLD_LOCAL);
        try{
            if(!luna_||!glib_)throw std::runtime_error("LS2/GLib platform runtime unavailable");
#define LOAD(name) name##_=get<decltype(name##_)>(luna_,#name)
            LOAD(LSErrorInit);LOAD(LSErrorFree);LOAD(LSRegister);LOAD(LSUnregister);
            LOAD(LSRegisterCategory);LOAD(LSCategorySetData);LOAD(LSGmainContextAttach);
            LOAD(LSMessageGetSenderServiceName);LOAD(LSMessageGetPayload);LOAD(LSMessageReply);
            LOAD(LSMessageRef);LOAD(LSMessageUnref);LOAD(LSCallOneReply);LOAD(LSCallCancel);
#undef LOAD
#define LOAD(name) name##_=get<decltype(name##_)>(glib_,#name)
            LOAD(g_main_context_new);LOAD(g_main_context_unref);LOAD(g_main_context_iteration);
            LOAD(g_main_loop_new);LOAD(g_main_loop_run);LOAD(g_main_loop_quit);LOAD(g_main_loop_unref);
            LOAD(g_timeout_source_new);LOAD(g_idle_source_new);LOAD(g_source_set_callback);
            LOAD(g_source_attach);LOAD(g_source_destroy);LOAD(g_source_unref);
            LOAD(g_unix_signal_source_new);
#undef LOAD
        }catch(...){if(luna_)dlclose(luna_);if(glib_)dlclose(glib_);throw;}
    }
    ~Ls2Runtime(){dlclose(luna_);dlclose(glib_);}
    Ls2Runtime(const Ls2Runtime&)=delete;
    Ls2Runtime& operator=(const Ls2Runtime&)=delete;
};
class Ls2Error {
    Ls2Runtime& api_;
public:
    LSError value{};
    explicit Ls2Error(Ls2Runtime& api):api_(api){api_.LSErrorInit_(&value);}
    ~Ls2Error(){api_.LSErrorFree_(&value);}
    std::string message()const{return value.message?value.message:"LS2 operation failed";}
};
}
