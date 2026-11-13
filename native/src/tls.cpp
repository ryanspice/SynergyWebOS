#include "synergy/tls.hpp"
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <arpa/inet.h>
#include <poll.h>
#include <cerrno>
#include <climits>
#include <algorithm>
#include <utility>

namespace synergy {
namespace {
using Bio=std::unique_ptr<BIO,decltype(&BIO_free)>;
using Certificate=std::unique_ptr<X509,decltype(&X509_free)>;
struct SecretPem {
    std::string value;
    ~SecretPem(){if(!value.empty())OPENSSL_cleanse(value.data(),value.size());}
};
Bio pem_bio(const std::string& text){
    Bio bio(BIO_new_mem_buf(text.data(),static_cast<int>(text.size())),BIO_free);
    if(!bio)throw std::runtime_error("Cannot read input bridge pairing material");
    return bio;
}
void load_pairing(SSL_CTX* context,const Config& config){
    // Load the bytes from the checked descriptors, not a second path open.
    // Pairing files must be regular, private and owned by the daemon user.
    const auto ca=read_private_file(config.bridge_ca_file);
    auto input=pem_bio(ca);unsigned count=0;
    for(;;){
        Certificate cert(PEM_read_bio_X509(input.get(),nullptr,nullptr,nullptr),X509_free);
        if(!cert)break;
        if(X509_STORE_add_cert(SSL_CTX_get_cert_store(context),cert.get())!=1)
            throw std::runtime_error("Invalid input bridge CA bundle");
        ++count;
    }
    ERR_clear_error();
    if(!count)throw std::runtime_error("Input bridge CA certificate missing");
    const auto public_pem=read_private_file(config.bridge_cert_file);
    input=pem_bio(public_pem);
    Certificate leaf(PEM_read_bio_X509(input.get(),nullptr,nullptr,nullptr),X509_free);
    if(!leaf||SSL_CTX_use_certificate(context,leaf.get())!=1)
        throw std::runtime_error("Invalid input bridge client certificate");
    for(;;){
        Certificate chain(PEM_read_bio_X509(input.get(),nullptr,nullptr,nullptr),X509_free);
        if(!chain)break;
        if(SSL_CTX_add_extra_chain_cert(context,chain.get())!=1)
            throw std::runtime_error("Invalid input bridge client certificate chain");
        (void)chain.release();
    }
    ERR_clear_error();
    SecretPem key_pem{read_private_file(config.bridge_key_file)};
    input=pem_bio(key_pem.value);
    std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)> key(
        PEM_read_bio_PrivateKey(input.get(),nullptr,[](char*,int,int,void*){return 0;},nullptr),EVP_PKEY_free);
    if(!key||SSL_CTX_use_PrivateKey(context,key.get())!=1||SSL_CTX_check_private_key(context)!=1)
        throw std::runtime_error("Invalid input bridge private key");
}
}
struct TlsClient::Impl {
    std::unique_ptr<SSL_CTX,decltype(&SSL_CTX_free)> context{nullptr,SSL_CTX_free};
    std::unique_ptr<SSL,decltype(&SSL_free)> stream{nullptr,SSL_free};
    Wait wait;
    short receive_event=POLLIN;
    void wait_error(int error,int64_t deadline){
        if(error==SSL_ERROR_WANT_READ)wait(POLLIN,deadline);
        else if(error==SSL_ERROR_WANT_WRITE)wait(POLLOUT,deadline);
        else throw std::runtime_error("Authenticated input bridge TLS connection failed");
    }
};
TlsClient::TlsClient(int socket,const Config& config,Wait wait):impl_(std::make_unique<Impl>()){
    impl_->wait=std::move(wait);
    impl_->context.reset(SSL_CTX_new(TLS_client_method()));
    auto* context=impl_->context.get();
    if(!context)throw std::runtime_error("Cannot create input bridge TLS context");
    SSL_CTX_set_default_passwd_cb(context,[](char*,int,int,void*){return 0;});
    if(SSL_CTX_set_min_proto_version(context,TLS1_2_VERSION)!=1)
        throw std::runtime_error("Cannot set input bridge TLS version");
    load_pairing(context,config);
    SSL_CTX_set_verify(context,SSL_VERIFY_PEER,nullptr);
    impl_->stream.reset(SSL_new(context));
    auto* stream=impl_->stream.get();
    if(!stream||SSL_set_fd(stream,socket)!=1)throw std::runtime_error("Cannot initialize input bridge TLS stream");
    in_addr v4{};in6_addr v6{};
    const bool ip=inet_pton(AF_INET,config.bridge_server_name.c_str(),&v4)==1||
                  inet_pton(AF_INET6,config.bridge_server_name.c_str(),&v6)==1;
    if(ip){
        if(X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(stream),config.bridge_server_name.c_str())!=1)
            throw std::runtime_error("Invalid input bridge certificate identity");
    }else if(SSL_set1_host(stream,config.bridge_server_name.c_str())!=1||
             SSL_set_tlsext_host_name(stream,config.bridge_server_name.c_str())!=1)
        throw std::runtime_error("Invalid input bridge certificate identity");
    const auto deadline=monotonic_ms()+5000;
    for(;;){
        ERR_clear_error();
        int result=SSL_connect(stream);
        if(result==1)break;
        impl_->wait_error(SSL_get_error(stream,result),deadline);
    }
    if(SSL_get_verify_result(stream)!=X509_V_OK||!SSL_get0_peer_certificate(stream))
        throw std::runtime_error("Input bridge peer authentication failed");
}
TlsClient::~TlsClient()=default;
bool TlsClient::pending()const{return SSL_pending(impl_->stream.get())>0;}
short TlsClient::receive_event()const{return impl_->receive_event;}
std::ptrdiff_t TlsClient::receive(std::span<uint8_t> data){
    ERR_clear_error();
    int result=SSL_read(impl_->stream.get(),data.data(),static_cast<int>(std::min(data.size(),static_cast<size_t>(INT_MAX))));
    if(result>0){impl_->receive_event=POLLIN;return result;}
    const int error=SSL_get_error(impl_->stream.get(),result);
    if(error==SSL_ERROR_ZERO_RETURN)return 0;
    if(error==SSL_ERROR_WANT_READ||error==SSL_ERROR_WANT_WRITE){
        impl_->receive_event=error==SSL_ERROR_WANT_READ?POLLIN:POLLOUT;
        errno=EAGAIN;return -1;
    }
    throw std::runtime_error("Authenticated input bridge TLS read failed");
}
void TlsClient::send(std::span<const uint8_t> data){
    const auto deadline=monotonic_ms()+3000;
    size_t offset=0;
    while(offset<data.size()){
        const int size=static_cast<int>(std::min(data.size()-offset,static_cast<size_t>(INT_MAX)));
        ERR_clear_error();
        const int result=SSL_write(impl_->stream.get(),data.data()+offset,size);
        if(result>0){offset+=static_cast<size_t>(result);continue;}
        impl_->wait_error(SSL_get_error(impl_->stream.get(),result),deadline);
    }
}
}
