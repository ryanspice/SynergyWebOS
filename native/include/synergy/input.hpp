#pragma once
#include "synergy/protocol.hpp"
#include <memory>
namespace synergy {
std::unique_ptr<Input> make_input(bool dry_run,std::function<void()> on_event={},int width=1920,int height=1080);
Json probe_input();
}
