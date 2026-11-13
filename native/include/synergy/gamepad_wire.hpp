#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace synergy::bridge {
inline constexpr std::size_t PayloadSize = 32;
inline constexpr std::size_t Slots = 4;
struct GamepadState {
    std::uint16_t buttons{};
    std::int16_t lx{}, ly{}, rx{}, ry{};
    std::uint8_t lt{}, rt{};
    bool connected{};
    bool operator==(const GamepadState&) const = default;
};
struct GamepadPacket {
    std::uint8_t slot{};
    std::uint32_t capture{};
    std::uint64_t sequence{};
    GamepadState state{};
};
namespace detail {
inline bool valid(const GamepadPacket& p) {
    return p.slot < Slots && p.sequence != 0 && !(p.state.buttons & 0x0c00u) &&
        (p.state.connected || p.state == GamepadState{});
}
inline void put(std::array<std::uint8_t, PayloadSize>& b, std::size_t off, std::uint64_t v, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) b[off + n - 1 - i] = static_cast<std::uint8_t>(v >> (8 * i));
}
inline std::uint64_t get(std::span<const std::uint8_t> b, std::size_t off, std::size_t n) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < n; ++i) v = (v << 8) | b[off + i];
    return v;
}
inline std::int16_t signed16(std::uint16_t v) {
    return static_cast<std::int16_t>(v <= 0x7fff ? static_cast<int>(v) : static_cast<int>(v) - 65536);
}
}
inline std::optional<std::array<std::uint8_t, PayloadSize>> encode_gamepad(const GamepadPacket& p) {
    if (!detail::valid(p)) return std::nullopt;
    std::array<std::uint8_t, PayloadSize> b{};
    b[0] = 'D'; b[1] = 'G'; b[2] = 'P'; b[3] = 'D'; b[4] = 1; b[5] = p.slot; b[6] = p.state.connected ? 1 : 0;
    detail::put(b, 8, p.capture, 4); detail::put(b, 12, p.sequence, 8);
    detail::put(b, 20, p.state.buttons, 2);
    detail::put(b, 22, static_cast<std::uint16_t>(p.state.lx), 2);
    detail::put(b, 24, static_cast<std::uint16_t>(p.state.ly), 2);
    detail::put(b, 26, static_cast<std::uint16_t>(p.state.rx), 2);
    detail::put(b, 28, static_cast<std::uint16_t>(p.state.ry), 2);
    b[30] = p.state.lt; b[31] = p.state.rt;
    return b;
}
inline std::optional<GamepadPacket> decode_gamepad(std::span<const std::uint8_t> b) {
    if (b.size() != PayloadSize || b[0] != 'D' || b[1] != 'G' || b[2] != 'P' || b[3] != 'D' || b[4] != 1 || b[6] > 1 || b[7] != 0) return std::nullopt;
    GamepadPacket p{};
    p.slot = b[5]; p.capture = static_cast<std::uint32_t>(detail::get(b, 8, 4)); p.sequence = detail::get(b, 12, 8);
    p.state.buttons = static_cast<std::uint16_t>(detail::get(b, 20, 2));
    p.state.lx = detail::signed16(static_cast<std::uint16_t>(detail::get(b, 22, 2)));
    p.state.ly = detail::signed16(static_cast<std::uint16_t>(detail::get(b, 24, 2)));
    p.state.rx = detail::signed16(static_cast<std::uint16_t>(detail::get(b, 26, 2)));
    p.state.ry = detail::signed16(static_cast<std::uint16_t>(detail::get(b, 28, 2)));
    p.state.lt = b[30]; p.state.rt = b[31]; p.state.connected = b[6] != 0;
    if (!detail::valid(p)) return std::nullopt;
    return p;
}
}
