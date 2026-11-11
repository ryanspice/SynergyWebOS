#pragma once
#include "spice/json.hpp"
#include <filesystem>
#include <string>
#include <cstdint>
namespace synergy {
using Json=spice::Json;
inline constexpr const char* AppId="dev.canopy.synergywebos";
inline constexpr const char* HomeApp="com.webos.app.home";
struct Config {
    std::string server;
    int port=24800;
    std::string name="webos-tv";
    int width=1920,height=1080;
    bool dry_run=true,allow_plaintext=false,cursor_keepalive=false;
    std::string home_target="system";
    int home_delay_ms=0;
    int idle_timeout_ms=15000;
    Json json()const;
    static Config from_json(const Json&);
    void validate(bool connecting=true)const;
};
int checked_integer(const std::string&,int lo,int hi);
bool numeric_lan_address(const std::string&);
bool safe_app_id(const std::string&);
std::filesystem::path default_runtime();
void validate_runtime(const std::filesystem::path&,bool create);
std::string read_private_file(const std::filesystem::path&,size_t limit=65536);
void atomic_private_write(const std::filesystem::path&,const std::string&);
int64_t monotonic_ms();
Json okay();
Json failure(const std::string&);
}
