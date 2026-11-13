#include "spice/render.hpp"
#include "synergy/ipc.hpp"
#include "synergy/protocol.hpp"
#ifdef SYNERGY_USE_LS2
#include "synergy/ls2_transport.hpp"
#endif
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
namespace synergy {
    namespace {
        using spice::Color;
        using spice::Rect;
        const auto Ink=Color::hex("#070b10"), Panel=Color::hex("#0d141b"), Raised=Color::hex("#111a23");
        const auto Text=Color::hex("#f2f7fa"), Muted=Color::hex("#8fa3b3"), Amber=Color::hex("#ffb35c"), Teal=Color::hex("#56d9c3");
        const auto Line=Color::hex("#1d2a35"), Danger=Color::hex("#ff7a7a");
        constexpr Uint32 ControlReadyEvent=0x8000; // SDL_USEREVENT; private to this app.
        // The renderer never waits on a socket. Requests and results are bounded, owned queues.
        class ControlWorker {
            std::filesystem::path root_;
            std::mutex mutex_;
            std::condition_variable cv_;
            struct Job { std::string tag; Json request; uint64_t epoch; };
            std::deque<Job> work_;
            std::deque<std::pair<std::string,Json>> results_;
            bool stop_=false,stop_pending_=false;
            uint64_t epoch_=0;
            std::function<void()> wake_;
            std::function<Json(const Json&,int)> transport_;
            std::thread thread_;
            Json send(const Json& job,int timeout){
                return transport_?transport_(job,timeout):request(root_,job,timeout);
            }
            void result_locked(const std::string& tag,Json result) {
                if(tag=="status"||tag=="stop"){
                    const auto prior=std::find_if(results_.begin(),results_.end(),[&](const auto& r){return r.first==tag;});
                    if(prior!=results_.end())results_.erase(prior);
                }
                if(results_.size()==16){
                    const auto oldest=std::find_if(results_.begin(),results_.end(),[](const auto& r){return r.first!="status"&&r.first!="stop";});
                    results_.erase(oldest); // at most two coalesced protected results
                }
                results_.emplace_back(tag,std::move(result));
            }
            void run() {
                for (;;) {
                    Job job;
                    {
                        std::unique_lock lock(mutex_);
                        cv_.wait(lock,[&]{
                            return stop_||stop_pending_||!work_.empty();}
                        );
                        if(stop_)return;
                        if(stop_pending_){
                            stop_pending_=false;
                            auto request=Json::object();
                            request.set("op",Json::string("stop"));
                            job={"stop",std::move(request),epoch_};
                        }
                        else {
                            job=std::move(work_.front());
                            work_.pop_front();
                        }
                    }
                    Json result;
                    try {
                        result=send(job.request,1200);
                        // One FIFO work item owns both operations. Later saves cannot
                        // replace the configuration before this start is submitted.
                        if(job.tag=="configure-start"&&result.get("ok").flag()){
                            bool cancelled;
                            {
                                std::lock_guard lock(mutex_);
                                cancelled=stop_||job.epoch!=epoch_;
                            }
                            if(cancelled)result=failure("Configuration saved; connection start cancelled by Stop.");
                            else {
                                auto start=Json::object();
                                start.set("op",Json::string("start"));
                                try{
                                    result=send(start,1200);
                                }
                                catch(const std::exception& e){
                                    result=failure(e.what());
                                }
                                if(!result.get("ok").flag())result=failure("Configuration saved, but connection start failed: "+result.get("error").str("Daemon request failed"));
                            }
                        }
                    }
                    catch(const std::exception& e) {
                        result=failure(e.what());
                    }
                    {
                        std::lock_guard lock(mutex_);
                        result_locked(job.tag,std::move(result));
                    }
                    if(wake_)wake_();
                }
            }
            public:
            explicit ControlWorker(std::filesystem::path root,std::function<void()> wake={},std::function<Json(const Json&,int)> transport={}):root_(std::move(root)),wake_(std::move(wake)),transport_(std::move(transport)),thread_(&ControlWorker::run,this){
            }
            ~ControlWorker() {
                {
                    std::lock_guard lock(mutex_);
                    stop_=true;
                    work_.clear();
                }
                cv_.notify_one();
                thread_.join();
            }
            bool post(const std::string& tag,Json job) {
                {
                    std::lock_guard lock(mutex_);
                    if(stop_)return false;
                    if(tag=="stop"){
                        ++epoch_;
                        stop_pending_=true;
                        for(auto it=work_.begin();it!=work_.end();){
                            const auto op=it->request.get("op").str();
                            if(it->tag=="configure-start"||op=="start"||op=="restart"){
                                result_locked(it->tag,failure("Connection start cancelled by Stop."));
                                it=work_.erase(it);
                            }
                            else ++it;
                        }
                    }
                    else {
                        if(work_.size()>=8)return false;
                        work_.push_back({tag,std::move(job),epoch_});
                    }
                }
                cv_.notify_one();
                if(tag=="stop"&&wake_)wake_();
                return true;
            }
            std::deque<std::pair<std::string,Json>> take() {
                std::lock_guard lock(mutex_);
                std::deque<std::pair<std::string,Json>> out;
                out.swap(results_);
                return out;
            }
        }
        ;
        struct Button {
            std::string id,label;
            Rect rect;
            bool selected=false;
        }
        ;
        class Deck {
            ControlWorker& worker_;
            spice::Sdl& sdl_;
            std::vector<Button> buttons_;
            std::string focus_="tab-link", editing_, edit_text_, old_focus_;
            bool uppercase_=false, loaded_=false, status_pending_=false, schema_warned_=false;
            int64_t refresh_=0;
            Json status_,probe_;
            Config config_;
            std::string page_="link", notice_="Start the separately provisioned daemon to connect.", last_key_;
            bool error_=false;
            std::string corner_="bottom-right";
            int corner_pixels_=2,delay_=250;
            void say(std::string message,bool error=false){
                notice_=std::move(message);
                error_=error;
            }
            bool post(const std::string& tag,Json job) {
                if(worker_.post(tag,std::move(job)))return true;
                say("Control queue is full; wait for the current request to finish.",true);
                return false;
            }
            void command(const std::string& op) {
                auto j=Json::object();
                j.set("op",Json::string(op));
                post(op,std::move(j));
            }
            void edit(const std::string& id,const std::string& text) {
                old_focus_=focus_;
                editing_=id;
                edit_text_=text;
                focus_="char-0";
                sdl_.StartTextInput();
            }
            void close_edit(){
                editing_.clear();
                focus_=old_focus_;
                sdl_.StopTextInput();
            }
            void apply_edit() {
                try {
                    if(editing_=="server")config_.server=edit_text_;
                    else if(editing_=="name")config_.name=edit_text_;
                    else if(editing_=="port")config_.port=checked_integer(edit_text_,1,65535);
                    else if(editing_=="width")config_.width=checked_integer(edit_text_,1,32767);
                    else if(editing_=="height")config_.height=checked_integer(edit_text_,1,32767);
                    else if(editing_=="hold")config_.home_delay_ms=checked_integer(edit_text_,0,5000);
                    close_edit();
                    say("Draft changed. Save before starting a new connection.");
                }
                catch(const std::exception& e){
                    say(e.what(),true);
                }
            }
            void save(bool start) {
                try {
                    config_.validate(start);
                    auto j=Json::object();
                    j.set("op",Json::string("configure"));
                    j.set("config",config_.json());
                    post(start?"configure-start":"configure",j);
                }
                catch(const std::exception& e){
                    say(e.what(),true);
                }
            }
            void add(const std::string& id,const std::string& label,Rect r,bool selected=false) {
                buttons_.push_back({
                    id,label,r,selected}
                );
            }
            void draw_buttons(spice::Renderer& r) {
                for(const auto& b:buttons_) {
                    bool focus=b.id==focus_;
                    r.rect(b.rect,focus?Color::hex("#24302d"):Raised,12);
                    if(focus)r.rect(b.rect,Amber,12,2);
                    if(b.selected)r.rect({
                        b.rect.x+12,b.rect.y+b.rect.h-7,b.rect.w-24,3}
                    ,Teal,1);
                    r.text(r.ellipsis(b.label,25,b.rect.w-32),b.rect.x+16,b.rect.y+(b.rect.h-30)*.5f,25,focus?Text:Muted);
                }
            }
            void field(spice::Renderer& r,const std::string& id,const std::string& label,const std::string& value,float x,float y,float w) {
                r.text(label,x,y,23,Muted);
                add(id,value.empty()?"Select to enter":value,{
                    x,y+36,w,64}
                );
            }
            void text_line(spice::Renderer& r,const std::string& s,float x,float y,float width=1200,float size=25,Color color=Muted) {
                r.text(r.ellipsis(s,size,width),x,y,size,color);
            }
            public:
            bool quit=false;
            void visible(bool active) {
                if(active)refresh_=0;
            }
            int wait_ms(bool active)const {
                if(!active||status_pending_)return -1;
                return static_cast<int>(std::clamp<int64_t>(refresh_-monotonic_ms(),0,2000));
            }
            explicit Deck(ControlWorker& w,spice::Sdl& s,std::string view):worker_(w),sdl_(s),page_(std::move(view)) {
                focus_="tab-"+page_;
            }
            // Returns whether anything drawn may have changed.
            bool update(bool active=true) {
                bool changed=false;
                for(auto& [tag,result]:worker_.take()) {
                    if(tag=="status"){
                        status_pending_=false;
                        // A periodic refresh identical to the shown status changes nothing.
                        if(loaded_&&result.get("ok").flag()&&result.dump()==status_.dump())continue;
                        if(!result.get("ok").flag()&&error_&&notice_==result.get("error").str("Daemon request failed"))continue;
                    }
                    changed=true;
                    if(!result.get("ok").flag()) {
                        say(result.get("error").str("Daemon request failed"),true);
                        continue;
                    }
                    if(tag=="status") {
                        status_=result;
                        const auto schema=static_cast<int>(status_.get("schema").num(-1));
                        if(schema!=synergy::ControlSchema) {
                            if(!schema_warned_) {
                                say("Daemon control schema "+std::to_string(schema)+" does not match UI schema "+std::to_string(synergy::ControlSchema)+"; update the app/daemon pair.",true);
                                schema_warned_=true;
                            }
                        }
                        else schema_warned_=false;
                        if(!loaded_){
                            config_=Config::from_json(status_.get("saved_config"));
                            loaded_=true;
                            say("Daemon available. Dry run first; real input needs explicit approval.");
                        }
                    }
                    else if(tag=="probe") {
                        probe_=result;
                        say("Probe complete. No virtual devices were created.");
                    }
                    else if(tag=="configure") {
                        say("Configuration saved; a running connection keeps its active settings.");
                    }
                    else if(tag=="configure-start")say("Configuration saved; start accepted. Check status for the connection result.");
                    else if(tag=="profile-load") {
                        auto p=result.get("profile");
                        corner_=p.get("corner").str("bottom-right");
                        corner_pixels_=static_cast<int>(p.get("cornerPixels").num(2));
                        delay_=static_cast<int>(p.get("switchDelayMs").num(250));
                        say("Local handoff reference loaded; desktop Synergy was not changed.");
                    }
                    else if(tag=="profile-save")say("Handoff reference saved locally. Apply server-side settings separately.");
                    else say("Request accepted: "+tag+". Check status for its result.");
                }
                if(active&&monotonic_ms()>=refresh_&&!status_pending_) {
                    auto j=Json::object();
                    j.set("op",Json::string("status"));
                    status_pending_=post("status",j);
                    if(!status_pending_)changed=true; // queue-full notice
                    refresh_=monotonic_ms()+2000;
                }
                return changed;
            }
            void activate(const std::string& id) {
                if(id.rfind("char-",0)==0) {
                    const std::string keys=uppercase_?"1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ.-_: ":"1234567890abcdefghijklmnopqrstuvwxyz.-_: ";
                    auto index=static_cast<size_t>(checked_integer(id.substr(5),0,100));
                    if(index<keys.size()&&edit_text_.size()<64)edit_text_+=keys[index];
                    return;
                }
                if(id=="edit-delete"){
                    spice::pop_utf8(edit_text_);
                    return;
                }
                if(id=="edit-clear"){
                    edit_text_.clear();
                    return;
                }
                if(id=="edit-case"){
                    uppercase_=!uppercase_;
                    return;
                }
                if(id=="edit-apply"){
                    apply_edit();
                    return;
                }
                if(id=="edit-cancel"){
                    close_edit();
                    return;
                }
                if(id.rfind("tab-",0)==0){
                    page_=id.substr(4);
                    focus_=id;
                    return;
                }
                if(id=="server")edit(id,config_.server);
                else if(id=="name")edit(id,config_.name);
                else if(id=="port")edit(id,std::to_string(config_.port));
                else if(id=="width")edit(id,std::to_string(config_.width));
                else if(id=="height")edit(id,std::to_string(config_.height));
                else if(id=="hold")edit(id,std::to_string(config_.home_delay_ms));
                else if(id=="dry"){
                    config_.dry_run=!config_.dry_run;
                    say(config_.dry_run?"Dry run selected.":"Real input selected. Plaintext approval and a rooted TV are also required.",!config_.dry_run);
                }
                else if(id=="plain"){
                    config_.allow_plaintext=!config_.allow_plaintext;
                    say("Unencrypted keyboard traffic is suitable only for an isolated trusted LAN.",true);
                }
                else if(id=="cursor"){
                    config_.cursor_keepalive=!config_.cursor_keepalive;
                    say("Experimental one-pixel cursor nudge; not absolute pointer synchronization.");
                }
                else if(id=="home-target")config_.home_target=config_.home_target=="system"?"off":"system";
                else if(id=="save")save(false);
                else if(id=="start")save(true);
                else if(id=="stop"||id=="probe")command(id);
                else if(id=="boot")command(status_.get("autostart").flag()?"autostart-disable":"autostart-enable");
                else if(id=="corner") {
                    static const std::vector<std::string> values{
                        "bottom-right","bottom-left","top-left","top-right"}
                    ;
                    auto it=std::find(values.begin(),values.end(),corner_);
                    corner_=values[(static_cast<size_t>(it-values.begin())+1)%values.size()];
                }
                else if(id=="corner-size")corner_pixels_=corner_pixels_>=20?2:corner_pixels_+2;
                else if(id=="switch-delay")delay_=delay_>=1000?0:delay_+250;
                else if(id=="profile-load")command(id);
                else if(id=="profile-save") {
                    auto p=Json::object();
                    p.set("corner",Json::string(corner_));
                    p.set("cornerPixels",Json::number(corner_pixels_));
                    p.set("switchDelayMs",Json::number(delay_));
                    p.set("switchDoubleTapMs",Json::number(0));
                    auto j=Json::object();
                    j.set("op",Json::string(id));
                    j.set("profile",p);
                    post(id,j);
                }
                else if(id=="exit")quit=true;
            }
            void key(int scan,int symbol,bool repeat,const std::string& name) {
                last_key_=name+" / scan "+std::to_string(scan)+" / key "+std::to_string(symbol);
                if(scan==41||symbol==461||name=="AC Back"||name=="Back") {
                    if(!editing_.empty())close_edit();
                    else if(page_!="link"){
                        page_="link";
                        focus_="tab-link";
                    }
                    else focus_="exit";
                    return;
                }
                if(scan==42&&!editing_.empty()){
                    spice::pop_utf8(edit_text_);
                    return;
                }
                if((scan==40||scan==88||symbol==13)&&!repeat){
                    activate(focus_);
                    return;
                }
                std::optional<spice::Direction> direction;
                if(scan==79||symbol==39)direction=spice::Direction::Right;
                if(scan==80||symbol==37)direction=spice::Direction::Left;
                if(scan==81||symbol==40)direction=spice::Direction::Down;
                if(scan==82||symbol==38)direction=spice::Direction::Up;
                if(direction) {
                    std::vector<spice::Node> nodes;
                    int index=0;
                    for(const auto& b:buttons_){
                        if(b.id==focus_)index=static_cast<int>(nodes.size());
                        nodes.push_back({
                            b.id,b.rect}
                        );
                    }
                    int next=spice::neighbor(nodes,index,*direction);
                    if(next>=0&&static_cast<size_t>(next)<buttons_.size())focus_=buttons_[static_cast<size_t>(next)].id;
                }
                if(scan==43&&!buttons_.empty()) {
                    auto it=std::find_if(buttons_.begin(),buttons_.end(),[&](const Button& b){
                        return b.id==focus_;}
                    );
                    size_t index=it==buttons_.end()?0:(static_cast<size_t>(it-buttons_.begin())+1)%buttons_.size();
                    focus_=buttons_[index].id;
                }
            }
            void text(const std::string& t) {
                if(editing_.empty())return;
                for(unsigned char c:t)if(c>=32&&c<127&&edit_text_.size()<64)edit_text_+=static_cast<char>(c);
            }
            // Returns whether focus or state may have changed.
            bool pointer(spice::Point point,bool click) {
                for(const auto& b:buttons_)if(b.rect.contains(point)){
                    const bool moved=focus_!=b.id;
                    focus_=b.id;
                    if(click)activate(b.id);
                    return moved||click;
                }
                return false;
            }
            Json report()const {
                auto j=Json::object();
                j.set("view",Json::string(page_));
                j.set("focus",Json::string(focus_));
                j.set("editing",Json::string(editing_));
                j.set("draft",config_.json());
                j.set("last_key",Json::string(last_key_));
                return j;
            }
            void draw(spice::Renderer& r) {
                buttons_.clear();
                r.rect({
                    0,0,1920,1080}
                ,Ink);
                r.gradient({
                    1180,0,740,1080}
                ,Color::hex("#142b2c"),Ink);
                for(int i=0;i<4;++i)r.rect({
                    64.f+static_cast<float>(i%2)*19,57.f+static_cast<float>(i/2)*19,14,14}
                ,i==3?Amber:Teal,3);
                r.text("Synergy",118,47,40,Text);
                r.text("WEBOS / NATIVE",298,66,18,Muted);
                r.text("Your desk, on the TV.",64,122,22,Muted);
                const std::vector<std::pair<std::string,std::string>> tabs{
                    {
                        "link","Connection"}
                    ,{
                        "input","Input"}
                    ,{
                        "handoff","Handoff"}
                    ,{
                        "diagnostics","Diagnostics"}
                }
                ;
                for(size_t i=0;i<tabs.size();++i)add("tab-"+tabs[i].first,tabs[i].second,{
                    64+static_cast<float>(i)*244,184,228,62}
                ,page_==tabs[i].first);
                add("exit","Close UI",Rect{
                    1650,60,205,62}
                );
                auto client=status_.get("client");
                auto state=client.get("state").str("daemon offline");
                r.text(state,1440,150,22,state=="active"?Teal:Muted);
                r.line(64,274,1856,274,Line);
                if(page_=="link") {
                    r.text("One desktop. One shared keyboard.",64,308,42,Text);
                    text_line(r,"Connect to Synergy Core on your desktop; this is input sharing, not screen streaming.",64,369,1770);
                    field(r,"server","DESKTOP LAN IP",config_.server,64,430,730);
                    field(r,"port","CORE PORT",std::to_string(config_.port),830,430,300);
                    field(r,"name","TV SCREEN NAME",config_.name,64,570,1066);
                    add("dry",config_.dry_run?"Dry run: ON":"Real input selected",{
                        64,720,335,66}
                    ,config_.dry_run);
                    add("save","Save settings",{
                        420,720,335,66}
                    );
                    add("start","Save + Connect",{
                        776,720,354,66}
                    );
                    add("stop","Stop / release input",{
                        64,815,335,66}
                    );
                    add("probe","Probe TV",{
                        420,815,335,66}
                    );
                    r.rect({
                        1190,430,666,451}
                    ,Panel,18);
                    r.text("Connection guardrails",1224,464,30,Text);
                    text_line(r,"01   Desktop runs Synergy server",1224,530,595);
                    text_line(r,"02   Screen name matches server layout",1224,579,595);
                    text_line(r,"03   Prove dry-run events first",1224,628,595);
                    text_line(r,"04   Root + uinput for real TV input",1224,677,595);
                    text_line(r,"Events: "+std::to_string(static_cast<uint64_t>(client.get("input_events").num())),1224,748,595,28,Teal);
                    text_line(r,"Saved settings do not mutate an active session.",1224,815,595,21);
                }
                else if(page_=="input") {
                    r.text("Deliberate control. Safe release.",64,308,42,Text);
                    text_line(r,"Meta opens Home; Alt stays normal until its five-second hold. Alt+Tab cycles running apps.",64,369,1770);
                    field(r,"width","PROTOCOL WIDTH",std::to_string(config_.width),64,432,390);
                    field(r,"height","PROTOCOL HEIGHT",std::to_string(config_.height),490,432,390);
                    field(r,"hold","META HOLD / MILLISECONDS",std::to_string(config_.home_delay_ms),916,432,440);
                    add("home-target","Meta target: "+config_.home_target,{
                        64,582,390,66}
                    );
                    add("cursor",config_.cursor_keepalive?"Cursor nudge: ON":"Cursor nudge: OFF",{
                        490,582,390,66}
                    ,config_.cursor_keepalive);
                    add("plain",config_.allow_plaintext?"Plaintext input approved":"Approve plaintext input",{
                        916,582,440,66}
                    ,config_.allow_plaintext);
                    text_line(r,"No TLS or clipboard. Never expose port 24800 to the Internet.",64,696,1740,27,Amber);
                    text_line(r,"Relative cursor movement is not guaranteed absolute positioning on webOS.",64,746,1740);
                    add("save","Save settings",{
                        64,814,390,66}
                    );
                    add("boot",status_.get("autostart").flag()?"Disable boot connection":"Enable boot connection",{
                        490,814,410,66}
                    ,status_.get("autostart").flag());
                    text_line(r,"Boot connection needs explicit root provisioning; legacy hooks are never replaced.",64,916,1740,22);
                }
                else if(page_=="handoff") {
                    r.text("Handoff is a server-side decision.",64,308,42,Text);
                    text_line(r,"Keep a local reference for desktop settings. Saving here never edits your Windows Synergy configuration.",64,369,1770);
                    add("corner","Corner: "+corner_,{
                        64,460,590,78}
                    );
                    add("corner-size","Corner reference: "+std::to_string(corner_pixels_)+" px",{
                        690,460,530,78}
                    );
                    add("switch-delay","Delay reference: "+std::to_string(delay_)+" ms",{
                        1256,460,600,78}
                    );
                    r.rect({
                        64,592,1792,184}
                    ,Panel,18);
                    r.text("Corner preferences, without hidden desktop changes.",96,625,32,Amber);
                    text_line(r,"The legacy desktop helper uses a server edge approximation. This client does not implement a new server protocol.",96,685,1725,25);
                    add("profile-save","Save local reference",{
                        64,815,430,66}
                    );
                    add("profile-load","Load local reference",{
                        530,815,430,66}
                    );
                }
                else {
                    r.text("Connection and input diagnostics.",64,308,42,Text);
                    add("probe","Read-only input probe",{
                        64,390,400,62}
                    );
                    text_line(r,"UI UID: "+std::to_string(geteuid())+"   |   Controller UID: "+std::to_string(static_cast<int>(status_.get("controller_uid").num(-1)))+"   |   C++20 / SDL2 / raw GLES2",500,405,1350,23);
                    r.rect({
                        64,488,1120,430}
                    ,Panel,16);
                    auto logs=client.get("log");
                    size_t first=logs.size()>9?logs.size()-9:0;
                    if(logs.size()==0)text_line(r,"No daemon log available.",92,522,1040);
                    for(size_t i=first;i<logs.size();++i)text_line(r,logs.at(i).str(),92,522+static_cast<float>(i-first)*39,1065,22);
                    text_line(r,"PROBE",1224,495,600,20,Teal);
                    text_line(r,"Root: "+std::string(probe_.get("root").flag()?"yes":"not established"),1224,544,620,24);
                    text_line(r,"uinput writable: "+std::string(probe_.get("uinput_writable").flag()?"yes":"not established"),1224,590,620,24);
                    text_line(r,"Peer authentication: Unix UID",1224,636,620,24);
                    text_line(r,"Shared app UID is a shared trust boundary.",1224,682,620,22,Amber);
                    text_line(r,"SynergyWebOS "+std::string(SYNERGY_VERSION),1224,712,620,22);
                    text_line(r,r.renderer,1224,758,620,22);
                    text_line(r,r.version,1224,801,620,22);
                    text_line(r,last_key_,64,936,1740,21);
                }
                draw_buttons(r);
                if(!editing_.empty()) {
                    r.rect({
                        0,0,1920,1080}
                    ,Ink.opacity(.92f));
                    r.rect({
                        310,168,1300,718}
                    ,Panel,24);
                    r.text("Edit "+editing_,350,202,32,Text);
                    r.text(r.ellipsis(edit_text_+"|",34,1200),350,262,34,Amber);
                    buttons_.clear();
                    const std::string keys=uppercase_?"1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ.-_: ":"1234567890abcdefghijklmnopqrstuvwxyz.-_: ";
                    for(size_t i=0;i<keys.size();++i)add("char-"+std::to_string(i),keys[i]==' '?"Space":std::string(1,keys[i]),{
                        350+static_cast<float>(i%10)*123,350+static_cast<float>(i/10)*82,111,68}
                    );
                    add("edit-delete","Delete",{
                        350,710,220,68}
                    );
                    add("edit-clear","Clear",{
                        594,710,220,68}
                    );
                    add("edit-case",uppercase_?"abc":"ABC",{
                        838,710,220,68}
                    );
                    add("edit-apply","Apply",{
                        1082,710,220,68}
                    );
                    add("edit-cancel","Cancel",{
                        1326,710,220,68}
                    );
                    draw_buttons(r);
                }
                r.rect({
                    0,986,1920,94}
                ,Panel);
                r.line(64,986,1856,986,Line);
                text_line(r,notice_,64,1007,1792,23,error_?Danger:Muted);
                text_line(r,"D-pad / pointer to navigate    OK to select    Back to cancel    Close UI keeps the daemon running",64,1044,1792,18);
            }
        }
        ;
        struct Window {
            spice::Sdl& s;
            SDL_Window* window=nullptr;
            SDL_GLContext gl=nullptr;
            explicit Window(spice::Sdl& sdl):s(sdl){
            }
            ~Window(){
                if(gl)s.GL_DeleteContext(gl);
                if(window)s.DestroyWindow(window);
                s.Quit();
            }
        }
        ;
    }
    int ui_main(int argc,char** argv) {
        int width=1280,height=720,frames=0;
        bool fullscreen=false;
        #ifdef SYNERGY_WEBOS
        width=1920; height=1080; fullscreen=true;
#endif
        std::string font,shot,view="link",report_path,qa_path;
        auto root=default_runtime();
        for(int i=1;i<argc;++i) {
            std::string a=argv[i];
            auto value=[&]{
                if(++i>=argc)throw std::runtime_error("Missing option value");
                return std::string(argv[i]);
            }
            ;
            if(a=="--runtime-dir")root=value();
            else if(a=="--font")font=value();
            else if(a=="--width")width=checked_integer(value(),640,7680);
            else if(a=="--height")height=checked_integer(value(),360,4320);
            else if(a=="--frames")frames=checked_integer(value(),1,10000);
            else if(a=="--screenshot")shot=value();
            else if(a=="--report")report_path=value();
            else if(a=="--qa-events")qa_path=value();
            else if(a=="--view")view=value();
            else if(a=="--fullscreen")fullscreen=true;
            else if(a=="--version"){
                std::cout<<SYNERGY_VERSION<<'\n';
                return 0;
            }
            else if(a=="--identity"){
                std::cout<<"UID="<<geteuid()<<" APP="<<AppId<<'\n';
                return 0;
            }
            else if(a=="--help"){
                std::cout<<"synergy-tv --runtime-dir ABS --view link|input|handoff|diagnostics\n--font INSTALLED_FONT --fullscreen --width N --height N --frames N --screenshot PNG\n--report JSON --qa-events JSON --identity --version\n";
                return 0;
            }
            else if(!a.empty()&&a.front()=='{') {
                auto j=Json::parse(a,4096);
#ifdef SYNERGY_WEBOS
                if(!j.get("@system_native_app").flag()||j.get("nid").str()!=AppId)throw std::runtime_error("Unexpected native launch identity");
                fullscreen=true;
                width=1920;
                height=1080;
#else
                (void)j;
                throw std::runtime_error("webOS launch arguments rejected by desktop build");
#endif
            }
            else throw std::runtime_error("Unknown UI option: "+a);
        }
        if(view!="link"&&view!="input"&&view!="handoff"&&view!="diagnostics")throw std::runtime_error("Unknown view");
        if(!shot.empty()&&frames==0)throw std::runtime_error("Screenshot requires --frames");
        Json qa=Json::array();
        if(!qa_path.empty()) {
            std::ifstream f(qa_path);
            std::string text((std::istreambuf_iterator<char>(f)),{
            }
            );
            qa=Json::parse(text,65536);
            if(!qa.is(json_type_array)||qa.size()>100)throw std::runtime_error("Invalid QA events");
        }
#ifdef SYNERGY_WEBOS
        // The jail puts /usr/lib first; pin the compatible packaged SDL rather
        // than silently loading the firmware's older SDL with the same SONAME.
        if(setenv("APPID",AppId,1)!=0)throw std::runtime_error("Cannot set native SDL app identity");
        spice::Sdl sdl(std::string("/media/developer/apps/usr/palm/applications/")+AppId+"/lib/libSDL2-2.0.so.0");
#else
        spice::Sdl sdl;
#endif
        Window window(sdl);
        sdl.SetMainReady();
        if(sdl.Init(SDL_INIT_VIDEO)!=0)throw std::runtime_error(sdl.GetError());
        sdl.GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
        sdl.GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
        sdl.GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
        sdl.GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
        sdl.GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);
        window.window=sdl.CreateWindow(("SynergyWebOS "+std::string(SYNERGY_VERSION)+" | Native").c_str(),SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|(fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0));
        if(!window.window)throw std::runtime_error(sdl.GetError());
        window.gl=sdl.GL_CreateContext(window.window);
        if(!window.gl)throw std::runtime_error(sdl.GetError());
        sdl.GL_SetSwapInterval(1);
        sdl.StopTextInput();
        spice::Renderer renderer(sdl,spice::find_font(font),{
        }
        );
        if(!renderer.is_es)throw std::runtime_error("OpenGL ES context required");
        std::function<Json(const Json&,int)> control_transport;
#ifdef SYNERGY_USE_LS2
        control_transport=[](const Json& job,int timeout){return ls2_request(job,timeout);};
#endif
        ControlWorker worker(root,[&]{
            SDL_Event event{};
            event.type=ControlReadyEvent;
            sdl.PushEvent(&event);
        },std::move(control_transport));
        Deck deck(worker,sdl,view);
        bool active=true,dirty=true;
        int rendered=0;
        std::cout<<"SynergyWebOS native "<<SYNERGY_VERSION<<"; UI UID="<<geteuid()<<"; "<<renderer.renderer<<'\n';
        auto viewport=[&]{
            int ww=0,wh=0,dw=0,dh=0;
            sdl.GetWindowSize(window.window,&ww,&wh);
            sdl.GL_GetDrawableSize(window.window,&dw,&dh);
            return spice::Viewport::make(ww,wh,dw,dh);
        }
        ;
        auto geometry=viewport();
        while(!deck.quit) {
            auto began=std::chrono::steady_clock::now();
            for(size_t i=0;i<qa.size();++i) {
                auto q=qa.at(i);
                if(static_cast<int>(q.get("frame").num(-1))!=rendered)continue;
                SDL_Event e{
                }
                ;
                auto type=q.get("type").str();
                if(type=="key"){
                    e.type=SDL_KEYDOWN;
                    e.key.keysym.scancode=static_cast<SDL_Scancode>(q.get("scan").num());
                    e.key.keysym.sym=static_cast<SDL_Keycode>(q.get("key").num());
                }
                else if(type=="click"){
                    e.type=SDL_MOUSEBUTTONDOWN;
                    e.button.button=1;
                    e.button.x=static_cast<int>(q.get("x").num());
                    e.button.y=static_cast<int>(q.get("y").num());
                }
                else if(type=="text"){
                    e.type=SDL_TEXTINPUT;
                    auto t=q.get("text").str();
                    if(t.size()>31)throw std::runtime_error("QA text too long");
                    std::copy(t.begin(),t.end(),e.text.text);
                }
                else throw std::runtime_error("Unknown QA event");
                sdl.PushEvent(&e);
            }
            SDL_Event event{
            }
            ;
            auto handle=[&](const SDL_Event& e){
                if(e.type==SDL_QUIT)deck.quit=true;
                else if(e.type==SDL_WINDOWEVENT) {
                    const bool was_active=active;
                    auto code=e.window.event;
                    if(code==SDL_WINDOWEVENT_CLOSE)deck.quit=true;
                    if(code==SDL_WINDOWEVENT_HIDDEN||code==SDL_WINDOWEVENT_MINIMIZED||code==SDL_WINDOWEVENT_FOCUS_LOST)active=false;
                    if(code==SDL_WINDOWEVENT_SHOWN||code==SDL_WINDOWEVENT_RESTORED||code==SDL_WINDOWEVENT_FOCUS_GAINED)active=true;
                    if(active!=was_active)deck.visible(active);
                    if(code==SDL_WINDOWEVENT_RESIZED||code==SDL_WINDOWEVENT_SIZE_CHANGED)geometry=viewport();
                    // Expose, resize, show/resume and focus changes all repaint.
                    dirty=true;
                }
                else if(e.type==SDL_KEYDOWN){
                    deck.key(static_cast<int>(e.key.keysym.scancode),e.key.keysym.sym,e.key.repeat!=0,sdl.GetScancodeName(e.key.keysym.scancode));
                    dirty=true;
                }
                else if(e.type==SDL_TEXTINPUT){
                    deck.text(e.text.text);
                    dirty=true;
                }
                else if(e.type==SDL_MOUSEMOTION&&(e.motion.xrel||e.motion.yrel)){
                    if(auto p=geometry.pointer(static_cast<float>(e.motion.x),static_cast<float>(e.motion.y)))dirty|=deck.pointer(*p,false);
                }
                else if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.button==1){
                    if(auto p=geometry.pointer(static_cast<float>(e.button.x),static_cast<float>(e.button.y)))dirty|=deck.pointer(*p,true);
                }
            }
            ;
            // Results push a user event. Only visible status refresh has a timer;
            // hidden windows sleep until input, results or a window event arrives.
            if(frames==0&&(!active||!dirty)){
                if(sdl.WaitEventTimeout(&event,deck.wait_ms(active)))handle(event);
            }
            while(sdl.PollEvent(&event))handle(event);
            dirty|=deck.update(active);
            if(frames>0||(active&&dirty)) {
                dirty=false;
                renderer.begin(geometry);
                deck.draw(renderer);
                renderer.end();
                ++rendered;
                if(frames&&rendered>=frames){
                    if(!shot.empty())renderer.screenshot(shot);
                    deck.quit=true;
                }
                sdl.GL_SwapWindow(window.window);
                // Pace only frames that were actually drawn.
                if(sdl.GL_GetSwapInterval()==0)std::this_thread::sleep_until(began+std::chrono::milliseconds(16));
            }
        }
        if(!report_path.empty()){
            std::ofstream out(report_path);
            out<<deck.report().dump(true)<<'\n';
            if(!out)throw std::runtime_error("Cannot write UI report");
        }
        return 0;
    }
}
int main(int argc,char** argv){
    try{
        return synergy::ui_main(argc,argv);
    }
    catch(const std::exception& e){
        std::cerr<<"synergy-tv: "<<e.what()<<'\n';
        return 2;
    }
}
