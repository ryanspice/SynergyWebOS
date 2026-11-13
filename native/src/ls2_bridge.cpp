#include "synergy/ls2_bridge.hpp"
#include <cstring>
#include <utility>

namespace synergy {
Ls2StatusBridge::Ls2StatusBridge(std::filesystem::path runtime, IdentityContract contract)
    : runtime_(std::move(runtime)), contract_(contract) {}

bool Ls2StatusBridge::authorized(std::string_view sender, IdentityContract contract) {
    return contract.exact_executable_role_verified && sender == AppId;
}

Json Ls2StatusBridge::dispatch(std::string_view sender, std::string_view payload) const {
    if (!authorized(sender, contract_)) return failure("LS2 caller denied");
    try {
        auto j = Json::parse(payload, 16384);
        if (!j.object_type() || j.members().size() != 1 ||
            j.get("op").str() != "status")
            return failure("Read-only bridge requires exactly op=status");
        // Existing kernel-authenticated Unix transport keeps its peer-owner,
        // framing, 16 KiB request / 128 KiB response and 2 s deadline checks.
        // Return the daemon schema unchanged; do not synthesize compatibility.
        return request(runtime_, j, 2000);
    } catch (const std::exception&) {
        return failure("LS2 status request invalid or daemon unavailable");
    }
}

bool Ls2StatusBridge::status(LSHandle* handle, LSMessage* message, void* context) noexcept {
    // No exceptions cross the C callback boundary. No caller content is logged.
    try {
        auto* self = static_cast<Ls2StatusBridge*>(context);
        const char* sender = LSMessageGetSenderServiceName(message);
        Json result;
        if (!self || !sender || !authorized(sender, self->contract_)) {
            result = failure("LS2 caller denied");
        } else {
            const char* raw = LSMessageGetPayload(message);
            const size_t size = raw ? strnlen(raw, 16385) : 0;
            result = size > 16384 ? failure("LS2 request too large") :
                self->dispatch(sender, std::string_view(raw ? raw : "", size));
        }
        auto payload = result.dump();
        if (payload.size() > 131072) payload = failure("LS2 response too large").dump();
        LSError error;
        LSErrorInit(&error);
        const bool sent = LSMessageReply(handle, message, payload.c_str(), &error);
        LSErrorFree(&error);
        (void)sent;
        return true; // handled; failed replies must not redispatch the request
    } catch (...) { return true; }
}

bool Ls2StatusBridge::attach(LSHandle* handle, LSError* error) {
    static LSMethod methods[] = {
        {"status", &Ls2StatusBridge::status, LUNA_METHOD_FLAGS_NONE},
        {nullptr, nullptr, LUNA_METHOD_FLAGS_NONE}
    };
    return LSRegisterCategory(handle, "/control", methods, nullptr, nullptr, error) &&
        LSCategorySetData(handle, "/control", this, error);
}
}
