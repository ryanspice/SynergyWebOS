#pragma once

// Pure, platform-independent wire logic for the desktop input bridge.
// No sockets, OpenSSL or platform headers belong here; the shared gamepad wire
// header is standard C++ only.  This keeps the parser, queue and controller
// coalescing policy unit-testable on any C++20 host.

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

// Protocol framing violations are reported with a typed error so the relay can
// translate them into a categorized [FRAME] session-end reason.
class FrameError : public std::runtime_error {
public:
    explicit FrameError(const char* what) : std::runtime_error(what) {}
};

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
                        throw FrameError("Invalid classic frame size");
                    remaining = length;
                    head_used = 0;
                }
                continue;
            }
            if (head_used < head.size()) head[head_used++] = byte;
            if (--remaining) continue;
            if (std::memcmp(head.data(), "CINN", 4) == 0) {
                if (length != 14) throw FrameError("Malformed focus enter");
                capture = 0;
                for (std::size_t i = 8; i < 12; ++i) capture = (capture << 8) | head[i];
                active = true;
            } else if (std::memcmp(head.data(), "COUT", 4) == 0 ||
                       std::memcmp(head.data(), "CBYE", 4) == 0) {
                if (length != 4) throw FrameError("Malformed focus leave");
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
    // One sized allocation with indexed copies.  The reserve/push_back/insert
    // pattern triggers a GCC 15 -Werror=free-nonheap-object false positive at
    // -O3; this form is equivalent and keeps strict warnings enabled.
    Bytes data(4 + payload.size());
    data[0] = static_cast<std::uint8_t>(n >> 24);
    data[1] = static_cast<std::uint8_t>(n >> 16);
    data[2] = static_cast<std::uint8_t>(n >> 8);
    data[3] = static_cast<std::uint8_t>(n);
    for (std::size_t i = 0; i < payload.size(); ++i) data[4 + i] = payload[i];
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

// Raised by the coalescer when a per-slot event queue is full.  The relay
// translates this into a categorized reason instead of silently dropping a
// release or a connection transition.
class PadQueueOverflow : public std::runtime_error {
public:
    PadQueueOverflow() : std::runtime_error("Controller event queue budget exceeded") {}
};

// A safety event undoes an active control on the TV and must survive pruning
// even when stale: disconnect/neutral, a released button, a trigger returning
// to zero, or any single stick axis returning to neutral.  An activation (a
// controller connecting with a button already held) is NOT a safety event;
// replaying it after a long delay would re-press a button the user released.
inline bool is_safety_event(const GamepadState& next, const GamepadState& previous) {
    if (next.connected != previous.connected) return !next.connected;
    if (!next.connected) return true;
    if ((previous.buttons & ~next.buttons) != 0) return true;
    if (previous.lt != 0 && next.lt == 0) return true;
    if (previous.rt != 0 && next.rt == 0) return true;
    if ((previous.lx != 0 && next.lx == 0) || (previous.ly != 0 && next.ly == 0) ||
        (previous.rx != 0 && next.rx == 0) || (previous.ry != 0 && next.ry == 0))
        return true;
    return false;
}

// Cross-slot ordering only: a connection activation is delivered before
// analog-only updates, but it is NOT exempt from stale pruning (see
// is_safety_event).  Keeping the two policies separate preserves both the
// established delivery order and the no-stale-replay guarantee.
inline bool is_authoritative(const GamepadState& next, const GamepadState& previous) {
    if (next.connected != previous.connected) return true;
    if (!next.connected) return true;
    return is_safety_event(next, previous);
}

struct PendingPad {
    PadSample sample;
    bool discrete;
};

// Bounded per-slot FIFO of controller transitions.  Discrete edges (button
// press/release, connect/disconnect, trigger zero crossings and stick
// neutralization) are determined against the last *observed* state and are
// kept in order.  Only consecutive pending analog-only snapshots for the same
// slot and capture generation are coalesced into the newest one.
class PadCoalescer {
public:
    static constexpr std::size_t MaxPendingPerSlot = 256;
    // A press delayed longer than this must not be replayed merely to drain a
    // queue.  Safety edges (releases/neutrals) are never pruned by age.
    static constexpr std::int64_t StaleEventMs = 500;

    std::array<std::deque<PendingPad>, Slots> pending{};
    std::array<GamepadState, Slots> last_sent{}, last_observed{};
    std::array<bool, Slots> have_observed{};
    bool initialized = false;
    std::size_t dropped_stale = 0;

    static bool neutral_sticks(const GamepadState& s) {
        return s.lx == 0 && s.ly == 0 && s.rx == 0 && s.ry == 0;
    }

    static bool stick_neutralized(const GamepadState& a, const GamepadState& b) {
        return (a.lx != 0 && b.lx == 0) || (a.ly != 0 && b.ly == 0) ||
               (a.rx != 0 && b.rx == 0) || (a.ry != 0 && b.ry == 0);
    }

    static bool is_discrete_change(const GamepadState& a, const GamepadState& b) {
        return a.connected != b.connected || a.buttons != b.buttons ||
            ((a.lt == 0) != (b.lt == 0)) ||
            ((a.rt == 0) != (b.rt == 0)) ||
            stick_neutralized(a, b);
    }

    // Focus loss clears only unsent state.  A write already handed to OpenSSL
    // is held by the caller and must remain stable until it completes.
    void focus_lost() {
        for (auto& queue : pending) queue.clear();
        last_sent.fill({});
        last_observed.fill({});
        have_observed.fill(false);
        initialized = false;
    }

    void observe(std::uint8_t slot, std::uint32_t capture, const GamepadState& state,
                 std::int64_t now) {
        if (slot >= Slots) return;
        const bool discrete = !have_observed[slot] ||
            is_discrete_change(last_observed[slot], state);
        have_observed[slot] = true;
        initialized = true;
        last_observed[slot] = state;
        const PadSample fresh{slot, capture, state, now};
        auto& queue = pending[slot];
        if (!discrete && !queue.empty() && !queue.back().discrete &&
            queue.back().sample.capture == capture) {
            queue.back().sample = fresh;  // coalesce analog only, never a discrete edge
            return;
        }
        if (queue.size() >= MaxPendingPerSlot) throw PadQueueOverflow();
        queue.push_back({fresh, discrete});
    }

    // Projects a stale full-state report onto safety only: every release is
    // retained, but no control that was inactive in last_sent may be activated.
    // This matters when one full-state event releases one button and presses
    // another -- blindly replaying it would re-press the new button.
    static GamepadState safety_projection(const GamepadState& next, const GamepadState& previous) {
        GamepadState out{};
        out.connected = previous.connected && next.connected;
        if (!out.connected) return out;
        out.buttons = static_cast<std::uint16_t>(previous.buttons & next.buttons);
        out.lx = previous.lx == 0 ? 0 : next.lx;
        out.ly = previous.ly == 0 ? 0 : next.ly;
        out.rx = previous.rx == 0 ? 0 : next.rx;
        out.ry = previous.ry == 0 ? 0 : next.ry;
        out.lt = previous.lt == 0 ? 0 : next.lt;
        out.rt = previous.rt == 0 ? 0 : next.rt;
        return out;
    }

    // Drops aged non-safety events.  A stale safety event is kept, but its
    // full state is projected so a stale activation cannot be replayed inside
    // an otherwise safety-relevant (mixed) transition.
    void prune_stale(std::int64_t now) {
        for (std::size_t slot = 0; slot < Slots; ++slot) {
            auto& queue = pending[slot];
            for (auto it = queue.begin(); it != queue.end();) {
                if (now - it->sample.captured < StaleEventMs) { ++it; continue; }
                if (!is_safety_event(it->sample.state, last_sent[slot])) {
                    it = queue.erase(it);
                    ++dropped_stale;
                    continue;
                }
                it->sample.state = safety_projection(it->sample.state, last_sent[slot]);
                ++it;
            }
        }
    }

    // Authoritative events are delivered first across slots; within a slot the
    // FIFO order is never changed.
    std::optional<PadSample> take_next() {
        std::size_t best = Slots;
        bool best_authoritative = false;
        std::int64_t best_time = std::numeric_limits<std::int64_t>::max();
        for (std::size_t i = 0; i < Slots; ++i) {
            if (pending[i].empty()) continue;
            const auto& front = pending[i].front().sample;
            const bool authoritative = is_authoritative(front.state, last_sent[i]);
            const bool prefer = best == Slots ||
                (authoritative && !best_authoritative) ||
                (authoritative == best_authoritative && front.captured < best_time);
            if (prefer) {
                best = i;
                best_authoritative = authoritative;
                best_time = front.captured;
            }
        }
        if (best == Slots) return std::nullopt;
        const auto result = pending[best].front().sample;
        pending[best].pop_front();
        return result;
    }

    void mark_sent(const PadSample& sample) {
        if (sample.slot < Slots) last_sent[sample.slot] = sample.state;
    }

    bool empty() const {
        for (const auto& queue : pending)
            if (!queue.empty()) return false;
        return true;
    }
};

}  // namespace bridge_wire
