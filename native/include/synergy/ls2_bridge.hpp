#pragma once
#include "synergy/ipc.hpp"
#include <luna-service2/lunaservice.h>
#include <string_view>

namespace synergy {
// Read-only first milestone. No registration, privilege acquisition or fallback.
// Lifetime: create before attaching the handle; destroy after LSUnregister.
class Ls2StatusBridge {
public:
    struct IdentityContract {
        // Set only after the effective TV hub role binds AppId exclusively to
        // the installed UI executable and unrelated registration is denied.
        bool exact_executable_role_verified = false;
    };
    Ls2StatusBridge(std::filesystem::path runtime, IdentityContract contract);
    bool attach(LSHandle*, LSError*);
    static bool authorized(std::string_view bus_sender, IdentityContract);
    // Public for bounded offline regression fixtures; sender comes from LS2,
    // never a JSON member. No mutation is supported by this milestone.
    Json dispatch(std::string_view bus_sender, std::string_view payload) const;
private:
    std::filesystem::path runtime_;
    IdentityContract contract_;
    static bool status(LSHandle*, LSMessage*, void*) noexcept;
};
}
