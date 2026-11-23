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
    check(pads.pending[0]->state.lx == 200, "newest analog sample replaces the older one");
    check(pads.pending[0]->captured == 8, "coalesced sample keeps the newest timestamp");

    // A queued release (button cleared) must survive a later analog-only sample.
    PadCoalescer release;
    GamepadState pressed{};
    pressed.connected = true;
    pressed.buttons = 0x0001;
    release.observe(1, 41, pressed, 0);
    release.mark_sent(release.take_next().value());  // last_sent[1] = pressed
    GamepadState let_go = pressed;
    let_go.buttons = 0;
    release.observe(1, 41, let_go, 8);
    GamepadState re_pressed = let_go;
    re_pressed.buttons = 0x0001;
    re_pressed.lx = 50;
    release.observe(1, 41, re_pressed, 16);
    check(release.pending[1]->state.buttons == 0,
          "authoritative button release is not coalesced away by a later analog sample");

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
}  // namespace

int main() {
    try {
        test_framer_focus();
        test_framer_sizes();
        test_queue();
        test_coalescing();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "bridge_wire_test: " << checks << " checks passed\n";
    return 0;
}
