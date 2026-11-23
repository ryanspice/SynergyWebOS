// Focused host tests for the platform-independent bridge wire logic.
// These verify the parser and controller coalescing policy without sockets,
// OpenSSL or Windows headers.
#include "../bridge_wire.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;

void check(bool value, const char* label) {
    if (!value) {
        std::cerr << "FAIL: " << label << '\n';
        throw std::runtime_error(label);
    }
    ++checks;
}

bridge_wire::Bytes body(std::size_t n, std::uint8_t seed = 0) {
    bridge_wire::Bytes data(n);
    for (std::size_t i = 0; i < n; ++i) data[i] = static_cast<std::uint8_t>(i + seed);
    return data;
}

void feed(bridge_wire::Framer& framer, const bridge_wire::Bytes& bytes, std::size_t step) {
    for (std::size_t i = 0; i < bytes.size(); i += step) {
        const auto end = std::min(bytes.size(), i + step);
        framer.consume(std::span<const std::uint8_t>(bytes).subspan(i, end - i));
    }
}

void test_framer_focus() {
    bridge_wire::Framer framer;
    auto cinn = bridge_wire::framed(
        bridge_wire::Bytes{'C', 'I', 'N', 'N', 1, 2, 3, 4, 0, 0, 0, 41, 5, 6});
    check(cinn.size() == 18, "CINN frame length is 4 + 14");
    feed(framer, cinn, 1);
    check(framer.active && framer.capture == 41, "fragmented CINN updates focus and capture ID");
    check(framer.boundary(), "framer returns to a boundary after CINN");

    auto cout = bridge_wire::framed(bridge_wire::Bytes{'C', 'O', 'U', 'T'});
    feed(framer, cout, 3);
    check(!framer.active, "COUT clears focus");
    feed(framer, bridge_wire::framed(bridge_wire::Bytes{'C', 'B', 'Y', 'E'}), 2);
    check(!framer.active, "CBYE clears focus");

    auto hello = bridge_wire::framed(bridge_wire::Bytes{'S', 'y', 'n', 'e', 'r', 'g', 'y'});
    feed(framer, hello, 5);
    check(framer.boundary() && !framer.active, "framed Synergy hello is accepted unchanged");
}

void test_framer_sizes() {
    // 320 KiB opaque body streams through with only the 14-byte head retained.
    bridge_wire::Framer framer;
    const auto payload = body(320 * 1024, 7);
    const auto frame = bridge_wire::framed(payload);
    feed(framer, frame, 16384);
    check(framer.boundary(), "320 KiB body reaches a frame boundary");

    // The protocol maximum is 4 MiB: a 1 MiB+ frame must be accepted.
    bridge_wire::Framer large;
    const auto big = bridge_wire::framed(body(bridge_wire::MaxFrameLength, 3));
    feed(large, big, 65536);
    check(large.boundary(), "4 MiB frame is accepted by the incremental parser");
    check(large.head.size() == 14, "framer head stays at 14 bytes for a maximum frame");

    for (std::uint32_t bad : {std::uint32_t{3}, bridge_wire::MaxFrameLength + 1}) {
        bool rejected = false;
        try {
            bridge_wire::Framer f;
            f.consume(bridge_wire::Bytes{static_cast<std::uint8_t>(bad >> 24),
                                         static_cast<std::uint8_t>(bad >> 16),
                                         static_cast<std::uint8_t>(bad >> 8),
                                         static_cast<std::uint8_t>(bad)});
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        check(rejected, "invalid classic frame length is rejected");
    }
}

void test_queue() {
    bridge_wire::Queue queue;
    queue.push(body(100, 1));
    queue.push(body(100, 2));
    check(queue.bytes == 200 && !queue.empty(), "queue tracks bytes and chunks");
    queue.consume(100);  // the relay always consumes exactly the front span
    check(queue.bytes == 100 && queue.front().size() == 100, "queue advances to the next chunk");
    queue.consume(100);
    check(queue.empty(), "queue empties after the final chunk");

    bool rejected = false;
    try {
        bridge_wire::Queue bounded;
        bounded.push(body(bridge_wire::QueueLimit + 1));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    check(rejected, "queue rejects payloads beyond its byte budget");
}

// Regression: rapid press/release/press/release must all survive queuing.
// On the single-snapshot coalescer this fails because the queued release is
// overwritten by the re-press, so the final release is never emitted.
void test_rapid_press_release_edges() {
    bridge_wire::PadCoalescer q;
    bridge_wire::GamepadState down{};
    down.connected = true;
    down.buttons = 0x0001;
    auto up = down;
    up.buttons = 0;
    q.observe(0, 41, down, 0);
    auto first = q.take_next();
    check(first && first->state.buttons == 1, "initial press delivered");
    q.mark_sent(*first);
    q.observe(0, 41, up, 8);
    q.observe(0, 41, down, 16);
    q.observe(0, 41, up, 24);
    for (const auto expected : {0u, 1u, 0u}) {
        auto next = q.take_next();
        check(next && next->state.buttons == expected,
              "every queued release/re-press edge survives");
        q.mark_sent(*next);
    }
    check(q.empty(), "edge queue drained");
}

void test_coalescing() {
    using bridge_wire::GamepadState;
    using bridge_wire::PadCoalescer;

    PadCoalescer pads;
    GamepadState analog{};
    analog.connected = true;
    analog.lx = 100;
    pads.observe(0, 41, analog, 0);
    GamepadState newer = analog;
    newer.lx = 200;
    pads.observe(0, 41, newer, 8);
    check(pads.pending[0].back().sample.state.lx == 200, "newest analog sample replaces the older one");
    check(pads.pending[0].back().sample.captured == 8, "coalesced sample keeps the newest timestamp");
    check(pads.pending[0].size() == 2, "initial discrete event and coalesced analog remain queued");
    GamepadState third = newer;
    third.lx = 300;
    pads.observe(0, 41, third, 16);
    check(pads.pending[0].size() == 2 && pads.pending[0].back().sample.state.lx == 300,
          "consecutive analog snapshots coalesce without growing the queue");

    // An analog refresh cannot overwrite a queued button edge.
    PadCoalescer release;
    GamepadState pressed{};
    pressed.connected = true;
    pressed.buttons = 0x0001;
    release.observe(1, 41, pressed, 0);
    release.mark_sent(release.take_next().value());  // last_sent[1] = pressed
    GamepadState let_go = pressed;
    let_go.buttons = 0;
    release.observe(1, 41, let_go, 8);
    GamepadState analog_refresh = let_go;
    analog_refresh.lx = 50;
    release.observe(1, 41, analog_refresh, 16);
    check(release.pending[1].size() == 2, "analog refresh after a release is a separate event");
    check(release.pending[1].front().sample.state.buttons == 0, "queued release stays at the front");
    check(release.pending[1].back().sample.state.lx == 50, "analog refresh is preserved behind the edge");

    // Disconnects are authoritative and are delivered before analog updates.
    PadCoalescer priority;
    GamepadState connected{};
    connected.connected = true;
    connected.lx = 10;
    priority.observe(2, 41, connected, 0);
    priority.mark_sent(priority.take_next().value());  // last_sent[2] = connected
    GamepadState moved = connected;
    moved.lx = 20;
    priority.observe(2, 41, moved, 8);
    GamepadState neutral{};  // connected == false
    priority.observe(3, 41, neutral, 12);
    const auto first = priority.take_next();
    check(first && first->slot == 3, "authoritative disconnect is delivered first");
    const auto second = priority.take_next();
    check(second && second->slot == 2, "analog update follows the authoritative report");
    check(priority.empty(), "coalescer empties after all samples are taken");

    // Focus loss clears only unsent state; held samples are discarded safely.
    PadCoalescer lost;
    lost.observe(0, 41, connected, 0);
    lost.focus_lost();
    check(lost.empty() && !lost.initialized, "focus loss discards unsent controller state");
}

void test_edge_ordering_and_generations() {
    using bridge_wire::GamepadState;
    using bridge_wire::PadCoalescer;

    // Disconnect/reconnect stays ordered within a slot even when another slot
    // has an authoritative event that is selected first.
    PadCoalescer ordered;
    GamepadState connected{};
    connected.connected = true;
    ordered.observe(0, 41, connected, 0);
    ordered.mark_sent(ordered.take_next().value());
    GamepadState disconnected{};
    ordered.observe(1, 41, disconnected, 4);
    ordered.observe(0, 41, disconnected, 8);
    ordered.observe(0, 41, connected, 16);
    auto cross_slot = ordered.take_next();
    check(cross_slot && cross_slot->slot == 1, "other slot's disconnect is selected first");
    auto first = ordered.take_next();
    auto second = ordered.take_next();
    check(first && !first->state.connected, "disconnect is delivered before reconnect in one slot");
    check(second && second->state.connected, "reconnect follows disconnect in the same slot");
    check(ordered.empty(), "ordered edge queue drains completely");

    // Different capture generations never coalesce into one event.
    PadCoalescer generations;
    GamepadState analog{};
    analog.connected = true;
    analog.lx = 10;
    generations.observe(0, 41, analog, 0);
    analog.lx = 20;
    generations.observe(0, 42, analog, 8);
    check(generations.pending[0].size() == 2, "analog snapshots from different captures are not mixed");
    check(generations.take_next()->capture == 41, "first generation is delivered first");
    check(generations.take_next()->capture == 42, "second generation follows");

    // A trigger zero crossing is a discrete edge, not an analog update.
    PadCoalescer triggers;
    GamepadState baseline{};
    baseline.connected = true;
    triggers.observe(2, 41, baseline, 0);
    triggers.mark_sent(triggers.take_next().value());
    GamepadState firing = baseline;
    firing.rt = 255;
    triggers.observe(2, 41, firing, 8);
    check(triggers.pending[2].size() == 1 && triggers.pending[2].back().discrete,
          "trigger zero crossing is queued as a discrete edge");
}

// Models the relay's in-flight TLS write: a sample already taken for
// transmission must stay byte-for-byte stable when focus is lost, while every
// unsent sample is discarded.  The full TLS WANT_WRITE path is exercised by a
// separate slow-reader end-to-end probe.
void test_inflight_focus_loss() {
    using bridge_wire::GamepadState;
    using bridge_wire::PadCoalescer;

    PadCoalescer pads;
    GamepadState held{};
    held.connected = true;
    held.buttons = 0x0001;
    pads.observe(0, 41, held, 0);
    const auto inflight = pads.take_next();  // handed to SSL_write, now frozen
    check(inflight && inflight->state.buttons == 1, "in-flight sample captured before focus loss");

    GamepadState unsent = held;
    unsent.buttons = 0x0002;
    pads.observe(1, 41, unsent, 8);
    pads.focus_lost();
    check(pads.empty(), "focus loss discards all unsent samples");
    check(inflight->state.buttons == 1 && inflight->capture == 41 && inflight->slot == 0,
          "in-flight sample bytes remain unchanged after focus loss");
    pads.mark_sent(*inflight);  // a completed write is still recorded
    check(pads.last_sent[0].buttons == 1, "completed in-flight write updates last-sent state");

    // A new capture starts clean; no old-generation state leaks into it.
    GamepadState reentry{};
    reentry.connected = true;
    pads.observe(0, 42, reentry, 16);
    auto next = pads.take_next();
    check(next && next->capture == 42, "re-entry emits only the new capture generation");
}

void test_budget_and_staleness() {
    using bridge_wire::GamepadState;
    using bridge_wire::PadCoalescer;

    // Discrete edges are bounded; overflow fails closed and keeps old edges.
    PadCoalescer bounded;
    GamepadState state{};
    state.connected = true;
    bounded.observe(0, 41, state, 0);
    for (std::size_t i = 0; i < PadCoalescer::MaxPendingPerSlot - 1; ++i) {
        state.buttons = static_cast<std::uint16_t>(i & 1u ? 1u : 0u);
        bounded.observe(0, 41, state, static_cast<std::int64_t>(i + 1));
    }
    state.buttons = 1;
    bool overflow = false;
    try {
        bounded.observe(0, 41, state, 9999);
    } catch (const bridge_wire::PadQueueOverflow&) {
        overflow = true;
    }
    check(overflow, "queue overflow raises an explicit typed error");
    check(bounded.pending[0].size() == PadCoalescer::MaxPendingPerSlot,
          "overflow does not consume or drop an existing event");

    // Aged non-authoritative events are pruned; safety edges are not.
    PadCoalescer stale;
    GamepadState connected{};
    connected.connected = true;
    stale.observe(0, 41, connected, 0);
    stale.mark_sent(stale.take_next().value());
    GamepadState press = connected;
    press.buttons = 0x0001;
    stale.observe(0, 41, press, 0);
    stale.prune_stale(PadCoalescer::StaleEventMs + 1);
    check(stale.empty() && stale.dropped_stale == 1,
          "stale press is explicitly pruned instead of replayed");

    PadCoalescer safety;
    GamepadState held{};
    held.connected = true;
    held.buttons = 0x0001;
    safety.observe(0, 41, held, 0);
    safety.mark_sent(safety.take_next().value());
    GamepadState released = held;
    released.buttons = 0;
    safety.observe(0, 41, released, 0);
    safety.prune_stale(PadCoalescer::StaleEventMs + 1);
    check(safety.pending[0].size() == 1 && safety.dropped_stale == 0,
          "stale release is retained because applying it is safe");
}
}  // namespace

int main() {
    try {
        test_framer_focus();
        test_framer_sizes();
        test_queue();
        test_rapid_press_release_edges();
        test_coalescing();
        test_edge_ordering_and_generations();
        test_inflight_focus_loss();
        test_budget_and_staleness();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "bridge_wire_test: " << checks << " checks passed\n";
    return 0;
}
