// Exercise the actual private Deck/ControlWorker, not a second implementation.
#define main synergy_ui_entry
#include "../src/ui.cpp"
#undef main
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <cstring>

namespace {
    using synergy::Json;
    int checks=0, failures=0;
    void check(bool ok,const std::string& name){
        ++checks;
        if(!ok){++failures;std::cerr<<"FAIL: "<<name<<'\n';}
    }
    template<class F>void wait(F predicate){
        const auto deadline=synergy::monotonic_ms()+3000;
        while(!predicate()){
            if(synergy::monotonic_ms()>=deadline)throw std::runtime_error("UI fixture deadline");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    void receive(int fd,void* data,size_t size){
        auto* p=static_cast<char*>(data);
        while(size){
            const auto n=recv(fd,p,size,0);
            if(n<=0)throw std::runtime_error("Fixture receive failed");
            p+=n;size-=static_cast<size_t>(n);
        }
    }
    struct Broker {
        std::filesystem::path path;
        int fd=-1;
        std::atomic<bool> stop=false,blocked=false,unblock=false;
        std::atomic<int> probes=0,statuses=0;
        std::mutex mutex;
        std::vector<std::string> calls;
        std::string saved="initial";
        bool fail_configure=false,fail_start=false;
        std::exception_ptr error;
        std::thread thread;
        explicit Broker(const std::filesystem::path& root,bool fail_config=false,bool fail_run=false):
            path(root/"control.sock"),fail_configure(fail_config),fail_start(fail_run){
            std::filesystem::create_directories(root);
            fd=socket(AF_UNIX,SOCK_STREAM,0);
            sockaddr_un address{};address.sun_family=AF_UNIX;
            if(path.string().size()>=sizeof address.sun_path)throw std::runtime_error("Fixture path too long");
            std::strcpy(address.sun_path,path.c_str());
            if(fd<0||bind(fd,reinterpret_cast<sockaddr*>(&address),sizeof address)||listen(fd,4))throw std::runtime_error("Fixture bind failed");
            thread=std::thread([this]{
                try{run();}catch(...){error=std::current_exception();}
            });
        }
        void run(){
            bool first=true;
            while(!stop){
                pollfd ready{fd,POLLIN,0};
                if(poll(&ready,1,20)<=0)continue;
                int peer=accept(fd,nullptr,nullptr);
                if(peer<0)continue;
                timeval timeout{2,0};setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
                try{
                    unsigned char header[4];receive(peer,header,4);
                    const auto n=(static_cast<unsigned>(header[0])<<24)|(static_cast<unsigned>(header[1])<<16)|(static_cast<unsigned>(header[2])<<8)|header[3];
                    if(n>65536)throw std::runtime_error("Unbounded fixture packet");
                    std::string text(n,'\0');receive(peer,text.data(),n);
                    auto job=Json::parse(text);auto op=job.get("op").str();
                    auto result=synergy::okay();
                    if(op=="status"){
                        ++statuses;
                        synergy::Config c;c.server="127.0.0.1";c.name=std::string(64,'W');
                        result.set("saved_config",c.json());result.set("schema",Json::number(synergy::ControlSchema));
                    }
                    if(op=="configure"){
                        if(first){
                            first=false;blocked=true;
                            while(!unblock&&!stop)std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                        const auto name=job.get("config").get("name").str();
                        {std::lock_guard lock(mutex);calls.push_back("configure:"+name);}
                        if(fail_configure)result=synergy::failure("Synthetic configure failure");
                        else saved=name;
                    }
                    if(op=="start"){
                        {std::lock_guard lock(mutex);calls.push_back("start:"+saved);}
                        if(fail_start)result=synergy::failure("Synthetic start failure");
                    }
                    if(op=="stop"){
                        std::lock_guard lock(mutex);calls.push_back("stop");
                    }
                    text=result.dump();const auto length=static_cast<unsigned>(text.size());
                    unsigned char out[4]={static_cast<unsigned char>(length>>24),static_cast<unsigned char>(length>>16),static_cast<unsigned char>(length>>8),static_cast<unsigned char>(length)};
                    if(send(peer,out,4,MSG_NOSIGNAL)!=4||send(peer,text.data(),text.size(),MSG_NOSIGNAL)!=static_cast<ssize_t>(text.size()))throw std::runtime_error("Fixture send failed");
                    if(op=="probe")++probes;
                    close(peer);
                }catch(...){close(peer);throw;}
            }
        }
        std::vector<std::string> snapshot(){std::lock_guard lock(mutex);return calls;}
        ~Broker(){stop=true;unblock=true;thread.join();close(fd);unlink(path.c_str());}
    };
    void load(synergy::Deck& deck){
        wait([&]{deck.update();return deck.report().get("draft").get("server").str()=="127.0.0.1";});
    }
    void rename(synergy::Deck&,const std::string&);
    void flush(synergy::Deck&,Broker&);
    void stopping_and_waking(spice::Sdl& sdl,const std::filesystem::path& root){
        {
            Broker broker(root/"reserved-stop");synergy::ControlWorker worker(root/"reserved-stop");
            synergy::Deck deck(worker,sdl,"link");load(deck);rename(deck,"held");
            deck.activate("start");wait([&]{return broker.blocked.load();});
            for(int i=0;i<8;++i)deck.activate("save");
            for(int i=0;i<3;++i)deck.activate("stop");
            broker.unblock=true;
            wait([&]{return broker.snapshot().size()>=10;});flush(deck,broker);
            auto calls=broker.snapshot();
            check(calls.size()==10&&calls[0]=="configure:held"&&calls[1]=="stop","Stop has reserved priority despite eight queued saves, repeated Stop coalesces");
            check(std::none_of(calls.begin(),calls.end(),[](const auto& call){return call.starts_with("start:");}),"in-flight configure-start cannot start after Stop intent");
            rename(deck,"new-intent");deck.activate("start");flush(deck,broker);
            calls=broker.snapshot();
            check(calls.size()==12&&calls[10]=="configure:new-intent"&&calls[11]=="start:new-intent","explicit Connect after Stop remains usable");
        }
        {
            Broker broker(root/"cancel-queued");synergy::ControlWorker worker(root/"cancel-queued");
            synergy::Deck deck(worker,sdl,"link");load(deck);rename(deck,"held");
            deck.activate("save");wait([&]{return broker.blocked.load();});
            for(int i=0;i<8;++i)deck.activate("start");
            deck.activate("stop");broker.unblock=true;
            wait([&]{return broker.snapshot().size()>=2;});flush(deck,broker);
            check(broker.snapshot()==std::vector<std::string>{"configure:held","stop"},"Stop invalidates all queued configure-start work");
        }
        {
            Broker broker(root/"wake-result");
            synergy::ControlWorker worker(root/"wake-result",[&]{SDL_Event e{};e.type=synergy::ControlReadyEvent;sdl.PushEvent(&e);});
            auto job=Json::object();job.set("op",Json::string("configure"));
            synergy::Config config;job.set("config",config.json());
            check(worker.post("configure",job),"wakeup fixture submits configure");
            wait([&]{return broker.blocked.load();});
            SDL_Event e{};while(sdl.PollEvent(&e)){}
            broker.unblock=true;
            bool woke=false;
            const auto until=synergy::monotonic_ms()+2000;
            while(!woke&&synergy::monotonic_ms()<until)if(sdl.WaitEventTimeout(&e,1000))woke=e.type==synergy::ControlReadyEvent;
            check(woke&&!worker.take().empty(),"worker result wakes an SDL event wait with published result");
        }
        {
            Broker broker(root/"hidden");synergy::ControlWorker worker(root/"hidden");
            synergy::Deck deck(worker,sdl,"link");load(deck);
            deck.visible(false);
            for(int i=0;i<3;++i){deck.update(false);std::this_thread::sleep_for(std::chrono::milliseconds(750));}
            const auto before=broker.statuses.load();deck.update(false);
            check(broker.statuses==before&&deck.wait_ms(false)==-1,"hidden Deck neither polls status nor uses a periodic wait timeout");
            deck.visible(true);deck.update();
            wait([&]{return broker.statuses>before;});
            check(broker.statuses>before,"returning to visible requests fresh status promptly");
        }
        {
            Broker broker(root/"result-overflow");synergy::ControlWorker worker(root/"result-overflow");
            auto job=Json::object();job.set("op",Json::string("status"));
            check(worker.post("status",job),"result overflow fixture accepts status");
            wait([&]{return broker.statuses==1;});
            bool accepted=true;
            for(int i=0;i<20;++i){
                job.set("op",Json::string("probe"));
                accepted&=worker.post("probe",job);
                wait([&]{return broker.probes>=i+1;});
            }
            check(accepted,"result overflow fixture submits all probes");
            const auto results=worker.take();
            check(results.size()<=16&&std::any_of(results.begin(),results.end(),[](const auto& result){return result.first=="status";}),"bounded result overflow retains status so refresh cannot remain pending forever");
        }
    }
    void rename(synergy::Deck& deck,const std::string& name){
        deck.activate("name");deck.activate("edit-clear");deck.text(name);deck.activate("edit-apply");
    }
    // Two FIFO markers also drain any UI-originated follow-up from the first update.
    void flush(synergy::Deck& deck,Broker& broker){
        for(int i=0;i<2;++i){
            const int next=broker.probes+1;
            deck.activate("probe");
            wait([&]{return broker.probes>=next;});
            deck.update();
        }
    }
    void transactions(spice::Sdl& sdl,const std::filesystem::path& root){
        {
            Broker broker(root/"ordering");synergy::ControlWorker worker(root/"ordering");
            synergy::Deck deck(worker,sdl,"link");load(deck);rename(deck,"first");
            deck.activate("start");wait([&]{return broker.blocked.load();});
            rename(deck,"second");deck.activate("save");deck.activate("start");
            broker.unblock=true;flush(deck,broker);
            check(broker.snapshot()==std::vector<std::string>{"configure:first","start:first","configure:second","configure:second","start:second"},"each Save + Connect starts its own configuration before later saves, even without UI polling");
        }
        {
            Broker broker(root/"save-first");synergy::ControlWorker worker(root/"save-first");
            synergy::Deck deck(worker,sdl,"link");load(deck);rename(deck,"save-only");
            deck.activate("save");wait([&]{return broker.blocked.load();});
            rename(deck,"connect");deck.activate("start");broker.unblock=true;flush(deck,broker);
            check(broker.snapshot()==std::vector<std::string>{"configure:save-only","configure:connect","start:connect"},"Save response cannot steal a later Connect intent");
        }
        {
            Broker broker(root/"config-failure",true);synergy::ControlWorker worker(root/"config-failure");
            synergy::Deck deck(worker,sdl,"link");load(deck);rename(deck,"bad");
            deck.activate("start");wait([&]{return broker.blocked.load();});broker.unblock=true;flush(deck,broker);
            check(broker.snapshot()==std::vector<std::string>{"configure:bad"},"failed configure never starts");
        }
        {
            Broker broker(root/"start-failure",false,true);synergy::ControlWorker worker(root/"start-failure");
            synergy::Deck deck(worker,sdl,"link");load(deck);rename(deck,"connect");
            deck.activate("start");wait([&]{return broker.blocked.load();});
            rename(deck,"after-failure");deck.activate("save");broker.unblock=true;flush(deck,broker);
            check(broker.snapshot()==std::vector<std::string>{"configure:connect","start:connect","configure:after-failure"},"failed start is not retried and does not prevent a later save");
        }
        {
            Broker broker(root/"queue-full");synergy::ControlWorker worker(root/"queue-full");
            synergy::Deck deck(worker,sdl,"link");load(deck);rename(deck,"accepted");
            deck.activate("start");wait([&]{return broker.blocked.load();});
            for(int i=0;i<8;++i)deck.activate("save");
            rename(deck,"rejected");deck.activate("start");broker.unblock=true;
            wait([&]{return broker.snapshot().size()>=9;});flush(deck,broker);
            const auto calls=broker.snapshot();
            check(calls.size()==10&&calls[0]=="configure:accepted"&&calls[1]=="start:accepted"&&std::count(calls.begin(),calls.end(),"configure:accepted")==9,"queue rejection cannot cancel accepted Connect or start rejected configuration");
        }
    }
    void* (*real_lookup)(const char*)=nullptr;
    decltype(spice::Gl::TexImage2D) real_upload=nullptr;
    decltype(spice::Gl::GetIntegerv) real_integer=nullptr;
    size_t uploads=0,upload_bytes=0,glyph_loads=0;
    size_t alpha_payload=0,rgba_uploads=0;
    bool expand_alpha=false;
    int simulated_texture_limit=0;
    FT_Error (*real_load)(FT_Face,FT_ULong,int32_t)=nullptr;
    FT_Error tracked_load(FT_Face face,FT_ULong c,int32_t flags){++glyph_loads;return real_load(face,c,flags);}
    void tracked_upload(spice::GLenum a,spice::GLint b,spice::GLint c,spice::GLsizei w,spice::GLsizei h,spice::GLint d,spice::GLenum e,spice::GLenum f,const void* p){
        const size_t pixels=static_cast<size_t>(w)*h;
        ++uploads;upload_bytes+=pixels*(e==0x1906?1:4);
        if(e==0x1906){
            alpha_payload+=pixels;
            if(expand_alpha){
                std::vector<unsigned char> rgba(pixels*4,255);
                const auto* alpha=static_cast<const unsigned char*>(p);
                for(size_t i=0;i<pixels;++i)rgba[i*4+3]=alpha[i];
                real_upload(a,b,0x1908,w,h,d,0x1908,f,rgba.data());
                return;
            }
        }
        else ++rgba_uploads;
        real_upload(a,b,c,w,h,d,e,f,p);
    }
    void tracked_integer(spice::GLenum parameter,spice::GLint* value){
        real_integer(parameter,value);
        if(parameter==0xd33&&simulated_texture_limit)*value=std::min(*value,simulated_texture_limit);
    }
    void* lookup(const char* name){
        auto* fn=real_lookup(name);
        if(std::strcmp(name,"glTexImage2D")==0){real_upload=reinterpret_cast<decltype(real_upload)>(fn);return reinterpret_cast<void*>(&tracked_upload);}
        if(std::strcmp(name,"glGetIntegerv")==0){real_integer=reinterpret_cast<decltype(real_integer)>(fn);return reinterpret_cast<void*>(&tracked_integer);}
        return fn;
    }
    void alpha_text(spice::Sdl& sdl){
        using ReadPixels=void(*)(int,int,int,int,spice::GLenum,spice::GLenum,void*);
        const auto read=reinterpret_cast<ReadPixels>(real_lookup("glReadPixels"));
        for(int pixels:{1280,1920,3840}){
            const int height=pixels*9/16;
            const auto viewport=spice::Viewport::make(pixels,height,pixels,height);
            std::vector<unsigned char> actual(static_cast<size_t>(pixels)*height*4),reference(actual.size());
            size_t compact=0;
            for(bool expand:{false,true}){
                expand_alpha=expand;alpha_payload=rgba_uploads=0;
                spice::Renderer renderer(sdl,spice::find_font(""),{});
                renderer.begin(viewport);
                renderer.gradient({0,0,1920,1080},spice::Color::hex("#142b2c"),spice::Color::hex("#070b10"));
                renderer.text("AV / j / caf\xc3\xa9 / \xe2\x82\xac / overlaps",50,60,37,spice::Color::hex("#ffb35c"));
                renderer.text(renderer.ellipsis(std::string(64,'W'),25,900),50,150,25,spice::Color::hex("#56d9c3").opacity(.6f));
                renderer.end();
                read(0,0,pixels,height,0x1908,0x1401,(expand?reference:actual).data());
                check(alpha_payload>0&&rgba_uploads==1,"text uses alpha uploads while white texture stays RGBA at "+std::to_string(pixels));
                check(renderer.texture_bytes()==alpha_payload,"text cache accounts one byte per texel at "+std::to_string(pixels));
                if(!expand)compact=alpha_payload;
                else check(alpha_payload==compact,"reference and compact text have identical dimensions at "+std::to_string(pixels));
                renderer.begin(viewport);uploads=0;
                renderer.text("AV / j / caf\xc3\xa9 / \xe2\x82\xac / overlaps",50,60,37);
                renderer.end();check(uploads==0,"warm alpha text uploads no new textures at "+std::to_string(pixels));
            }
            check(actual==reference,"alpha and RGBA-expanded coverage render identical pixels at "+std::to_string(pixels));
        }
        expand_alpha=false;
    }
    // The previous, candidate-by-candidate algorithm: the reference for results.
    std::string reference_ellipsis(spice::Renderer& r,const std::string& value,float size,float max){
        if(r.width(value,size)<=max)return value;
        auto s=value;
        while(!s.empty()&&r.width(s+"...",size)>max)spice::pop_utf8(s);
        return s+"...";
    }
    void text_layout(spice::Sdl& sdl){
        const auto uhd=spice::Viewport::make(3840,2160,3840,2160);
        const std::string name(64,'W');
        float full=0;
        for(int limit:{0,2048,512}){
            simulated_texture_limit=limit;
            spice::Renderer r(sdl,spice::find_font(""),{});
            simulated_texture_limit=0;
            r.begin(uhd);
            const auto measured=r.width(name,25);
            if(!limit)full=measured;
            const auto label=std::to_string(limit);
            check(std::abs(measured-full)<.001f&&measured>1034,"measured width is not clamped by texture limit "+label);
            const auto shortened=r.ellipsis(name,25,1034);
            check(shortened.ends_with("...")&&shortened.size()<name.size()&&r.width(shortened,25)<=1034,"ellipsis sees true overflow at texture limit "+label);
            if(limit)check(r.width(shortened,25)*2<=limit,"ellipsis result fits one texture at limit "+label);
            r.text(name,0,0,25);
            check(r.text(name,0,0,25)*2<=static_cast<float>(limit?limit:1<<30),"overlong drawn text is clipped, not stretched, at limit "+label);
            r.end();
        }
        spice::Renderer r(sdl,spice::find_font(""),{});
        for(int pixels:{1280,1920,3840}){
            const auto viewport=spice::Viewport::make(pixels,pixels*9/16,pixels,pixels*9/16);
            r.begin(viewport);
            glyph_loads=0;
            const auto shortened=r.ellipsis(name+std::to_string(pixels),25,1034);
            check(glyph_loads<=8,"cold ellipsis loads each distinct glyph once at "+std::to_string(pixels)+" (loads="+std::to_string(glyph_loads)+")");
            glyph_loads=0;
            r.ellipsis(name+std::to_string(pixels),25,1034);
            check(glyph_loads==0,"warm ellipsis loads no glyphs at "+std::to_string(pixels));
            bool same=true;
            for(const std::string& value:{std::string(64,'W'),std::string("AVAVAVAVAVAVTyToTjWAVAyLTLTLT.,;"),std::string("caf\xc3\xa9 \xe2\x82\xac uber Gr\xc3\xb6\xc3\x9f""e ijklm"),std::string(80,'i'),std::string("....."),std::string("")})
                for(float field:{0.f,10.f,40.f,120.f,333.f,940.f,1600.f})
                    for(float size:{18.f,25.f,34.f}){
                        const auto fast=r.ellipsis(value,size,field),slow=reference_ellipsis(r,value,size,field);
                        if(fast!=slow){same=false;std::cerr<<"ellipsis mismatch size="<<size<<" field="<<field<<" '"<<fast<<"' vs '"<<slow<<"'\n";}
                    }
            check(same,"single-layout ellipsis matches candidate re-measurement at "+std::to_string(pixels));
            r.end();
        }
    }
    void redraw_gating(spice::Sdl& sdl,const std::filesystem::path& root){
        Broker broker(root/"idle");synergy::ControlWorker worker(root/"idle");
        synergy::Deck deck(worker,sdl,"link");load(deck);
        spice::Renderer r(sdl,spice::find_font(""),{});
        r.begin(spice::Viewport::make(1920,1080,1920,1080));deck.draw(r);r.end();
        // Span at least one periodic refresh; the unchanged status must not dirty.
        bool changed=false;
        const auto until=synergy::monotonic_ms()+2300;
        while(synergy::monotonic_ms()<until){changed|=deck.update();std::this_thread::sleep_for(std::chrono::milliseconds(20));}
        check(!changed,"identical periodic status refresh does not request a redraw");
        const int next=broker.probes+1;
        deck.activate("probe");wait([&]{return broker.probes>=next;});
        bool probed=false;
        wait([&]{probed|=deck.update();return probed;});
        check(probed,"worker result requests a redraw");
        // Focus starts on tab-link; tab-input occupies x 308..536, y 184..246.
        check(deck.pointer({400,215},false),"pointer moving onto a new control requests a redraw");
        check(!deck.pointer({402,216},false),"pointer moving within the focused control does not");
        check(!deck.pointer({5,5},false),"pointer over empty space does not");
    }
    void rendering(spice::Sdl& sdl,const std::filesystem::path& root){
        Broker broker(root/"render");synergy::ControlWorker worker(root/"render");
        synergy::Deck deck(worker,sdl,"link");load(deck);deck.activate("name");
        for(int pixels:{1280,1920,3840}){
            spice::Renderer r(sdl,spice::find_font(""),{});
            const auto viewport=spice::Viewport::make(pixels,pixels*9/16,pixels,pixels*9/16);
            r.begin(viewport);uploads=upload_bytes=0;
            for(const std::string value:{"AV", "j", "space ", "", "caf\xc3\xa9", "\xe2\x82\xac"}){
                const auto measured=r.width(value,27);
                const auto drawn=r.text(value,0,0,27);
                check(std::abs(measured-drawn)<.001f,"measurement matches drawn texture at "+std::to_string(pixels));
            }
            r.end();r.begin(viewport);uploads=upload_bytes=0;
            const auto before=r.text_cache_size();
            auto shortened=r.ellipsis(std::string(64,'W'),34,940);
            check(uploads==0&&r.text_cache_size()==before,"width and ellipsis allocate no GL textures at "+std::to_string(pixels));
            check(r.width(shortened,34)<=940&&shortened.ends_with("..."),"ellipsis fits available width at "+std::to_string(pixels));
            r.end();
            for(int frame=0;frame<4;++frame){
                uploads=upload_bytes=0;
                const auto start=std::chrono::steady_clock::now();
                r.begin(viewport);deck.draw(r);r.end();
                const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                std::cout<<"edit_64W width="<<pixels<<" frame="<<frame<<" uploads="<<uploads<<" uploaded_bytes="<<upload_bytes<<" draw_ms="<<ms<<'\n';
                if(frame>0)check(uploads==0,"unchanged edit frame reuses textures at "+std::to_string(pixels));
                check(r.texture_bytes()<=24*1024*1024&&r.text_cache_size()<=384,"drawn text cache stays bounded");
            }
            for(int i=0;i<500;++i)r.width("measurement-"+std::to_string(i),25);
            r.end();
        }
    }
}
extern "C" void* __real_dlsym(void*,const char*);
// Linked with --wrap=dlsym: counts the renderer's actual FreeType glyph loads.
extern "C" void* __wrap_dlsym(void* handle,const char* name){
    auto* p=__real_dlsym(handle,name);
    if(std::strcmp(name,"FT_Load_Char")==0){real_load=reinterpret_cast<decltype(real_load)>(p);return reinterpret_cast<void*>(&tracked_load);}
    return p;
}
int main(int argc,char** argv){
    try{
        if(argc!=2)throw std::runtime_error("Expected private POSIX fixture root");
        spice::Sdl sdl;synergy::Window window(sdl);
        bool explicit_runtime_rejected=false;
        try {spice::Sdl missing("/nonexistent-synergy-packaged-sdl.so");}
        catch(const std::exception&){explicit_runtime_rejected=true;}
        check(explicit_runtime_rejected,"an explicit SDL runtime path never falls back to the system library");
        sdl.SetMainReady();if(sdl.Init(SDL_INIT_VIDEO))throw std::runtime_error(sdl.GetError());
        sdl.GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
        sdl.GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);sdl.GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
        window.window=sdl.CreateWindow("Native UI regression fixture",0,0,3840,2160,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
        if(!window.window)throw std::runtime_error(sdl.GetError());
        window.gl=sdl.GL_CreateContext(window.window);if(!window.gl)throw std::runtime_error(sdl.GetError());
        real_lookup=sdl.GL_GetProcAddress;sdl.GL_GetProcAddress=lookup;
        transactions(sdl,argv[1]);stopping_and_waking(sdl,argv[1]);rendering(sdl,argv[1]);alpha_text(sdl);text_layout(sdl);redraw_gating(sdl,argv[1]);
        std::cout<<(failures?"FAIL: ":"PASS: ")<<checks<<" UI regression assertions; failures="<<failures<<'\n';
        return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
