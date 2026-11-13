#pragma once
#include "synergy/config.hpp"
#include <functional>
#include <cstddef>
#include <memory>
#include <span>

namespace synergy {
// Borrows the already connected nonblocking socket. Every wait uses the
// client's cancellation fd and a finite deadline; no plaintext fallback.
class TlsClient {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    using Wait=std::function<void(short,int64_t)>;
    TlsClient(int socket,const Config&,Wait);
    ~TlsClient();
    TlsClient(const TlsClient&)=delete;
    TlsClient& operator=(const TlsClient&)=delete;
    bool pending()const;
    short receive_event()const;
    std::ptrdiff_t receive(std::span<uint8_t>);
    void send(std::span<const uint8_t>);
};
}
