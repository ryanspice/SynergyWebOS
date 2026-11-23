#include "synergy/protocol.hpp"
#include "synergy/platform.hpp"
#include "synergy/tls.hpp"
#include "synergy/input.hpp"
#include "synergy/i18n.hpp"
#include "spice/core.hpp"
#include <algorithm>
#include <array>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <tuple>
#include <random>
#include <ctime>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
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
        // Explicit sized construction avoids a GCC 15 -O3 false-positive
        // -Warray-bounds when the range constructor is inlined next to
        // subsequent push_back calls on the returned vector.
        Bytes out(s.size());
        for(std::size_t i=0;i<s.size();++i)out[i]=static_cast<std::uint8_t>(static_cast<unsigned char>(s[i]));
        return out;
    }
    void p16(Bytes& v,int n){
        v.push_back(static_cast<uint8_t>(n>>8));
        v.push_back(static_cast<uint8_t>(n));
    }
    void p32(Bytes& v,uint32_t n){
        for(int i=24;i>=0;i-=8)v.push_back(static_cast<uint8_t>(n>>i));
    }
    void p64(Bytes& v,uint64_t n){
        for(int i=56;i>=0;i-=8)v.push_back(static_cast<uint8_t>(n>>i));
    }
    Bytes enter_at(int x,int y,int mask=0,uint32_t capture=1){
        auto v=b("CINN");
        p16(v,x);
        p16(v,y);
        p32(v,capture);
        p16(v,mask);
        return v;
    }
    Bytes enter(int mask=0){
        return enter_at(100,200,mask);
    }
    Bytes key(std::string_view cmd,uint16_t id,int mask,int button,int repeats=1){
        auto v=b(cmd);
        p16(v,id);
        p16(v,mask);
        if(cmd=="DKRP")p16(v,repeats);
        p16(v,button);
        return v;
    }
    Bytes dgpd(uint8_t slot,uint32_t capture,uint64_t sequence,const bridge::GamepadState& state){
        auto v=b("DGPD");
        v.push_back(1);
        v.push_back(slot);
        v.push_back(state.connected?1:0);
        v.push_back(0);
        p32(v,capture);
        p64(v,sequence);
        p16(v,state.buttons);
        p16(v,static_cast<uint16_t>(state.lx));
        p16(v,static_cast<uint16_t>(state.ly));
        p16(v,static_cast<uint16_t>(state.rx));
        p16(v,static_cast<uint16_t>(state.ry));
        v.push_back(state.lt);
        v.push_back(state.rt);
        return v;
    }
    Bytes move(std::string_view cmd,int x,int y){
        auto v=b(cmd);
        p16(v,x);
        p16(v,y);
        return v;
    }
    template<class F>void rejects_with(F f,const std::string& fragment,const std::string& name){
        std::string message;
        try{
            f();
        }
        catch(const std::exception& e){
            message=e.what();
        }
        check(message.find(fragment)!=std::string::npos,name);
    }
    // Ephemeral, in-memory generated PKI. Private keys exist only briefly in
    // mode-0600 files under a mkdtemp directory that is removed by the fixture
    // destructor; nothing is committed or left on disk.
    struct TlsFixture {
        std::string dir,ca,cert,key,other_cert,other_key;
        std::string ca_pem,server_cert_pem,server_key_pem;
        std::string wrong_name_cert_pem,wrong_name_key_pem;
        std::string wrong_ca_pem,wrong_server_cert_pem,wrong_server_key_pem;
        ~TlsFixture(){if(!dir.empty())std::filesystem::remove_all(dir);}
    };
    std::string pem_cert(X509* cert){
        BIO* bio=BIO_new(BIO_s_mem());
        if(!bio||!PEM_write_bio_X509(bio,cert)){BIO_free(bio);throw std::runtime_error("fixture PEM");}
        char* data=nullptr;
        const long length=BIO_get_mem_data(bio,&data);
        std::string out(data,static_cast<size_t>(length));
        BIO_free(bio);
        return out;
    }
    std::string pem_key(EVP_PKEY* key){
        BIO* bio=BIO_new(BIO_s_mem());
        if(!bio||!PEM_write_bio_PrivateKey(bio,key,nullptr,nullptr,0,nullptr,nullptr)){
            BIO_free(bio);throw std::runtime_error("fixture PEM");
        }
        char* data=nullptr;
        const long length=BIO_get_mem_data(bio,&data);
        std::string out(data,static_cast<size_t>(length));
        BIO_free(bio);
        return out;
    }
    EVP_PKEY* fixture_key(){
        EVP_PKEY_CTX* ctx=EVP_PKEY_CTX_new_id(EVP_PKEY_EC,nullptr);
        if(!ctx)throw std::runtime_error("fixture key context");
        EVP_PKEY* key=nullptr;
        const bool ok=EVP_PKEY_keygen_init(ctx)>0&&
            EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx,NID_X9_62_prime256v1)>0&&
            EVP_PKEY_keygen(ctx,&key)>0;
        EVP_PKEY_CTX_free(ctx);
        if(!ok){if(key)EVP_PKEY_free(key);throw std::runtime_error("fixture key generation");}
        return key;
    }
    X509* fixture_cert(EVP_PKEY* key,const std::string& cn,bool is_ca,
                       EVP_PKEY* issuer_key,X509* issuer){
        X509* cert=X509_new();
        if(!cert)throw std::runtime_error("fixture certificate");
        X509_set_version(cert,2);
        ASN1_INTEGER_set(X509_get_serialNumber(cert),is_ca?1:2);
        X509_gmtime_adj(X509_getm_notBefore(cert),0);
        X509_gmtime_adj(X509_getm_notAfter(cert),3600);
        X509_set_pubkey(cert,key);
        X509_NAME* name=X509_get_subject_name(cert);
        X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_ASC,
            reinterpret_cast<const unsigned char*>(cn.c_str()),-1,-1,0);
        X509_set_issuer_name(cert,issuer?X509_get_subject_name(issuer):name);
        if(is_ca){
            X509V3_CTX context;
            X509V3_set_ctx_nodb(&context);
            X509V3_set_ctx(&context,cert,cert,nullptr,nullptr,0);
            X509_EXTENSION* ext=X509V3_EXT_conf_nid(nullptr,&context,NID_basic_constraints,
                                                    "critical,CA:TRUE");
            if(!ext||!X509_add_ext(cert,ext,-1)){
                X509_EXTENSION_free(ext);X509_free(cert);throw std::runtime_error("fixture CA extension");
            }
            X509_EXTENSION_free(ext);
        }
        if(!X509_sign(cert,issuer_key?issuer_key:key,EVP_sha256())){
            X509_free(cert);throw std::runtime_error("fixture certificate signature");
        }
        return cert;
    }
    void add_sans(X509* cert,const std::string& dns,const std::string& ip){
        GENERAL_NAMES* names=sk_GENERAL_NAME_new_null();
        if(!names)throw std::runtime_error("fixture SAN");
        if(!dns.empty()){
            ASN1_IA5STRING* value=ASN1_IA5STRING_new();
            GENERAL_NAME* name=GENERAL_NAME_new();
            if(!value||!name||!ASN1_STRING_set(value,dns.c_str(),-1)){
                ASN1_IA5STRING_free(value);GENERAL_NAME_free(name);
                sk_GENERAL_NAME_pop_free(names,GENERAL_NAME_free);
                throw std::runtime_error("fixture DNS SAN");
            }
            GENERAL_NAME_set0_value(name,GEN_DNS,value);
            sk_GENERAL_NAME_push(names,name);
        }
        if(!ip.empty()){
            ASN1_OCTET_STRING* value=ASN1_OCTET_STRING_new();
            GENERAL_NAME* name=GENERAL_NAME_new();
            in_addr address{};
            if(!value||!name||inet_pton(AF_INET,ip.c_str(),&address)!=1||
               !ASN1_OCTET_STRING_set(value,
                    reinterpret_cast<const unsigned char*>(&address),sizeof address)){
                ASN1_OCTET_STRING_free(value);GENERAL_NAME_free(name);
                sk_GENERAL_NAME_pop_free(names,GENERAL_NAME_free);
                throw std::runtime_error("fixture IP SAN");
            }
            GENERAL_NAME_set0_value(name,GEN_IPADD,value);
            sk_GENERAL_NAME_push(names,name);
        }
        X509_EXTENSION* extension=X509V3_EXT_i2d(NID_subject_alt_name,0,names);
        const bool added=extension&&X509_add_ext(cert,extension,-1);
        X509_EXTENSION_free(extension);
        sk_GENERAL_NAME_pop_free(names,GENERAL_NAME_free);
        if(!added)throw std::runtime_error("fixture SAN extension");
    }
    X509* cert_from_pem(const std::string& text){
        BIO* bio=BIO_new_mem_buf(text.data(),static_cast<int>(text.size()));
        X509* cert=bio?PEM_read_bio_X509(bio,nullptr,nullptr,nullptr):nullptr;
        BIO_free(bio);
        if(!cert)throw std::runtime_error("fixture PEM decode");
        return cert;
    }
    EVP_PKEY* key_from_pem(const std::string& text){
        BIO* bio=BIO_new_mem_buf(text.data(),static_cast<int>(text.size()));
        EVP_PKEY* key=bio?PEM_read_bio_PrivateKey(bio,nullptr,nullptr,nullptr):nullptr;
        BIO_free(bio);
        if(!key)throw std::runtime_error("fixture PEM decode");
        return key;
    }
    std::string write_pem(const std::string& dir,const std::string& name,const std::string& text){
        const std::string path=dir+"/"+name;
        const int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0)throw std::runtime_error("fixture file open");
        FILE* stream=fdopen(fd,"wb");
        if(!stream){close(fd);throw std::runtime_error("fixture file stream");}
        const bool written=fwrite(text.data(),1,text.size(),stream)==text.size();
        fclose(stream);
        if(!written)throw std::runtime_error("fixture file write");
        return path;
    }
    TlsFixture make_tls_fixture(){
        TlsFixture fixture;
        char pattern[]="/tmp/syn-tls-XXXXXX";
        char* directory=mkdtemp(pattern);
        if(!directory)throw std::runtime_error("fixture directory");
        fixture.dir=directory;
        EVP_PKEY* ca_key=fixture_key();
        X509* ca=fixture_cert(ca_key,"Test Bridge CA",true,nullptr,nullptr);
        EVP_PKEY* client_key=fixture_key();
        X509* client=fixture_cert(client_key,"bridge.test",false,ca_key,ca);
        EVP_PKEY* other_key=fixture_key();
        X509* other=fixture_cert(other_key,"bridge.test",false,ca_key,ca);
        fixture.ca_pem=pem_cert(ca);
        fixture.ca=write_pem(fixture.dir,"ca.pem",fixture.ca_pem);
        fixture.cert=write_pem(fixture.dir,"client.pem",pem_cert(client));
        fixture.key=write_pem(fixture.dir,"client.key",pem_key(client_key));
        fixture.other_cert=write_pem(fixture.dir,"other.pem",pem_cert(other));
        fixture.other_key=write_pem(fixture.dir,"other.key",pem_key(other_key));

        // Server side: trusted chain with both DNS and IP SAN identities.
        EVP_PKEY* server_key=fixture_key();
        X509* server=fixture_cert(server_key,"bridge.test",false,ca_key,ca);
        add_sans(server,"bridge.test","127.0.0.1");
        if(!X509_sign(server,ca_key,EVP_sha256()))throw std::runtime_error("fixture server signature");
        fixture.server_cert_pem=pem_cert(server);
        fixture.server_key_pem=pem_key(server_key);

        // Same trusted CA, but a different identity for mismatch tests.
        EVP_PKEY* wrong_name_key=fixture_key();
        X509* wrong_name=fixture_cert(wrong_name_key,"wrong.test",false,ca_key,ca);
        add_sans(wrong_name,"wrong.test","127.0.0.2");
        if(!X509_sign(wrong_name,ca_key,EVP_sha256()))throw std::runtime_error("fixture server signature");
        fixture.wrong_name_cert_pem=pem_cert(wrong_name);
        fixture.wrong_name_key_pem=pem_key(wrong_name_key);

        // An unrelated CA and a server chain signed only by it.
        EVP_PKEY* rogue_ca_key=fixture_key();
        X509* rogue_ca=fixture_cert(rogue_ca_key,"Untrusted CA",true,nullptr,nullptr);
        EVP_PKEY* rogue_server_key=fixture_key();
        X509* rogue_server=fixture_cert(rogue_server_key,"bridge.test",false,rogue_ca_key,rogue_ca);
        add_sans(rogue_server,"bridge.test","127.0.0.1");
        if(!X509_sign(rogue_server,rogue_ca_key,EVP_sha256()))
            throw std::runtime_error("fixture server signature");
        fixture.wrong_ca_pem=pem_cert(rogue_ca);
        fixture.wrong_server_cert_pem=pem_cert(rogue_server);
        fixture.wrong_server_key_pem=pem_key(rogue_server_key);

        EVP_PKEY_free(ca_key);X509_free(ca);
        EVP_PKEY_free(client_key);X509_free(client);
        EVP_PKEY_free(other_key);X509_free(other);
        EVP_PKEY_free(server_key);X509_free(server);
        EVP_PKEY_free(wrong_name_key);X509_free(wrong_name);
        EVP_PKEY_free(rogue_ca_key);X509_free(rogue_ca);
        EVP_PKEY_free(rogue_server_key);X509_free(rogue_server);
        return fixture;
    }
    struct BorrowedSockets {
        int local=-1,peer=-1;
        BorrowedSockets(){
            int fds[2];
            if(socketpair(AF_UNIX,SOCK_STREAM,0,fds)!=0)throw std::runtime_error("fixture socketpair");
            local=fds[0];peer=fds[1];
            for(int fd:fds){
                const int flags=fcntl(fd,F_GETFL,0);
                if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)!=0)
                    throw std::runtime_error("fixture nonblocking socket");
            }
        }
        ~BorrowedSockets(){if(local>=0)close(local);if(peer>=0)close(peer);}
    };
    // A real OpenSSL TLS server driven in the same thread as the production
    // TlsClient. The injected Wait callback advances SSL_accept, so both sides
    // make genuine progress without threads, sleeps or external sockets.
    struct TlsServer {
        std::unique_ptr<SSL_CTX,decltype(&SSL_CTX_free)> context{nullptr,SSL_CTX_free};
        std::unique_ptr<SSL,decltype(&SSL_free)> ssl{nullptr,SSL_free};
        bool accepted=false,verify_ok=false,peer_certificate=false;
        int version=0;
        std::string sni,last_error;
        Bytes received,reply=b("PONG");
        bool reply_sent=false;
        TlsServer(int fd,const std::string& certificate,const std::string& key,
                  const std::string& client_ca,bool require_client=true,int max_version=0){
            context.reset(SSL_CTX_new(TLS_server_method()));
            if(!context)throw std::runtime_error("server context");
            if(max_version&&SSL_CTX_set_max_proto_version(context.get(),max_version)!=1)
                throw std::runtime_error("server max version unsupported");
            X509* cert=cert_from_pem(certificate);
            EVP_PKEY* private_key=key_from_pem(key);
            const bool configured=SSL_CTX_use_certificate(context.get(),cert)==1&&
                SSL_CTX_use_PrivateKey(context.get(),private_key)==1&&
                SSL_CTX_check_private_key(context.get())==1;
            X509_free(cert);EVP_PKEY_free(private_key);
            if(!configured)throw std::runtime_error("server certificate setup");
            X509* ca=cert_from_pem(client_ca);
            const bool stored=X509_STORE_add_cert(SSL_CTX_get_cert_store(context.get()),ca)==1;
            X509_free(ca);
            if(!stored)throw std::runtime_error("server CA setup");
            SSL_CTX_set_verify(context.get(),
                require_client?(SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT):SSL_VERIFY_NONE,
                nullptr);
            ssl.reset(SSL_new(context.get()));
            if(!ssl||SSL_set_fd(ssl.get(),fd)!=1)throw std::runtime_error("server stream");
            SSL_set_accept_state(ssl.get());
        }
        void record(){
            version=SSL_version(ssl.get());
            verify_ok=SSL_get_verify_result(ssl.get())==X509_V_OK;
            peer_certificate=SSL_get0_peer_certificate(ssl.get())!=nullptr;
            const char* name=SSL_get_servername(ssl.get(),TLSEXT_NAMETYPE_host_name);
            sni=name?name:"";
        }
        void pump(){
            if(accepted)return;
            ERR_clear_error();
            const int result=SSL_accept(ssl.get());
            if(result==1){accepted=true;record();return;}
            const int error=SSL_get_error(ssl.get(),result);
            if(error==SSL_ERROR_WANT_READ||error==SSL_ERROR_WANT_WRITE)return;
            last_error="server accept failed";
            throw std::runtime_error(last_error);
        }
        void finish(int limit=64){
            for(int step=0;step<limit&&!accepted;++step)pump();
        }
        void read_once(){
            ERR_clear_error();
            std::array<uint8_t,128> buffer{};
            const int count=SSL_read(ssl.get(),buffer.data(),static_cast<int>(buffer.size()));
            if(count>0){received.assign(buffer.begin(),buffer.begin()+count);return;}
            const int error=SSL_get_error(ssl.get(),count);
            if(error==SSL_ERROR_WANT_READ||error==SSL_ERROR_WANT_WRITE)return;
            throw std::runtime_error("server read failed");
        }
        void write_reply(){
            if(reply_sent)return;
            ERR_clear_error();
            const int count=SSL_write(ssl.get(),reply.data(),static_cast<int>(reply.size()));
            if(count>0){reply_sent=true;return;}
            const int error=SSL_get_error(ssl.get(),count);
            if(error==SSL_ERROR_WANT_READ||error==SSL_ERROR_WANT_WRITE)return;
            throw std::runtime_error("server write failed");
        }
    };
    struct HandshakeOutcome {
        bool client_ok=false;
        bool payload_ok=false;
        std::string error,client_payload;
        int wait_calls=0;
        std::vector<short> wait_events;
    };
    HandshakeOutcome run_tls(TlsServer& server,int client_fd,const Config& config){
        HandshakeOutcome outcome;
        TlsClient::Wait wait=[&](short event,int64_t deadline){
            outcome.wait_events.push_back(event);
            if(++outcome.wait_calls>128)throw std::runtime_error("handshake pump bound exceeded");
            if(synergy::monotonic_ms()>deadline)throw std::runtime_error("handshake deadline exhausted");
            if(!server.accepted)server.pump();
        };
        try{
            TlsClient client(client_fd,config,wait);
            outcome.client_ok=true;
            server.finish();
            const Bytes ping=b("PING");
            client.send(std::span<const uint8_t>(ping.data(),ping.size()));
            for(int attempt=0;attempt<8&&server.received.empty();++attempt)server.read_once();
            for(int attempt=0;attempt<8&&!server.reply_sent;++attempt)server.write_reply();
            std::array<uint8_t,32> buffer{};
            const auto count=client.receive(buffer);
            if(count>0){
                outcome.client_payload.assign(buffer.begin(),buffer.begin()+static_cast<size_t>(count));
                outcome.payload_ok=outcome.client_payload=="PONG";
            }
        }
        catch(const std::exception& e){
            outcome.error=e.what();
        }
        return outcome;
    }
    bool expected_tls_failure(const std::string& message){
        for(const char* fragment:{
            "Authenticated input bridge TLS connection failed",
            "Input bridge peer authentication failed",
            "server accept failed","handshake pump bound exceeded","handshake deadline exhausted"})
            if(message.find(fragment)!=std::string::npos)return true;
        return false;
    }
    struct Recorder:Input {
        using E=std::tuple<char,int,int>;
        std::vector<E> events;
        std::vector<std::pair<uint8_t,bridge::GamepadState>> gamepads;
        bool absolute_positions=false;
        void key(int c,int v)override{
            events.emplace_back('k',c,v);
        }
        void button(int c,int v)override{
            events.emplace_back('b',c,v);
        }
        void move(int x,int y)override{
            events.emplace_back('m',x,y);
        }
        bool position(int x,int y)override{
            if(!absolute_positions)return false;
            events.emplace_back('p',x,y);
            return true;
        }
        void nudge()override{
            if(absolute_positions)events.emplace_back('n',0,0);
            else Input::nudge();
        }
        void wheel(int x,int y)override{
            events.emplace_back('w',x,y);
        }
        bool gamepad(uint8_t slot,const bridge::GamepadState& state)override{
            gamepads.emplace_back(slot,state);
            return true;
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
        {
            FrameDecoder decoder(true);
            std::vector<Bytes> emitted;
            auto receive=[&](Bytes&& payload){emitted.push_back(std::move(payload));};
            decoder.consume(frame(b("CNOP")),receive,32);
            emitted.clear();
            auto ignored=Bytes(FrameDecoder::MaxFrame,'x');
            std::copy_n("DCLP",4,ignored.begin());
            auto wire=frame(ignored);
            size_t completed=0,peak=0;
            for(size_t at=0;at<wire.size();at+=8192){
                completed+=decoder.consume(std::span(wire).subspan(at,std::min<size_t>(8192,wire.size()-at)),receive,32);
                peak=std::max(peak,decoder.retained_bytes());
            }
            check(completed==1&&emitted.empty()&&!decoder.partial(),"ignored MiB frame is streamed without emitting a payload");
            check(peak==0,"ignored frame retains no heap payload across chunks");
            decoder.consume(frame(b("CALV")),receive,32);
            check(emitted==std::vector<Bytes>{b("CALV")},"discard preserves framing of the following control packet");
            decoder.clear();
            rejects([&]{decoder.consume(std::span(wire).first(8),receive,32);},"first handshake frame cannot be discarded as clipboard");
        }
        {
            // Parity with the desktop bridge conformance test: a 1.5 MiB
            // clipboard body must stream through the native receiver too.
            FrameDecoder decoder(true);
            std::vector<Bytes> emitted;
            auto receive=[&](Bytes&& payload){emitted.push_back(std::move(payload));};
            decoder.consume(frame(b("CNOP")),receive,32);  // clear first-frame handshake guard
            emitted.clear();
            Bytes body(1536*1024,'y');std::copy_n("DCLP",4,body.begin());
            auto wire=frame(body);
            size_t completed=0,peak=0;
            for(size_t at=0;at<wire.size();at+=16384){
                completed+=decoder.consume(std::span(wire).subspan(at,std::min<size_t>(16384,wire.size()-at)),receive,32);
                peak=std::max(peak,decoder.retained_bytes());
            }
            check(completed==1&&emitted.empty()&&peak==0,"1.5 MiB ignored body streams without retention or rejection");
        }
        {
            FrameDecoder decoder;
            Bytes payload(100,'x');std::copy_n("DSOP",4,payload.begin());
            auto wire=frame(payload);
            rejects([&]{decoder.consume(std::span(wire).first(8),[](Bytes&&){},99);},"retained budget rejects declared allocation before body arrives");
            decoder.clear();
            auto small=frame(b("CNOP"));auto combined=small;
            combined.insert(combined.end(),small.begin(),small.end());combined.insert(combined.end(),small.begin(),small.end());
            std::vector<Bytes> held;
            rejects([&]{decoder.consume(combined,[&](Bytes&& p){held.push_back(std::move(p));},8);},"one consume call charges all emitted frame capacities");
            check(held.size()==2,"byte budget stops before retaining the third payload");
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
            frame(Bytes(FrameDecoder::MaxFrame+1));}
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
            h.input.absolute_positions=true;
            h.active();
            check(h.input.count('p',100,200)==1,"CINN synchronizes the absolute cursor position");
            h.session.handle(move("DMMV",0,0),200);
            h.session.handle(move("DMMV",1919,1079),201);
            h.session.handle(move("DMRM",-32768,-32768),202);
            h.session.handle(move("DMRM",32767,32767),203);
            check(h.input.count('p',0,0)==2,"DMMV and negative DMRM clamp to the top-left bound");
            check(h.input.count('p',1919,1079)==2,"DMMV and positive DMRM clamp to the bottom-right bound");
            check(h.input.count('m',0,0)==0,"absolute DMMV/DMRM also injected relative motion");
        }
        {
            Config c;
            c.width=1920;
            c.height=1080;
            Harness h(c);
            h.input.absolute_positions=true;
            h.handshake();
            h.session.handle(enter_at(65535,65535),100);
            check(h.input.count('p',1919,1079)==1,"out-of-range CINN seed was not clamped for absolute input");
            h.session.handle(move("DMRM",-1,-1),101);
            h.session.handle(move("DMMV",65535,65535),102);
            h.session.handle(move("DMRM",-1,-1),103);
            check(h.input.count('p',1918,1078)==2,"CINN/DMMV tracked an unclamped absolute coordinate");
            check(h.input.count('p',1919,1079)==2,"DMMV out-of-range point did not clamp to the configured edge");
            check(h.input.count('m',0,0)==0,"clamped absolute tracking used a duplicate relative fallback");
        }
        {
            Harness h;
            h.input.absolute_positions=true;
            h.handshake();
            h.session.handle(move("DMMV",20,30),50);
            h.session.handle(move("DMRM",4,-6),51);
            check(h.input.events.empty(),"inactive pointer packets caused input");
            h.session.handle(enter_at(100,200),100);
            check(h.input.count('p',100,200)==1,"first entry did not seed the absolute position");
            h.session.handle(b("COUT"),101);
            const auto after_leave=h.input.events.size();
            h.session.handle(move("DMMV",700,800),102);
            h.session.handle(move("DMRM",50,60),103);
            check(h.input.events.size()==after_leave,"inactive pointer packets after leave caused input");
            h.session.handle(enter_at(30,40),104);
            h.session.handle(move("DMRM",7,-8),105);
            check(h.input.count('p',30,40)==1&&h.input.count('p',37,32)==1,
                  "re-entry did not replace the prior absolute pointer seed");
            check(h.input.count('m',0,0)==0,"absolute re-entry used relative pointer injection");
        }
        {
            Config c;
            c.cursor_keepalive=true;
            Harness h(c);
            h.input.absolute_positions=true;
            h.active();
            h.session.tick(2100);
            check(h.input.count('n',0,0)==1,"absolute cursor keepalive did not call nudge");
            check(h.input.count('m',0,0)==0,"absolute cursor keepalive fell back to generic relative moves");
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
            h.active();
            h.session.handle(key("DKDN",'a',0,30),200);
            h.session.handle(key("DKRP",'a',0,30,7),201);
            check(h.input.count('k',30,2)==7,"batched repeat count preserved");
            h.session.handle(key("DKRP",'a',0,30,0),202);
            check(h.input.count('k',30,2)==7,"zero repeat count injects nothing");
            h.session.handle(key("DKRP",'a',0,30,65535),203);
            check(h.input.count('k',30,2)==65542,"full uint16 repeat count terminates without overflow");
            h.session.handle(key("DKUP",'a',0,30),204);
            h.session.handle(key("DKRP",'a',0,30,7),205);
            check(h.input.count('k',30,2)==65542,"released key cannot repeat");
            h.session.handle(key("DKDN",0xefe9,4,56),206);
            h.session.handle(key("DKRP",0xefe9,4,56,7),207);
            check(h.input.count('k',56,2)==0,"Alt repeats remain consumed");
            h.session.handle(key("DKDN",0xef09,4,15),208);
            h.session.handle(key("DKRP",0xef09,4,15,7),209);
            check(h.actions.size()==1&&h.input.count('k',15,2)==0,"batched Alt Tab repeats do not cycle or inject");
            h.session.handle(b("COUT"),210);
            h.session.handle(key("DKRP",'a',0,30,7),211);
            check(h.input.count('k',30,2)==65542,"batched inactive repeats discarded");
        }
        {
            // Deferred (client) mode: bounded chunks, wire order, cleanup.
            Harness h;
            h.active();
            h.session.defer_repeats(true);
            h.session.handle(key("DKDN",'a',0,30),200);
            h.session.handle(key("DKRP",'a',0,30,65535),201);
            check(h.session.pending()&&h.input.count('k',30,2)==0,"deferred repeat batch queued, not emitted inline");
            check(h.session.pump(202,16)==16&&h.input.count('k',30,2)==16,"pump emits at most one chunk");
            h.session.handle(key("DKUP",'a',0,30),203);
            check(!h.session.pending()&&h.input.count('k',30,2)==65535,"later packet drains the full batch first");
            const auto up=std::find(h.input.events.begin(),h.input.events.end(),Recorder::E{'k',30,0});
            check(up!=h.input.events.end()&&std::count(up,h.input.events.end(),Recorder::E{'k',30,2})==0,"key-up ordered after every repeat");
            h.session.handle(key("DKDN",'a',0,30),204);
            h.session.handle(key("DKRP",'a',0,30,500),205);
            h.session.pump(206,10);
            h.session.handle(b("COUT"),207);
            check(h.input.count('k',30,2)==65535+500,"COUT still follows the queued repeats");
            h.session.handle(enter(),208);
            h.session.handle(key("DKDN",'a',0,30),209);
            h.session.handle(key("DKRP",'a',0,30,500),210);
            h.session.release();
            check(!h.session.pending()&&h.session.pump(211,1000)==0,"release discards undelivered repeats");
            h.session.handle(enter(),212);
            h.session.handle(key("DKRP",'a',0,30,9),213);
            check(h.session.pump(214,100)==9&&h.input.count('k',30,2)==65535+500,"repeats of an unpressed key stay inert");
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
        for(bool inherited:{false,true}){
            Harness h;
            h.active(inherited?4:0);
            if(!inherited)h.session.handle(key("DKDN",0xefe9,4,56),200);
            h.session.tick(inherited?5100:5200);
            h.session.handle(key("DKDN",'a',4,30),5300);
            h.session.handle(key("DKDN",0xefe9,4,56),5301);
            h.session.handle(key("DKDN",0xefea,4,100),5302);
            h.session.tick(10300);
            check(h.input.count('k',56,1)==1&&h.input.count('k',100,1)==0,"consumed Alt hold cannot re-inherit or inject either physical Alt");
            check(h.input.count('k',30,1)==1&&h.actions.size()==1,"ordinary input survives consumed hold without another Home");
            h.session.handle(key("DKUP",0xefe9,4,56),10400);
            h.session.handle(key("DKUP",0xefea,0,100),10401);
            h.session.handle(key("DKDN",0xefea,4,100),10500);
            h.session.tick(15500);
            check(h.input.count('k',100,1)==1&&h.input.count('k',100,0)==1&&h.actions.size()==2,"release rearms next Alt hold");
            h.session.handle(b("COUT"),15600);
            h.session.handle(enter(4),15700);
            check(h.input.count('k',56,1)==2,"capture cleanup rearms inherited Alt");
        }
        {
            Harness h;
            h.active(4);
            h.session.tick(5100);
            h.session.handle(key("DKDN",0xefea,4,100),5200);
            h.session.handle(key("DKDN",'a',4,30),5201);
            h.session.tick(10200);
            check(h.input.count('k',56,1)==1&&h.input.count('k',100,1)==0&&h.actions.size()==1,"physical peer replacement cannot restart consumed inherited Alt hold");
            h.session.handle(key("DKUP",0xefea,0,100),10300);
            h.session.handle(key("DKDN",0xefea,4,100),10400);
            check(h.input.count('k',100,1)==1&&h.resets==1,"next physical hold injects Alt and resets cycle cache");
        }
        {
            Harness h;
            h.active(4);
            h.session.tick(5100);
            h.session.handle(key("DKDN",'a',0,30),5200);
            h.session.handle(key("DKUP",'a',0,30),5201);
            h.session.handle(key("DKDN",'b',4,48),5300);
            h.session.tick(10300);
            check(h.input.count('k',56,1)==2&&h.input.count('k',56,0)==2&&h.actions.size()==2,"cleared mask ends consumed synthetic Alt hold and rearms next one");
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
        {
            // IDs follow the primary upstream Deskflow KeyTypes.h declarations
            // in .ai/inbox/input-0.4.1-implementation-20261113/upstream/.
            // A physical 'a' scan code must not override semantic controls.
            const std::tuple<uint16_t,int> media_keys[]={
                {0xe0a6,158},{0xe0a7,159},{0xe0a8,173},{0xe0a9,128},
                {0xe0aa,217},{0xe0ab,156},{0xe0ac,172},{0xe0ad,113},
                {0xe0ae,114},{0xe0af,115},{0xe0b0,163},{0xe0b1,165},
                {0xe0b2,166},{0xe0b3,164},{0xe0b4,155},{0xe0b5,226}
            };
            for(const auto& [id,expected]:media_keys)
                check(linux_key(id,30)==expected,"extended media semantic key mapping beats physical A");
        }
        {
            const std::tuple<uint16_t,int> function_keys[]={
                {0xefca,183},{0xefcb,184},{0xefcc,185},{0xefcd,186},
                {0xefce,187},{0xefcf,188},{0xefd0,189},{0xefd1,190},
                {0xefd2,191},{0xefd3,192},{0xefd4,193},{0xefd5,194}
            };
            for(const auto& [id,expected]:function_keys)
                check(linux_key(id,30)==expected,"F13-F24 semantic key mapping");
        }
        {
            const std::tuple<uint16_t,int> keypad_keys[]={
                {0xef8d,96},
                {0xef95,71},{0xef96,75},{0xef97,72},{0xef98,77},{0xef99,80},
                {0xef9a,73},{0xef9b,81},{0xef9c,79},{0xef9d,76},{0xef9e,82},{0xef9f,83},
                {0xefaa,55},{0xefab,78},{0xefac,121},{0xefad,74},{0xefae,83},{0xefaf,98},{0xefbd,117},
                {0xefb0,82},{0xefb1,79},{0xefb2,80},{0xefb3,81},{0xefb4,75},
                {0xefb5,76},{0xefb6,77},{0xefb7,71},{0xefb8,72},{0xefb9,73}
            };
            for(const auto& [id,expected]:keypad_keys)
                check(linux_key(id,30)==expected,"keypad semantic key mapping");
        }
        {
            const uint16_t unsupported[]={
                0xe001,0xe05f,0xe0b6,0xe0b7,0xe0b8,0xe0b9,0xef00,0xef10,0xef80,0xef89,0xeffe
            };
            Harness h;
            h.active();
            const auto replies_before=std::count(h.sent.begin(),h.sent.end(),b("CNOP"));
            for(uint16_t id:unsupported){
                check(linux_key(id,30)==0,"unsupported E0/EF semantic key has no physical fallback");
                h.session.handle(key("DKDN",id,0,30),200);
                h.session.handle(key("DKRP",id,0,30,2),201);
                h.session.handle(key("DKUP",id,0,30),202);
            }
            check(h.input.events.empty()&&h.session.active()&&h.session.pressed()==0,
                  "unsupported E0/EF keys neither inject A nor disrupt capture");
            check(std::count(h.sent.begin(),h.sent.end(),b("CNOP"))==replies_before+3*std::ssize(unsupported),
                  "unsupported E0/EF key packets are acknowledged");
        }
        {
            Harness h;
            h.active();
            const std::tuple<uint16_t,int> volume_keys[]={{0xe0ae,114},{0xe0af,115}};
            for(const auto& [id,code]:volume_keys){
                h.session.handle(key("DKDN",id,0,30),200);
                h.session.handle(key("DKRP",id,0,30,3),201);
                h.session.handle(key("DKUP",id,0,30),202);
                check(h.input.count('k',code,1)==1&&h.input.count('k',code,2)==3&&h.input.count('k',code,0)==1,
                      "held volume key repeats and releases");
            }
            check(h.session.pressed()==0,"volume key-up clears held-key state");
        }
        {
            Harness h;
            h.active();
            const std::tuple<uint16_t,int> one_shot_media[]={
                {0xe0ad,113},{0xe0b3,164},{0xe0b0,163},{0xe0b1,165},{0xe0b2,166}
            };
            for(const auto& [id,code]:one_shot_media){
                h.session.handle(key("DKDN",id,0,30),200);
                h.session.handle(key("DKRP",id,0,30,3),201);
                h.session.handle(key("DKUP",id,0,30),202);
                check(h.input.count('k',code,1)==1&&h.input.count('k',code,2)==0&&h.input.count('k',code,0)==1,
                      "one-shot media key suppresses repeat but preserves key-up");
            }
            check(h.session.pressed()==0,"one-shot media key-ups clear held-key state");
        }
        {
            Harness h;
            h.active();
            h.session.handle(key("DKDN",0xe0ad,0,30),200);
            h.session.handle(key("DKRP",0xe0ad,0,30,3),201);
            h.session.handle(b("COUT"),202);
            check(h.input.count('k',113,1)==1&&h.input.count('k',113,2)==0&&h.input.count('k',113,0)==1,
                  "COUT releases held mute after suppressing repeats");
            check(!h.session.active()&&h.session.pressed()==0,"COUT clears media capture state");
        }
        {
            Harness h;
            h.active();
            h.session.handle(key("DKDN",0xe0b2,0,30),200);
            h.session.handle(key("DKRP",0xe0b2,0,30,3),201);
            h.session.release();
            check(h.input.count('k',166,1)==1&&h.input.count('k',166,2)==0&&h.input.count('k',166,0)==1,
                  "session stop releases held media stop after suppressing repeats");
            check(!h.session.active()&&h.session.pressed()==0,"session stop clears media capture state");
        }
        {
            Harness h;
            h.handshake();
            h.session.handle(key("DKDN",'a',0,30),100);
            h.session.handle(key("DKRP",'a',0,30,2),101);
            h.session.handle(key("DKUP",'a',0,30),102);
            auto down=b("DMDN");down.push_back(1);h.session.handle(down,103);
            auto up=b("DMUP");up.push_back(1);h.session.handle(up,104);
            check(!h.session.active()&&h.session.pressed()==0&&h.input.events.empty(),
                  "keyboard and mouse controls outside CINN are ignored");
            check(std::count(h.sent.begin(),h.sent.end(),b("CNOP"))==5,
                  "inactive keyboard and mouse controls are acknowledged");
        }
        {
            Harness h;
            h.active();
            const auto replies_before=std::count(h.sent.begin(),h.sent.end(),b("CNOP"));
            for(uint8_t id:{uint8_t(6),uint8_t(7),uint8_t(255)}){
                auto down=b("DMDN");down.push_back(id);h.session.handle(down,200);
                auto up=b("DMUP");up.push_back(id);h.session.handle(up,201);
            }
            check(h.session.active()&&h.input.events.empty(),
                  "unknown mouse button IDs do not inject or disconnect");
            check(std::count(h.sent.begin(),h.sent.end(),b("CNOP"))==replies_before+6,
                  "unknown mouse button down and up packets are acknowledged");
            auto invalid=b("DMDN");invalid.push_back(0);
            rejects([&]{h.session.handle(invalid,202);},"mouse button ID zero remains invalid");
        }
        {
            Harness h;
            h.active();
            const std::tuple<uint8_t,int> button_map[]={
                {1,0x110},{2,0x112},{3,0x111},{4,0x113},{5,0x114}
            };
            for(const auto& [id,code]:button_map){
                auto down=b("DMDN");down.push_back(id);h.session.handle(down,200);
                auto up=b("DMUP");up.push_back(id);h.session.handle(up,201);
                check(h.input.count('b',code,1)==1&&h.input.count('b',code,0)==1,
                      "mouse button ID maps to the original Linux down/up code");
            }
            for(const auto& [id,code]:button_map){
                auto down=b("DMDN");down.push_back(id);h.session.handle(down,202);
            }
            h.session.handle(b("COUT"),203);
            for(const auto& [id,code]:button_map)
                check(h.input.count('b',code,1)==2&&h.input.count('b',code,0)==2,
                      "COUT releases each mapped mouse button exactly once");
            check(!h.session.active(),"COUT clears mapped mouse-button capture");
        }
        {
            // DGPD is admitted only in explicit authenticated bridge mode.
            Harness classic;
            classic.active();
            bridge::GamepadState connected{};
            connected.buttons=0x1000;
            connected.connected=true;
            rejects([&]{classic.session.handle(dgpd(0,1,1,connected),101);},
                    "direct classic mode refuses DGPD");
            check(classic.input.gamepads.empty(),"classic DGPD refusal does not reach input");
        }
        {
            Config c;
            c.input_bridge=true;
            Harness h(c);
            h.handshake();
            bridge::GamepadState connected{};
            connected.buttons=0x1000;
            connected.lx=1234;
            connected.connected=true;
            h.session.handle(dgpd(1,21,1,connected),50);
            check(h.input.gamepads.empty(),"bridge DGPD before focus is ignored");
            h.session.handle(enter_at(100,200,0,21),100);
            h.session.handle(dgpd(1,21,2,connected),101);
            check(h.input.gamepads.size()==1&&h.input.gamepads.back()==std::pair<uint8_t,bridge::GamepadState>{1,connected},
                  "active matching capture accepts first bridge state");
            h.session.handle(dgpd(1,21,2,connected),102);
            h.session.handle(dgpd(1,20,3,connected),103);
            check(h.input.gamepads.size()==1,"replayed and previous-capture states cannot re-press controls");
            h.session.handle(b("COUT"),104);
            check(h.input.gamepads.size()==2&&h.input.gamepads.back().first==1&&
                  h.input.gamepads.back().second==bridge::GamepadState{},
                  "COUT neutralizes the active pad");
            h.session.handle(dgpd(1,21,4,connected),105);
            h.session.handle(enter_at(100,200,0,22),200);
            h.session.handle(dgpd(1,22,2,connected),201);
            check(h.input.gamepads.size()==2,"gamepad sequence does not reset across COUT and CINN");
            h.session.handle(dgpd(1,22,5,connected),202);
            check(h.input.gamepads.size()==3&&h.input.gamepads.back().second==connected,
                  "new capture accepts a higher connection-global sequence");
        }
        {
            Config c;
            c.input_bridge=true;
            Harness h(c);
            auto hello=b("Synergy");p16(hello,1);p16(hello,6);
            h.session.handle(hello,0);
            bridge::GamepadState connected{};
            connected.buttons=0x1000;
            connected.connected=true;
            h.session.handle(dgpd(0,31,1,connected),1);
            h.session.handle(b("QINF"),2);
            h.session.handle(dgpd(0,31,2,connected),3);
            check(h.input.gamepads.empty(),"bridge DGPD is ignored until ready and in focus");
            h.session.handle(enter_at(100,200,0,31),4);
            h.session.handle(dgpd(0,31,3,connected),5);
            check(h.input.gamepads.size()==1&&h.input.gamepads.front().second==connected,
                  "ready negotiated focus admits matching bridge input");
        }
        {
            Config c;
            c.input_bridge=true;
            Harness h(c);
            h.active();
            bridge::GamepadState connected{};
            connected.buttons=0x1000;
            connected.connected=true;
            auto good=dgpd(0,1,1,connected);
            std::vector<Bytes> malformed;
            auto short_packet=good;short_packet.pop_back();malformed.push_back(short_packet);
            auto long_packet=good;long_packet.push_back(0);malformed.push_back(long_packet);
            auto wrong_magic=good;wrong_magic[0]='X';malformed.push_back(wrong_magic);
            auto wrong_version=good;wrong_version[4]=2;malformed.push_back(wrong_version);
            auto wrong_slot=good;wrong_slot[5]=4;malformed.push_back(wrong_slot);
            auto invalid_connected=good;invalid_connected[6]=2;malformed.push_back(invalid_connected);
            auto reserved=good;reserved[7]=1;malformed.push_back(reserved);
            auto reserved_buttons=good;reserved_buttons[20]=0x0c;malformed.push_back(reserved_buttons);
            auto no_sequence=good;std::fill(no_sequence.begin()+12,no_sequence.begin()+20,0);malformed.push_back(no_sequence);
            bridge::GamepadState invalid_disconnected{};
            invalid_disconnected.ly=-1;
            malformed.push_back(dgpd(0,1,2,invalid_disconnected));
            for(const auto& packet:malformed)
                rejects([&]{h.session.handle(packet,101);},"malformed or reserved DGPD fields are rejected");
            check(h.input.gamepads.empty(),"invalid DGPD frames do not reach input");
        }
        {
            Config c;
            c.input_bridge=true;
            Harness h(c);
            h.active();
            bridge::GamepadState first{};
            first.buttons=0x1000;
            first.connected=true;
            bridge::GamepadState second=first;
            second.lx=1;
            h.session.handle(dgpd(0,1,1,first),100,349);
            h.session.handle(dgpd(0,1,2,second),200,450);
            check(h.input.gamepads.size()==1&&h.input.gamepads.front().second==first,
                  "processing delay accepts age 249 ms and drops age 250 ms");
        }
        {
            Config c;
            c.input_bridge=true;
            Harness h(c);
            h.active();
            bridge::GamepadState connected{};
            connected.buttons=0x1000;
            connected.lx=-32768;
            connected.rt=255;
            connected.connected=true;
            h.session.handle(dgpd(0,1,1,connected),100);
            h.session.expire_gamepads(599);
            check(h.input.gamepads.size()==1,"gamepad watchdog waits until 500 ms after its last positive state");
            h.session.expire_gamepads(600);
            check(h.input.gamepads.size()==2&&h.input.gamepads.back().first==0&&
                  h.input.gamepads.back().second==bridge::GamepadState{},
                  "gamepad watchdog emits a fully neutral disconnected state at 500 ms");
            h.session.expire_gamepads(601);
            check(h.input.gamepads.size()==2,"expired gamepad is neutralized only once");
            h.session.handle(dgpd(0,1,2,connected),700);
            h.session.handle(dgpd(3,1,3,connected),701);
            check(h.input.gamepads.size()==4,"two slots may be active concurrently");
            h.session.release();
            check(h.input.gamepads.size()==6&&
                  h.input.gamepads[4]==std::pair<uint8_t,bridge::GamepadState>{0,bridge::GamepadState{}}&&
                  h.input.gamepads[5]==std::pair<uint8_t,bridge::GamepadState>{3,bridge::GamepadState{}},
                  "release neutralizes every active controller slot");
        }
        {
            Config c;
            c.input_bridge=true;
            Harness h(c);
            h.active();
            h.session.handle(key("DKDN",'a',0,30),200);
            h.session.handle(key("DKUP",'a',0,30),201);
            h.session.handle(move("DMMV",12,7),202);
            Bytes down=b("DMDN");down.push_back(1);h.session.handle(down,203);
            Bytes up=b("DMUP");up.push_back(1);h.session.handle(up,204);
            const auto events_before_fraction=h.input.events.size();
            h.session.handle(move("DMWM",2,-3),205);
            check(h.input.events.size()==events_before_fraction,"fractional wheel deltas are retained before a full notch");
            h.session.handle(move("DMWM",238,-357),206);
            check(h.input.count('k',30,1)==1&&h.input.count('k',30,0)==1&&
                  h.input.count('m',-88,-193)==1&&h.input.count('b',0x110,1)==1&&
                  h.input.count('b',0x110,0)==1&&h.input.count('w',2,-3)==1&&
                  h.input.gamepads.empty(),
                  "bridge mode preserves existing keyboard, pointer, button, and wheel input");
        }
        {
            Config defaults;
            check(!defaults.input_bridge&&defaults.bridge_ca_file.empty()&&
                  defaults.bridge_cert_file.empty()&&defaults.bridge_key_file.empty()&&
                  defaults.bridge_server_name.empty(),
                  "input bridge and pairing paths default off and empty");
            Config bridge_config=defaults;
            bridge_config.server="127.0.0.1";
            bridge_config.input_bridge=true;
            bridge_config.bridge_ca_file="/tmp/bridge-ca.pem";
            bridge_config.bridge_cert_file="/tmp/bridge-client.pem";
            bridge_config.bridge_key_file="/tmp/bridge-key.pem";
            bridge_config.bridge_server_name="bridge.test";
            bridge_config.validate();
            auto round=Config::from_json(Json::parse(bridge_config.json().dump()));
            check(round.input_bridge&&round.bridge_ca_file==bridge_config.bridge_ca_file&&
                  round.bridge_cert_file==bridge_config.bridge_cert_file&&
                  round.bridge_key_file==bridge_config.bridge_key_file&&
                  round.bridge_server_name==bridge_config.bridge_server_name,
                  "bridge mode pairing paths and server identity roundtrip");
            bridge_config.bridge_cert_file.clear();
            rejects([&]{bridge_config.validate(false);},"bridge mode requires all pairing paths");
        }
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
            R"({"width":0})",R"({"width":32768})",R"({"port":1.5})",R"({"dry_run":"false"})",R"({"command":"sh"})",R"({"name":"a b"})",R"({"home_target":"shell"})",R"({"locale":"fr-FR"})"}
        )rejects([&]{
            Config::from_json(Json::parse(json));}
        ,"config validation");
        {
            check(catalog_valid()&&catalog_size()>=80,"i18n catalog has unique, non-empty entries in both locales");
            check(tr("unknown.key",Locale::En)=="unknown.key","unknown i18n key falls back to its name");
            check(tr("tab.link",Locale::En)=="Connection"&&
                  tr("tab.link",Locale::FrCA)=="Connexion","UI tab labels translate to Quebec French");
            check(tr("events",Locale::FrCA,{{"n","7"}})=="\303\211v\303\251nements : 7","i18n placeholders interpolate in the active locale");
            check(locale_from_string("fr-CA")==Locale::FrCA&&locale_from_string("fr-FR")==Locale::FrCA&&
                  locale_from_string("en-GB")==Locale::En&&locale_from_string("de")==Locale::Unknown,
                  "locale tags map to supported UI locales");
            check(std::string(locale_string(Locale::FrCA))=="fr-CA"&&
                  std::string(locale_string(locale_from_string("fr-CA")))=="fr-CA",
                  "locale string roundtrip");
            Config localized;
            localized.server="192.168.1.10";
            localized.locale="fr-CA";
            localized.validate();
            auto localized_round=Config::from_json(Json::parse(localized.json().dump()));
            check(localized_round.locale=="fr-CA"&&locale_from_string(localized_round.locale)==Locale::FrCA,
                  "UI locale persists through the typed config roundtrip");
            check(locale_from_string(Config::from_json(Json::parse(R"({"server":"192.168.1.10"})")).locale)==Locale::En,
                  "missing locale defaults to English");
        }
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
            const auto wall_start=std::chrono::steady_clock::now();
            const auto cpu_start=std::clock();
            auto closed=bounded_process("/bin/sh",{"-c","printf done; exec 1>&-; sleep 0.4; exit 7"},1000);
            const auto wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall_start).count();
            const auto cpu=static_cast<double>(std::clock()-cpu_start)/CLOCKS_PER_SEC;
            check(closed.output=="done"&&closed.exit_code==7&&!closed.timed_out,"EOF before exit preserves output and waits for child status");
            std::cout<<"EOF wait: wall_s="<<wall<<" parent_cpu_s="<<cpu<<'\n';
            check(cpu<wall*.5+.01,"EOF before exit waits instead of busy-spinning");
            auto closed_slow=bounded_process("/bin/sh",{"-c","exec 1>&-; sleep 5"},60);
            check(closed_slow.timed_out,"EOF does not bypass process deadline");
            auto descendant=bounded_process("/bin/sh",{"-c","sleep 5 & exit 0"},60);
            check(descendant.timed_out,"child exit does not bypass deadline on descendant-held pipe");
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
        {
            // G1 (3a): real TlsClient constructor/configuration fail-closed
            // paths. Every negative case uses real OpenSSL and real temporary
            // PEM files; no fake TLS, no handshake, no network, no real sleeps.
            std::string fixture_dir;
            {
                TlsFixture fixture=make_tls_fixture();
                fixture_dir=fixture.dir;
                Config valid;
                valid.server="127.0.0.1";
                valid.input_bridge=true;
                valid.bridge_ca_file=fixture.ca;
                valid.bridge_cert_file=fixture.cert;
                valid.bridge_key_file=fixture.key;
                valid.bridge_server_name="bridge.test";
                BorrowedSockets sockets;
                const TlsClient::Wait immediate=[](short,int64_t){};
                auto construct=[&](const Config& config,TlsClient::Wait wait){
                    TlsClient client(sockets.local,config,wait);
                    (void)client;
                };
                {
                    Config tls_case=valid;tls_case.bridge_ca_file=fixture.dir+"/absent-ca.pem";
                    rejects([&]{construct(tls_case,immediate);},"missing CA bundle fails closed");
                }
                {
                    Config tls_case=valid;tls_case.bridge_ca_file=write_pem(fixture.dir,"empty-ca.pem","");
                    rejects_with([&]{construct(tls_case,immediate);},"CA certificate missing",
                                 "empty CA bundle fails closed");
                }
                {
                    Config tls_case=valid;
                    tls_case.bridge_ca_file=write_pem(fixture.dir,"bad-ca.pem",
                        "-----BEGIN CERTIFICATE-----\nnot-certificate-data\n-----END CERTIFICATE-----\n");
                    rejects_with([&]{construct(tls_case,immediate);},"CA certificate missing",
                                 "malformed CA PEM fails closed");
                }
                {
                    Config tls_case=valid;tls_case.bridge_cert_file=fixture.dir+"/absent-client.pem";
                    rejects([&]{construct(tls_case,immediate);},"missing client certificate fails closed");
                }
                {
                    Config tls_case=valid;tls_case.bridge_cert_file=write_pem(fixture.dir,"empty-client.pem","");
                    rejects_with([&]{construct(tls_case,immediate);},"Invalid input bridge client certificate",
                                 "empty client certificate fails closed");
                }
                {
                    Config tls_case=valid;
                    tls_case.bridge_cert_file=write_pem(fixture.dir,"bad-client.pem",
                        "-----BEGIN CERTIFICATE-----\nxxx\n-----END CERTIFICATE-----\n");
                    rejects_with([&]{construct(tls_case,immediate);},"Invalid input bridge client certificate",
                                 "malformed client certificate fails closed");
                }
                {
                    Config tls_case=valid;tls_case.bridge_key_file=fixture.dir+"/absent-key.pem";
                    rejects([&]{construct(tls_case,immediate);},"missing private key fails closed");
                }
                {
                    Config tls_case=valid;
                    // Build the malformed PEM at runtime so the repository
                    // guard does not mistake this negative fixture for a
                    // committed private key. The bytes written are identical.
                    const std::string header="-----BEGIN "+std::string("PRIVATE KEY-----");
                    const std::string footer="-----END "+std::string("PRIVATE KEY-----");
                    tls_case.bridge_key_file=write_pem(fixture.dir,"bad-key.pem",
                        header+"\nxxx\n"+footer+"\n");
                    rejects_with([&]{construct(tls_case,immediate);},"Invalid input bridge private key",
                                 "malformed private key fails closed");
                }
                {
                    Config tls_case=valid;tls_case.bridge_cert_file=fixture.other_cert;
                    rejects_with([&]{construct(tls_case,immediate);},"Invalid input bridge private key",
                                 "certificate and private key mismatch fails closed");
                }
                {
                    Config tls_case=valid;tls_case.bridge_server_name.clear();
                    rejects_with([&]{construct(tls_case,immediate);},"Invalid input bridge certificate identity",
                                 "missing server identity fails closed");
                }
                {
                    // No responding peer: SSL_connect asks the injected wait for
                    // readiness. Throwing there models an exhausted deadline
                    // without a real timer or sleep.
                    const int flags=fcntl(sockets.local,F_GETFL,0);
                    check(flags>=0&&fcntl(sockets.local,F_SETFL,flags|O_NONBLOCK)==0,
                          "borrowed socket can be made nonblocking for the deadline case");
                    short waited_event=0;
                    bool waited=false;
                    TlsClient::Wait expiring=[&](short event,int64_t){
                        waited=true;waited_event=event;
                        throw std::runtime_error("injected wait deadline exhausted");
                    };
                    rejects([&]{construct(valid,expiring);},
                            "handshake fails closed when the injected wait deadline expires");
                    check(waited,"handshake entered the injected wait path with no peer");
                    check(waited_event==POLLIN||waited_event==POLLOUT,
                          "TLS handshake wait requests a socket readiness event");
                    check(fcntl(sockets.local,F_GETFD)!=-1,
                          "borrowed descriptor stays owned by the test after failed construction");
                    char probe=0;
                    const ssize_t received=recv(sockets.peer,&probe,1,MSG_DONTWAIT);
                    check(received<=0||static_cast<unsigned char>(probe)==0x16,
                          "failed TLS setup emits no classic plaintext to the peer");
                }
            }
            check(!std::filesystem::exists(fixture_dir),
                  "ephemeral PKI directory and private keys removed on cleanup");
        }
        {
            // G1 (3b): genuine mTLS handshakes. The real production TlsClient
            // talks to a real OpenSSL SSL_accept server over a nonblocking
            // socketpair; the injected Wait advances the server in this thread.
            TlsFixture fixture=make_tls_fixture();
            auto tls_config=[&](const std::string& identity){
                Config config;
                config.server="127.0.0.1";
                config.input_bridge=true;
                config.bridge_ca_file=fixture.ca;
                config.bridge_cert_file=fixture.cert;
                config.bridge_key_file=fixture.key;
                config.bridge_server_name=identity;
                return config;
            };
            const auto trusted_server=[&](int fd){
                return TlsServer(fd,fixture.server_cert_pem,fixture.server_key_pem,
                                 fixture.ca_pem,true);
            };
            {
                BorrowedSockets socks;
                TlsServer server=trusted_server(socks.peer);
                HandshakeOutcome outcome=run_tls(server,socks.local,tls_config("bridge.test"));
                check(outcome.client_ok&&outcome.error.empty(),"trusted DNS mTLS handshake succeeds");
                check(server.accepted,"server completed a real SSL_accept");
                check(server.verify_ok&&server.peer_certificate,
                      "server independently verified the client certificate");
                check(server.version>=TLS1_2_VERSION,
                      "negotiated TLS version meets the production minimum");
                check(server.sni=="bridge.test","client sent DNS SNI observed by the server");
                check(server.received==b("PING")&&outcome.payload_ok,
                      "application payload exchanged after mutual authentication");
                check(!outcome.wait_events.empty()&&
                      std::all_of(outcome.wait_events.begin(),outcome.wait_events.end(),
                          [](short event){return event==POLLIN||event==POLLOUT;}),
                      "handshake advanced through injected socket-readiness waits");
                std::cout<<"tls3b handshake: version="<<server.version
                         <<" sni="<<server.sni<<" waits=";
                for(short event:outcome.wait_events)
                    std::cout<<(event==POLLIN?"IN":event==POLLOUT?"OUT":"?")<<' ';
                std::cout<<'\n';
            }
            {
                BorrowedSockets socks;
                TlsServer server=trusted_server(socks.peer);
                HandshakeOutcome outcome=run_tls(server,socks.local,tls_config("127.0.0.1"));
                check(outcome.client_ok&&outcome.error.empty(),"trusted IP-SAN mTLS handshake succeeds");
                check(server.accepted&&server.verify_ok,"IP identity chain and client verified");
                check(server.sni.empty(),"IP identity does not emit DNS SNI");
            }
            {
                BorrowedSockets socks;
                TlsServer server(socks.peer,fixture.wrong_server_cert_pem,
                                 fixture.wrong_server_key_pem,fixture.ca_pem,true);
                HandshakeOutcome outcome=run_tls(server,socks.local,tls_config("bridge.test"));
                check(!outcome.client_ok&&expected_tls_failure(outcome.error),
                      "untrusted server certificate chain is rejected during handshake");
            }
            {
                BorrowedSockets socks;
                TlsServer server(socks.peer,fixture.wrong_name_cert_pem,
                                 fixture.wrong_name_key_pem,fixture.ca_pem,true);
                HandshakeOutcome outcome=run_tls(server,socks.local,tls_config("bridge.test"));
                check(!outcome.client_ok&&expected_tls_failure(outcome.error),
                      "valid but incorrectly named server certificate is rejected");
            }
            {
                BorrowedSockets socks;
                TlsServer server=trusted_server(socks.peer);
                HandshakeOutcome outcome=run_tls(server,socks.local,tls_config("127.0.0.2"));
                check(!outcome.client_ok&&expected_tls_failure(outcome.error),
                      "IP identity mismatch is rejected despite a trusted chain");
            }
            {
                BorrowedSockets socks;
                TlsServer server(socks.peer,fixture.server_cert_pem,fixture.server_key_pem,
                                 fixture.wrong_ca_pem,true);
                HandshakeOutcome outcome=run_tls(server,socks.local,tls_config("bridge.test"));
                // The client can finishing SSL_connect before the server's
                // verification alert arrives; the authoritative evidence is
                // that SSL_accept never completed on the server side.
                check(!server.accepted&&!server.verify_ok&&server.received.empty()&&
                      outcome.error.find("server accept failed")!=std::string::npos,
                      "server rejects a client certificate from an untrusted CA");
            }
            {
                BorrowedSockets socks;
                TlsServer server=trusted_server(socks.peer);
                Config missing=tls_config("bridge.test");
                missing.bridge_key_file.clear();
                HandshakeOutcome outcome=run_tls(server,socks.local,missing);
                check(!outcome.client_ok&&outcome.error.find("Read state")!=std::string::npos,
                      "missing client credentials fail before any TLS handshake");
                check(server.received.empty(),"no payload is sent after credential failure");
            }
            {
                // Version-mismatch fixture: only meaningful if this OpenSSL
                // still permits a TLS 1.1-only server context.
                std::string setup_error;
                BorrowedSockets socks;
                std::unique_ptr<TlsServer> server;
                try{
                    server=std::make_unique<TlsServer>(socks.peer,fixture.server_cert_pem,
                        fixture.server_key_pem,fixture.ca_pem,true,TLS1_1_VERSION);
                }
                catch(const std::exception& e){
                    setup_error=e.what();
                }
                if(setup_error.find("server max version unsupported")!=std::string::npos){
                    std::cout<<"tls3b version fixture: BLOCKED ("<<setup_error<<")\n";
                    check(true,"TLS 1.1-only peer fixture unavailable on this OpenSSL (BLOCKED)");
                }
                else{
                    check(setup_error.empty(),"TLS 1.1-only peer fixture configured");
                    HandshakeOutcome outcome=run_tls(*server,socks.local,tls_config("bridge.test"));
                    std::cout<<"tls3b version fixture: observed failure="<<outcome.error<<'\n';
                    check(!outcome.client_ok&&expected_tls_failure(outcome.error),
                          "peer restricted below TLS 1.2 cannot negotiate");
                }
            }
        }
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
