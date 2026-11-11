#pragma once
#include "synergy/protocol.hpp"
#include <memory>
namespace synergy {
std::unique_ptr<Input> make_input(bool dry_run,std::function<void()> on_event={});
Json probe_input();
}
