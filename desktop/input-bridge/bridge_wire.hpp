#pragma once

// Pure, platform-independent wire logic for the desktop input bridge.
// No sockets, OpenSSL or Windows headers belong here so the parser, queue and
// controller coalescing policy can be unit tested on any C++20 host.

#include "synergy/gamepad_wire.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace bridge_wire {

using Bytes = std::vector<std::uint8_t>;

inline constexpr std::size_t QueueLimit = 64 * 1024;
inline constexpr std::size_t QueueChunkLimit = 256;

// The Synergy protocol reference allows a maximum message length of 4 MiB.
// The framer never retains a whole payload, so this bound only governs the
// incremental command inspection, not memory use.
inline constexpr std::uint32_t MaxFrameLength = 4u * 1024u * 1024u;

// Timing policy (milliseconds).  Transport progress, partial-frame progress
// and controller freshness are tracked separately: a large but progressing
// transfer must not be mistaken for a stalled connection or stale input.
inline constexpr std::int64_t RelayProgressMs = 3000;
inline constexpr std::int64_t FrameStallMs = 5000;
inline constexpr std::int64_t FrameTransferDeadlineMs = 60000;
inline constexpr std::int64_t ControllerSampleIntervalMs = 8;

// Observes bytes only after the corresponding read/write completed.  It keeps
// at most 14 payload bytes and never retains a clipboard body.  A frame longer
// than MaxFrameLength is rejected, but frames up to that size stream through
// unchanged because the parser is incremental.
struct Framer {
    std::array<std::uint8_t, 4> prefix{};
    std::array<std::uint8_t, 14> head{};
    std::size_t prefix_used = 0, head_used = 0;
    std::uint32_t remaining = 0, length = 0, capture = 0;
    bool active = false;

    bool boundary() const { return prefix_used == 0; }

    void consume(std::span<const std::uint8_t> data) {
        for (auto byte : data) {
            if (prefix_used < 4) {
                prefix[prefix_used++] = byte;
                if (prefix_used == 4) {
                    length = 0;
                    for (auto value : prefix) length = (length << 8) | value;
                    if (length < 4 || length > MaxFrameLength)
                        throw std::runtime_error("Invalid classic frame size");
                    remaining = length;
                    head_used = 0;
                }
                continue;
            }
            if (head_used < head.size()) head[head_used++] = byte;
            if (--remaining) continue;
            if (std::memcmp(head.data(), "CINN", 4) == 0) {
                if (length != 14) throw std::runtime_error("Malformed focus enter");
                capture = 0;
                for (std::size_t i = 8; i < 12; ++i) capture = (capture << 8) | head[i];
                active = true;
            } else if (std::memcmp(head.data(), "COUT", 4) == 0 ||
                       std::memcmp(head.data(), "CBYE", 4) == 0) {
                if (length != 4) throw std::runtime_error("Malformed focus leave");
                active = false;
            }
            prefix_used = 0;
        }
    }
};

// One ordered writer with explicit byte and chunk budgets.
struct Queue {
    std::deque<Bytes> chunks;
    std::size_t offset = 0, bytes = 0;

    bool empty() const { return chunks.empty(); }

    void push(Bytes data) {
        if (chunks.size() >= QueueChunkLimit || bytes + data.size() > QueueLimit)
            throw std::runtime_error("Relay queue budget exceeded");
        bytes += data.size();
        chunks.push_back(std::move(data));
    }

    std::span<const std::uint8_t> front() const {
        return std::span<const std::uint8_t>(chunks.front()).subspan(offset);
    }

    void consume(std::size_t n) {
        bytes -= n;
        offset += n;
        if (offset == chunks.front().size()) {
            chunks.pop_front();
            offset = 0;
        }
    }
};

inline Bytes framed(std::span<const std::uint8_t> payload) {
    const auto n = static_cast<std::uint32_t>(payload.size());
    Bytes data;
    data.reserve(4 + payload.size());
    data.push_back(static_cast<std::uint8_t>(n >> 24));
    data.push_back(static_cast<std::uint8_t>(n >> 16));
    data.push_back(static_cast<std::uint8_t>(n >> 8));
    data.push_back(static_cast<std::uint8_t>(n));
    data.insert(data.end(), payload.begin(), payload.end());
    return data;
}

using GamepadState = synergy::bridge::GamepadState;
inline constexpr std::size_t Slots = synergy::bridge::Slots;

struct PadSample {
    std::uint8_t slot{};
    std::uint32_t capture{};
    GamepadState state{};
    std::int64_t captured{};
};

// A connection change, an explicit neutral/disconnected state, or a released
// button is safety-relevant: the TV uses these to undo held controls.  Such a
// sample must not be coalesced away by a newer analog-only sample.
inline bool is_authoritative(const GamepadState& next, const GamepadState& previous) {
    if (next.connected != previous.connected) return true;
    if (!next.connected) return true;
    if ((previous.buttons & ~next.buttons) != 0) return true;
    return false;
}

// Coalesces controller updates that have not started transmission.  The newest
// state per slot wins, except that an already queued authoritative release or
// disconnect survives until it is transmitted.  Sequence numbers are assigned
// by the caller only when a sample actually starts transmission, so dropped
// samples never leave gaps or replay stale input.
class PadCoalescer {
public:
    std::array<std::optional<PadSample>, Slots> pending{};
    std::array<GamepadState, Slots> last_sent{};
    bool initialized = false;

    // Focus loss clears only unsent state.  A write already handed to OpenSSL
    // is held by the caller and must remain stable until it completes.
    void focus_lost() {
        pending.fill(std::nullopt);
        last_sent.fill({});
        initialized = false;
    }

    void observe(std::uint8_t slot, std::uint32_t capture, const GamepadState& state,
                 std::int64_t now) {
        if (slot >= Slots) return;
        if (!initialized) {
            last_sent.fill({});
            initialized = true;
        }
        PadSample fresh{slot, capture, state, now};
        auto& queued = pending[slot];
        if (!queued.has_value()) {
            queued = fresh;
            return;
        }
        const bool queued_authoritative = is_authoritative(queued->state, last_sent[slot]);
        const bool fresh_authoritative = is_authoritative(fresh.state, last_sent[slot]);
        if (queued_authoritative && !fresh_authoritative) return;  // preserve release/disconnect
        queued = fresh;  // newest wins; authoritative state overrides
    }

    // Authoritative samples are delivered first, then the oldest sample.
    std::optional<PadSample> take_next() {
        std::size_t best = Slots;
        bool best_authoritative = false;
        std::int64_t best_time = std::numeric_limits<std::int64_t>::max();
        for (std::size_t i = 0; i < Slots; ++i) {
            if (!pending[i].has_value()) continue;
            const bool authoritative = is_authoritative(pending[i]->state, last_sent[i]);
            const bool prefer = best == Slots ||
                (authoritative && !best_authoritative) ||
                (authoritative == best_authoritative && pending[i]->captured < best_time);
            if (prefer) {
                best = i;
                best_authoritative = authoritative;
                best_time = pending[i]->captured;
            }
        }
        if (best == Slots) return std::nullopt;
        auto result = pending[best];
        pending[best].reset();
        return result;
    }

    void mark_sent(const PadSample& sample) {
        if (sample.slot < Slots) last_sent[sample.slot] = sample.state;
    }

    bool empty() const {
        for (const auto& item : pending)
            if (item.has_value()) return false;
        return true;
    }
};

}  // namespace bridge_wire
