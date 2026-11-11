#include "synergy/protocol.hpp"
#include "synergy/platform.hpp"
#include "synergy/input.hpp"
#include "spice/core.hpp"
#include <unistd.h>
#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <tuple>
#include <random>
using namespace synergy;
namespace {
    int checks=0;
    void check(bool ok,const std::string& name){
        ++checks;
        if(!ok)throw std::runtime_error(name);
    }
    template<class F>void rejects(F f,const std::string& name){
        bool failed=false;
        try{
            f();
        }
        catch(const std::exception&){
            failed=true;
        }
        check(failed,name);
    }
    Bytes b(std::string_view s){
        return {
            s.begin(),s.end()}
        ;
    }
    void p16(Bytes& v,int n){
        v.push_back(static_cast<uint8_t>(n>>8));
        v.push_back(static_cast<uint8_t>(n));
    }
    void p32(Bytes& v,uint32_t n){
        for(int i=24;i>=0;i-=8)v.push_back(static_cast<uint8_t>(n>>i));
    }
    Bytes enter(int mask=0){
        auto v=b("CINN");
        p16(v,100);
        p16(v,200);
        p32(v,1);
        p16(v,mask);
        return v;
    }
    Bytes key(std::string_view cmd,uint16_t id,int mask,int button){
        auto v=b(cmd);
        p16(v,id);
        p16(v,mask);
        if(cmd=="DKRP")p16(v,1);
        p16(v,button);
        return v;
    }
    Bytes move(std::string_view cmd,int x,int y){
        auto v=b(cmd);
        p16(v,x);
        p16(v,y);
        return v;
    }
    struct Recorder:Input {
        using E=std::tuple<char,int,int>;
        std::vector<E> events;
        void key(int c,int v)override{
            events.emplace_back('k',c,v);
        }
        void button(int c,int v)override{
            events.emplace_back('b',c,v);
        }
        void move(int x,int y)override{
            events.emplace_back('m',x,y);
        }
        void wheel(int x,int y)override{
            events.emplace_back('w',x,y);
        }
        int count(char t,int x,int y)const{
            return static_cast<int>(std::count(events.begin(),events.end(),E{
                t,x,y}
            ));
        }
    }
    ;
    struct Harness {
        Recorder input;
        std::vector<Bytes> sent;
        std::vector<Action> actions;
        int resets=0;
        Session session;
        explicit Harness(Config c={
        }
        ):session(c,input,[&](const Bytes& v){
            sent.push_back(v);}
        ,[&](Action a){
            actions.push_back(a);}
        ,[&]{
            ++resets;}
        ){
        }
        void handshake(){
            auto h=b("Synergy");
            p16(h,1);
            p16(h,6);
            session.handle(h,0);
            session.handle(b("QINF"),0);
        }
        void active(int mask=0){
            handshake();
            session.handle(enter(mask),100);
        }
    }
    ;
    void tests(){
        {
            FrameDecoder d;
            Bytes f=frame(b("CALV"));
            std::vector<Bytes> got;
            for(auto c:f){
                auto r=d.feed(std::span(&c,1));
                got.insert(got.end(),r.begin(),r.end());
            }
            check(got==std::vector<Bytes>{
                b("CALV")}
            ,"bytewise frame");
            check(!d.partial(),"frame drained");
            auto copy=f;
            f.insert(f.end(),copy.begin(),copy.end());
            check(d.feed(f).size()==2,"coalesced frames");
        }
        {
            auto payload=b("Synergy");
            p16(payload,1);
            p16(payload,4);
            auto f=frame(payload);
            for(size_t split=1;split<f.size();++split){
                FrameDecoder d;
                check(d.feed(std::span(f.data(),split)).empty(),"partial frame preserved");
                check(d.feed(std::span(f.data()+split,f.size()-split)).at(0)==payload,"every split reassembled");
            }
        }
        for(auto bytes:std::vector<Bytes>{
            {
                0,0,0,0}
            ,{
                0,0,0,3}
            ,{
                255,255,255,255}
            ,{
                0x16,3,3,0}
        }
        )rejects([&]{
            FrameDecoder d;d.feed(bytes);}
        ,"hostile length/TLS rejected");
        {
            FrameDecoder d;
            auto f=frame(Bytes(1024*1024,'x'));
            check(d.feed(f).at(0).size()==1024*1024,"maximum bounded frame");
        }
        rejects([]{
            frame(Bytes(1024*1024+1));}
        ,"oversized outbound frame rejected");
        {
            Harness h;
            rejects([&]{
                h.session.handle(b("QINF"),0);}
            ,"hello required");
        }
        {
            Harness h;
            h.handshake();
            check(h.sent[0].size()==23,"hello response length");
            check(h.sent[1].size()==18,"DINF length");
            check(std::string(h.sent[1].begin(),h.sent[1].begin()+4)=="DINF","DINF tag");
            check(h.session.ready(),"negotiated");
            h.session.handle(key("DKDN",'a',0,30),100);
            check(h.input.events.empty(),"inactive key blocked");
            h.session.handle(b("CALV"),100);
            check(h.sent[h.sent.size()-2]==b("CALV")&&h.sent.back()==b("CNOP"),"keepalive order");
        }
        {
            Harness h;
            auto hello=b("Barrier");
            p16(hello,1);
            p16(hello,4);
            h.session.handle(hello,0);
            check(std::equal(h.sent[0].begin(),h.sent[0].begin()+7,"Barrier"),"Barrier hello retained");
        }
        {
            Harness h;
            h.active();
            h.session.handle(move("DMMV",125,190),200);
            h.session.handle(move("DMRM",-4,8),201);
            check(h.input.count('m',25,-10)==1,"entry coordinates seed relative motion");
            check(h.input.count('m',-4,8)==1,"signed relative motion");
            h.session.handle(move("DMWM",0,60),202);
            check(h.input.count('w',0,1)==0,"wheel remainder retained");
            h.session.handle(move("DMWM",-120,60),203);
            check(h.input.count('w',-1,1)==1,"wheel accumulation");
        }
        {
            Harness h;
            h.active();
            auto down=b("DMDN");
            down.push_back(1);
            h.session.handle(down,200);
            h.session.handle(down,201);
            h.session.handle(key("DKDN",'a',0,30),202);
            h.session.handle(key("DKDN",'a',0,30),203);
            h.session.handle(key("DKRP",'a',0,30),204);
            check(h.input.count('k',30,1)==1,"duplicate down suppressed");
            check(h.input.count('k',30,2)==1,"repeat injected");
            h.session.handle(b("COUT"),205);
            check(h.input.count('k',30,0)==1&&h.input.count('b',0x110,0)==1,"COUT releases keys and buttons");
            check(h.session.pressed()==0&&!h.session.active(),"COUT clears capture");
            h.session.handle(key("DKRP",'a',0,30),206);
            check(h.input.count('k',30,2)==1,"inactive repeat discarded");
        }
        {
            Harness h;
            h.active(3);
            check(h.input.count('k',42,1)==1&&h.input.count('k',29,1)==1,"entry shift ctrl inherited");
            h.session.handle(key("DKUP",0xefe2,2,54),200);
            check(h.input.count('k',42,0)==1,"right shift release releases inherited left shift");
            h.session.release();
            check(h.input.count('k',29,0)==1,"disconnect inherited ctrl release");
        }
        {
            Harness h;
            h.active(1);
            h.session.handle(key("DKDN",0xefe2,1,54),200);
            check(h.input.count('k',54,1)==1&&h.input.count('k',42,0)==1,"physical modifier replaces inherited peer");
        }
        {
            Harness h;
            h.active();
            h.session.handle(key("DKDN",0xefeb,0,0xe05b),200);
            h.session.handle(key("DKRP",0xefeb,0,0xe05b),201);
            check(h.actions.size()==1&&h.actions[0]==Action::Home,"Meta once per hold");
            check(h.input.events.empty(),"Meta never injected");
            h.session.handle(key("DKUP",0xefeb,0,0xe05b),202);
            h.session.handle(key("DKDN",0xefec,0,0xe05c),203);
            check(h.actions.size()==2,"right Meta next hold");
        }
        {
            Config c;
            c.home_delay_ms=400;
            Harness h(c);
            h.active();
            h.session.handle(key("DKDN",0xefeb,0,0x15b),200);
            h.session.tick(599);
            check(h.actions.empty(),"Meta hold not early");
            h.session.tick(600);
            check(h.actions.size()==1,"Meta hold threshold");
        }
        {
            Config c;
            c.home_delay_ms=400;
            Harness h(c);
            h.active();
            h.session.handle(key("DKDN",0xefeb,0,0x15b),200);
            h.session.handle(b("COUT"),300);
            h.session.tick(1000);
            check(h.actions.empty(),"leave cancels pending Home");
        }
        {
            Config c;
            c.home_target="off";
            Harness h(c);
            h.active();
            h.session.handle(key("DKDN",0xefeb,0,0x15b),200);
            h.session.tick(9000);
            check(h.actions.empty(),"Meta disabled");
        }
        {
            Harness h;
            h.active(0x18);
            h.session.tick(10000);
            check(h.actions.empty(),"inherited Meta never launches Home");
        }
        {
            Harness h;
            h.active();
            h.session.handle(key("DKDN",0xefe9,4,56),200);
            h.session.tick(5199);
            check(h.actions.empty(),"Alt not early");
            h.session.tick(5200);
            check(h.actions==std::vector<Action>{
                Action::Home}
            ,"Alt 5s Home");
            check(h.input.count('k',56,0)==1,"Alt released before action");
            h.session.tick(8000);
            check(h.actions.size()==1,"Alt once");
        }
        {
            Harness h;
            h.active();
            h.session.handle(key("DKDN",0xefea,4,0x138),200);
            h.session.handle(key("DKDN",0xef09,5,15),201);
            h.session.handle(key("DKRP",0xef09,5,15),202);
            h.session.handle(key("DKUP",0xef09,4,15),203);
            h.session.tick(9000);
            check(h.actions==std::vector<Action>{
                Action::CycleBackward}
            ,"Alt Shift Tab and hold cancellation");
            check(h.input.count('k',15,1)==0,"Tab consumed");
            check(h.resets==1,"cycle cache reset on first Alt hold");
        }
        {
            Harness h;
            h.active();
            h.session.handle(key("DKDN",0xefea,4,0x138),200);
            h.session.handle(key("DKUP",0xefea,4,0x138),201);
            h.session.handle(key("DKDN",0xefea,4,0x138),202);
            check(h.resets==2,"cycle cache reset on each Alt hold");
        }
        {
            Config c;
            c.cursor_keepalive=true;
            Harness h(c);
            h.active();
            h.session.tick(2100);
            check(h.input.count('m',1,0)==1&&h.input.count('m',-1,0)==1,"optin cursor nudge");
        }
        {
            Harness h;
            h.active();
            h.session.tick(2100);
            check(h.input.events.empty(),"no cursor nudge by default");
            rejects([&]{
                h.session.handle(b("DKDN"),100);}
            ,"short key frame rejected");
            rejects([&]{
                h.session.handle(b("XXXX"),100);}
            ,"unknown command fails closed");
        }
        for(auto cmd:{
            "EICV","EBAD","EUNK","EBSY"}
        ){
            Harness h;
            h.handshake();
            rejects([&]{
                h.session.handle(b(cmd),0);}
            ,"server rejection handled");
        }
        {
            Harness h;
            h.active();
            h.session.handle(key("DKDN",'a',0,30),200);
            check(!h.session.handle(b("CBYE"),201),"CBYE stops");
            check(h.input.count('k',30,0)==1,"CBYE release");
        }
        check(linux_key(0,0xe048)==103&&linux_key(0,0x148)==103,"two Windows extended button encodings");
        check(linux_key(0xef51,0x1e)==105,"semantic navigation key wins");
        check(linux_key('A',0)==30,"ASCII fallback");
        check(linux_key(0xefc9,0)==88,"F12");
        check(linux_key(0x20ac,0)==0,"unsupported Unicode not guessed");
        Config c;
        c.server="192.168.1.10";
        c.validate();
        auto round=Config::from_json(Json::parse(c.json().dump()));
        check(round.json().dump()==c.json().dump(),"typed config roundtrip");
        for(auto host:{
            "8.8.8.8","host.example","1.2.3.4;rm","::","192.168.1.1\n"}
        )check(!numeric_lan_address(host),"unsafe/public endpoint rejected");
        for(auto host:{
            "127.0.0.1","192.168.0.2","10.2.3.4","172.16.1.1","172.31.1.1","169.254.1.2","::1","fd00::1"}
        )check(numeric_lan_address(host),"numeric local address allowed");
        for(auto json:{
            R"({"width":0})",R"({"width":32768})",R"({"port":1.5})",R"({"dry_run":"false"})",R"({"command":"sh"})",R"({"name":"a b"})",R"({"home_target":"shell"})"}
        )rejects([&]{
            Config::from_json(Json::parse(json));}
        ,"config validation");
        c.dry_run=false;
        rejects([&]{
            c.validate();}
        ,"plaintext real input explicit approval");
        c.allow_plaintext=true;
        c.validate();
        check(!safe_app_id("a;touch /tmp/x")&&safe_app_id("com.webos.app.home"),"application ID allowlist");
        {
            auto v=spice::Viewport::make(1280,720,3840,2160);
            auto point=v.pointer(640,360);
            check(point&&point->x==960&&point->y==540,"DPI pointer mapped once");
            auto square=spice::Viewport::make(1000,1000,1000,1000);
            check(!square.pointer(500,0),"letterbox click rejected");
            std::vector<spice::Node> nodes{
                {
                    "a",{
                        0,0,100,50}
                }
                ,{
                    "b",{
                        120,0,100,50}
                }
                ,{
                    "c",{
                        0,80,100,50}
                }
            }
            ;
            check(spice::neighbor(nodes,0,spice::Direction::Right)==1,"Dpad aligned neighbor");
        }
        {
            auto r=bounded_process("/bin/echo",{
            "literal;not-shell"}
            );
            check(r.exit_code==0&&r.output=="literal;not-shell\n","spawn literal argv no shell");
            auto slow=bounded_process("/bin/sleep",{
            "5"}
            ,60);
            check(slow.timed_out,"process deadline");
            auto flood=bounded_process("/usr/bin/yes",{
            }
            ,1000,128);
            check(flood.too_large,"process output bounded");
        }
        {
            auto path=std::filesystem::path("/tmp")/("syn-core-"+std::to_string(getpid()));
            validate_runtime(path,true);
            atomic_private_write(path/"config","original");
            check(read_private_file(path/"config")=="original","atomic state readwrite");
            check(symlink("config",(path/"alias").c_str())==0,"test symlink created");
            rejects([&]{
                read_private_file(path/"alias");}
            ,"symlink read rejected");
            rejects([&]{
                atomic_private_write(path/"alias","bad");}
            ,"symlink overwrite rejected");
            check(read_private_file(path/"config")=="original","original preserved");
            chmod((path/"config").c_str(),0644);
            rejects([&]{
                read_private_file(path/"config");}
            ,"world readable state rejected");
            std::filesystem::remove_all(path);
        }
#ifndef SYNERGY_WEBOS
        rejects([]{
            make_input(false,[]{
            }
            );}
        ,"host build cannot create input devices");
#endif
        // Deterministic malformed-packet stress, no native input backend involved.
        std::mt19937 random(42);
        for(int i=0;i<2000;++i){
            Harness h;
            h.active();
            Bytes fuzz(static_cast<size_t>(random()%40));
            for(auto& byte:fuzz)byte=static_cast<uint8_t>(random());
            try{
                h.session.handle(fuzz,1000);
            }
            catch(const std::exception&){
            }
            h.session.release();
        }
        check(true,"2000 malformed packet cases completed");
    }
}
int main(){
    try{
        tests();
        std::cout<<"PASS: "<<checks<<" assertions; 2000 malformed-packet cases\n";
        return 0;
    }
    catch(const std::exception& e){
        std::cerr<<"FAIL after "<<checks<<" assertions: "<<e.what()<<'\n';
        return 1;
    }
}
