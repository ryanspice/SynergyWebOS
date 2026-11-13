#include "synergy/input.hpp"
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <climits>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace fixture {
struct DeviceCapture {
    int fd = -1;
    bool descriptor_written = false;
    uinput_user_dev descriptor{};
    std::vector<std::pair<unsigned long, int>> capabilities;
    std::vector<std::vector<input_event>> frames;
    bool created = false;
    bool destroyed = false;
    int close_count = 0;
};
bool device_present = true;
uid_t effective_uid = 0;
int next_fd = 100;
int stat_calls = 0;
int open_calls = 0;
int write_calls = 0;
int ioctl_calls = 0;
enum class DiscoveryFailure { none, load, symbol, request };
DiscoveryFailure discovery_failure = DiscoveryFailure::none;
int discovery_result = 0;
int dlopen_calls = 0;
int dlsym_calls = 0;
int dlclose_calls = 0;
int scan_calls = 0;
int created_at_dlopen = 0;
int created_at_scan = 0;
bool discovery_requested = false;
bool library_open = false;
bool event_write_before_scan = false;
std::map<int, DeviceCapture> devices;
std::vector<int> opened_fds;

void reset(bool present = true, uid_t uid = 0) {
    device_present = present;
    effective_uid = uid;
    next_fd = 100;
    stat_calls = open_calls = write_calls = ioctl_calls = 0;
    discovery_failure = DiscoveryFailure::none;
    discovery_result = 0;
    dlopen_calls = dlsym_calls = dlclose_calls = scan_calls = 0;
    created_at_dlopen = created_at_scan = 0;
    discovery_requested = library_open = event_write_before_scan = false;
    devices.clear();
    opened_fds.clear();
}
void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
int request_device_scan() {
    ++fixture::scan_calls;
    fixture::created_at_scan = 0;
    for (const auto& pair : fixture::devices)
        if (pair.second.created && !pair.second.destroyed) ++fixture::created_at_scan;
    fixture::discovery_requested = true;
    return fixture::discovery_result;
}
}

extern "C" int __real_stat(const char*, struct stat*);
extern "C" int __wrap_stat(const char* path, struct stat* result) {
    if (std::strcmp(path, "/dev/uinput") == 0 || std::strcmp(path, "/dev/input/uinput") == 0) {
        ++fixture::stat_calls;
        if (!fixture::device_present) { errno = ENOENT; return -1; }
        std::memset(result, 0, sizeof(*result));
        result->st_mode = S_IFCHR | 0600;
        return 0;
    }
    return __real_stat(path, result);
}
extern "C" int __real_open(const char*, int, ...);
extern "C" int __wrap_open(const char* path, int flags, ...) {
    if (std::strcmp(path, "/dev/uinput") == 0 || std::strcmp(path, "/dev/input/uinput") == 0) {
        ++fixture::open_calls;
        if (!fixture::device_present) { errno = ENOENT; return -1; }
        const int fd = fixture::next_fd++;
        fixture::devices[fd].fd = fd;
        fixture::opened_fds.push_back(fd);
        (void)flags;
        return fd;
    }
    return __real_open(path, flags);
}
extern "C" int __wrap_ioctl(int fd, unsigned long request, ...) {
    auto found = fixture::devices.find(fd);
    if (found == fixture::devices.end()) { errno = EBADF; return -1; }
    ++fixture::ioctl_calls;
    auto& device = found->second;
    if (request == UI_DEV_CREATE) { device.created = true; return 0; }
    if (request == UI_DEV_DESTROY) { device.destroyed = true; return 0; }
    va_list args;
    va_start(args, request);
    const int argument = va_arg(args, int);
    va_end(args);
    device.capabilities.emplace_back(request, argument);
    return 0;
}
extern "C" ssize_t __real_write(int, const void*, size_t);
extern "C" ssize_t __wrap_write(int fd, const void* data, size_t size) {
    auto found = fixture::devices.find(fd);
    if (found == fixture::devices.end()) return __real_write(fd, data, size);
    ++fixture::write_calls;
    auto& device = found->second;
    if (size == sizeof(uinput_user_dev)) {
        std::memcpy(&device.descriptor, data, size);
        device.descriptor_written = true;
        return static_cast<ssize_t>(size);
    }
    if (!fixture::discovery_requested) fixture::event_write_before_scan = true;
    if (size == 0 || size % sizeof(input_event) != 0) { errno = EINVAL; return -1; }
    const auto* events = static_cast<const input_event*>(data);
    device.frames.emplace_back(events, events + size / sizeof(input_event));
    return static_cast<ssize_t>(size);
}
extern "C" int __real_close(int);
extern "C" int __wrap_close(int fd) {
    auto found = fixture::devices.find(fd);
    if (found == fixture::devices.end()) return __real_close(fd);
    ++found->second.close_count;
    return 0;
}
extern "C" uid_t __wrap_geteuid() { return fixture::effective_uid; }
extern "C" void* __wrap_dlopen(const char* path, int flags) {
    ++fixture::dlopen_calls;
    fixture::created_at_dlopen = 0;
    for (const auto& pair : fixture::devices)
        if (pair.second.created && !pair.second.destroyed) ++fixture::created_at_dlopen;
    fixture::check(std::strcmp(path, "/usr/lib/libim.so.1.0.0") == 0,
                   "input discovery loaded an unexpected runtime path");
    fixture::check(flags == (RTLD_NOW | RTLD_LOCAL),
                   "input discovery used unexpected dynamic-loader flags");
    if (fixture::discovery_failure == fixture::DiscoveryFailure::load) return nullptr;
    fixture::library_open = true;
    return reinterpret_cast<void*>(static_cast<uintptr_t>(0x1));
}
extern "C" void* __wrap_dlsym(void* handle, const char* symbol) {
    ++fixture::dlsym_calls;
    fixture::check(fixture::library_open && handle == reinterpret_cast<void*>(static_cast<uintptr_t>(0x1)),
                   "input discovery resolved a symbol without the opened runtime");
    fixture::check(std::strcmp(symbol, "IM_RequestToCheckInput") == 0,
                   "input discovery requested an unexpected symbol");
    if (fixture::discovery_failure == fixture::DiscoveryFailure::symbol) return nullptr;
    return reinterpret_cast<void*>(&fixture::request_device_scan);
}
extern "C" int __wrap_dlclose(void* handle) {
    ++fixture::dlclose_calls;
    fixture::check(fixture::library_open && handle == reinterpret_cast<void*>(static_cast<uintptr_t>(0x1)),
                   "input discovery closed an unexpected runtime handle");
    fixture::library_open = false;
    return 0;
}

namespace {
using fixture::check;
using fixture::DeviceCapture;
void check_no_discovery_calls() {
    check(fixture::dlopen_calls == 0 && fixture::dlsym_calls == 0 && fixture::dlclose_calls == 0 && fixture::scan_calls == 0,
          "input discovery loader was called on a refused or dry-run backend");
}
void check_failed_backend_cleanup() {
    check(fixture::opened_fds.size() == 2, "failed discovery did not construct both uinput devices first");
    for (const auto& pair : fixture::devices) {
        const auto& device = pair.second;
        check(device.created && device.destroyed && device.close_count == 1,
              "failed discovery did not destroy and close both created uinput devices");
    }
}
void test_discovery_refusal_ordering() {
    fixture::reset(true, 1000);
    bool nonroot_refused = false;
    try { (void)synergy::make_input(false); }
    catch (const std::runtime_error&) { nonroot_refused = true; }
    check(nonroot_refused, "real input did not refuse a nonroot caller");
    check_no_discovery_calls();

    fixture::reset(false, 0);
    bool missing_refused = false;
    try { (void)synergy::make_input(false); }
    catch (const std::runtime_error&) { missing_refused = true; }
    check(missing_refused, "real input did not refuse absent uinput nodes");
    check_no_discovery_calls();

    fixture::reset(true, 0);
    auto dry = synergy::make_input(true);
    dry.reset();
    check_no_discovery_calls();
}
void test_discovery_load_failure_cleanup() {
    fixture::reset(true, 0);
    fixture::discovery_failure = fixture::DiscoveryFailure::load;
    bool failed = false;
    try { (void)synergy::make_input(false); }
    catch (const std::runtime_error& e) { failed = std::string(e.what()).find("Cannot load webOS input discovery runtime") != std::string::npos; }
    check(failed, "missing input discovery runtime did not fail closed");
    check(fixture::dlopen_calls == 1 && fixture::dlsym_calls == 0 && fixture::dlclose_calls == 0 && fixture::scan_calls == 0,
          "runtime-load failure used unexpected discovery calls");
    check(fixture::created_at_dlopen == 2 && !fixture::event_write_before_scan,
          "runtime load was not requested after both devices and before events");
    check_failed_backend_cleanup();
}
void test_discovery_symbol_failure_cleanup() {
    fixture::reset(true, 0);
    fixture::discovery_failure = fixture::DiscoveryFailure::symbol;
    bool failed = false;
    try { (void)synergy::make_input(false); }
    catch (const std::runtime_error& e) { failed = std::string(e.what()).find("webOS input discovery API unavailable") != std::string::npos; }
    check(failed, "missing input discovery symbol did not fail closed");
    check(fixture::dlopen_calls == 1 && fixture::dlsym_calls == 1 && fixture::dlclose_calls == 1 && fixture::scan_calls == 0,
          "missing-symbol failure did not close the opened runtime exactly once");
    check(fixture::created_at_dlopen == 2 && !fixture::event_write_before_scan && !fixture::library_open,
          "symbol resolution was out of order or left the runtime open");
    check_failed_backend_cleanup();
}
void test_discovery_request_failure_cleanup() {
    fixture::reset(true, 0);
    fixture::discovery_failure = fixture::DiscoveryFailure::request;
    fixture::discovery_result = -1;
    bool failed = false;
    try { (void)synergy::make_input(false); }
    catch (const std::runtime_error& e) { failed = std::string(e.what()).find("webOS input device scan request failed") != std::string::npos; }
    check(failed, "failed input scan request did not fail closed");
    check(fixture::dlopen_calls == 1 && fixture::dlsym_calls == 1 && fixture::dlclose_calls == 1 && fixture::scan_calls == 1,
          "failed scan request did not close the opened runtime exactly once");
    check(fixture::created_at_dlopen == 2 && fixture::created_at_scan == 2 && !fixture::event_write_before_scan && !fixture::library_open,
          "scan request was out of order or left the runtime open");
    check_failed_backend_cleanup();
}
void test_discovery_repeats_for_reconnected_backend() {
    fixture::reset(true, 0);
    {
        auto first = synergy::make_input(false);
        check(fixture::dlopen_calls == 1 && fixture::dlsym_calls == 1 && fixture::dlclose_calls == 1 && fixture::scan_calls == 1,
              "first backend creation did not make exactly one discovery request");
        check(fixture::created_at_dlopen == 2 && fixture::created_at_scan == 2,
              "first discovery request preceded creation of both devices");
    }
    {
        auto reconnected = synergy::make_input(false);
        check(fixture::dlopen_calls == 2 && fixture::dlsym_calls == 2 && fixture::dlclose_calls == 2 && fixture::scan_calls == 2,
              "reconnected backend did not make exactly one fresh discovery request");
        check(fixture::created_at_dlopen == 2 && fixture::created_at_scan == 2,
              "reconnect discovery did not follow creation of its new device pair");
        reconnected->key(KEY_A, 1);
        check(!fixture::event_write_before_scan, "input event was written before its discovery request");
    }
    check(fixture::opened_fds.size() == 4, "reconnect fixture did not create a second device pair");
    for (const auto& pair : fixture::devices) {
        const auto& device = pair.second;
        check(device.created && device.destroyed && device.close_count == 1,
              "reconnected backend did not clean up its uinput devices");
    }
}
void test_nonroot_refusal() {
    fixture::reset(true, 1000);
    bool refused = false;
    try { (void)synergy::make_input(false); }
    catch (const std::runtime_error& e) { refused = std::string(e.what()).find("root") != std::string::npos; }
    check(refused, "real input did not refuse a nonroot caller");
    check(fixture::stat_calls == 0 && fixture::open_calls == 0, "nonroot refusal touched uinput discovery/open");
    check_no_discovery_calls();
}
void test_missing_device_refusal() {
    fixture::reset(false, 0);
    bool refused = false;
    try { (void)synergy::make_input(false); }
    catch (const std::runtime_error& e) { refused = std::string(e.what()).find("No uinput") != std::string::npos; }
    check(refused, "real input did not refuse absent uinput nodes");
    check(fixture::stat_calls == 2 && fixture::open_calls == 0, "missing-device refusal opened a device or skipped discovery");
    check_no_discovery_calls();
}
void test_dry_run_never_touches_device() {
    fixture::reset(true, 0);
    int callbacks = 0;
    auto input = synergy::make_input(true, [&] { ++callbacks; });
    input->key(KEY_A, 1);
    input->button(BTN_LEFT, 1);
    input->move(0, 0);
    input->wheel(0, 0);
    synergy::bridge::GamepadState pad{};
    pad.connected = true;
    check(input->gamepad(0, pad), "dry-run accepts a controller state");
    check(callbacks == 3, "dry-run gamepad frame counts as one logical input event");
    check(fixture::stat_calls == 0 && fixture::open_calls == 0 && fixture::ioctl_calls == 0 && fixture::write_calls == 0,
          "dry-run made a device syscall");
    check_no_discovery_calls();
}
bool has_capability(const DeviceCapture& device, unsigned long request, int value) {
    for (const auto& item : device.capabilities) if (item.first == request && item.second == value) return true;
    return false;
}
void check_frame(const DeviceCapture& d, size_t index, std::initializer_list<std::tuple<unsigned short, unsigned short, int>> expected) {
    check(index < d.frames.size(), "missing expected input frame");
    const auto& frame = d.frames[index];
    check(frame.size() == expected.size() + 1, "input frame has incorrect record count");
    size_t at = 0;
    for (const auto& want : expected) {
        const auto& event = frame[at++];
        check(event.type == std::get<0>(want) && event.code == std::get<1>(want) && event.value == std::get<2>(want),
              "input event payload/order mismatch");
    }
    const auto& syn = frame.back();
    check(syn.type == EV_SYN && syn.code == SYN_REPORT && syn.value == 0, "frame missing terminal SYN_REPORT");
}
void test_production_uinput_devices_and_frames() {
    fixture::reset(true, 0);
    int metered = 0;
    {
        auto input = synergy::make_input(false, [&] { ++metered; });
        check(fixture::opened_fds.size() == 2, "expected keyboard and mouse uinput descriptors");
        check(fixture::dlopen_calls == 1 && fixture::dlsym_calls == 1 && fixture::dlclose_calls == 1 && fixture::scan_calls == 1,
              "production backend construction did not make exactly one discovery request");
        check(fixture::created_at_dlopen == 2 && fixture::created_at_scan == 2 && !fixture::event_write_before_scan && !fixture::library_open,
              "production discovery request was out of order or left the runtime open");
        const auto& keyboard = fixture::devices.at(fixture::opened_fds[0]);
        const auto& mouse = fixture::devices.at(fixture::opened_fds[1]);
        check(std::string(keyboard.descriptor.name) == "SynergyWebOS Native Keyboard", "keyboard name changed");
        check(std::string(mouse.descriptor.name) == "SynergyWebOS Native Mouse", "mouse name changed");
        for (const auto* device : {&keyboard, &mouse}) {
            check(device->descriptor_written, "missing uinput_user_dev descriptor write");
            const auto& id = device->descriptor.id;
            check(id.bustype == BUS_USB && id.vendor == 0x1d6b && id.product == 0x0104 && id.version == 1,
                  "legacy-compatible HID identity mismatch");
            check(device->created, "uinput device was not created");
            check(has_capability(*device, UI_SET_EVBIT, EV_KEY), "EV_KEY capability missing");
        }
        check(has_capability(keyboard, UI_SET_KEYBIT, KEY_A), "keyboard key capability missing");
        check(has_capability(keyboard, UI_SET_KEYBIT, 255), "keyboard upper key capability missing");
        for (int code : {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA})
            check(has_capability(mouse, UI_SET_KEYBIT, code), "mouse button capability missing");
        check(has_capability(mouse, UI_SET_PROPBIT, INPUT_PROP_POINTER), "mouse pointer property missing");
        check(has_capability(mouse, UI_SET_EVBIT, EV_ABS), "mouse absolute event capability missing");
        for (int code : {ABS_X, ABS_Y})
            check(has_capability(mouse, UI_SET_ABSBIT, code), "mouse absolute axis capability missing");
        check(has_capability(mouse, UI_SET_EVBIT, EV_REL), "mouse EV_REL capability missing");
        for (int code : {REL_WHEEL, REL_HWHEEL})
            check(has_capability(mouse, UI_SET_RELBIT, code), "relative wheel capability missing");
        check(!has_capability(mouse, UI_SET_RELBIT, REL_X) && !has_capability(mouse, UI_SET_RELBIT, REL_Y),
              "absolute mouse unexpectedly advertises relative X/Y motion");
        check(mouse.descriptor.absmin[ABS_X] == 0 && mouse.descriptor.absmin[ABS_Y] == 0 &&
              mouse.descriptor.absmax[ABS_X] == 1919 && mouse.descriptor.absmax[ABS_Y] == 1079,
              "default mouse descriptor does not match 1920x1080 coordinates");

        input->key(KEY_A, 1);
        input->button(BTN_LEFT, 1);
        input->move(12, -7);
        input->wheel(3, -2);
        input->move(0, 0);
        input->wheel(0, 0);
        check(metered == 4, "event metering or zero-axis suppression changed");
        check(keyboard.frames.size() == 1 && mouse.frames.size() == 3, "zero movement emitted I/O or frame count changed");
        check_frame(keyboard, 0, {{EV_KEY, KEY_A, 1}});
        check_frame(mouse, 0, {{EV_KEY, BTN_LEFT, 1}});
        check_frame(mouse, 1, {{EV_ABS, ABS_X, 12}, {EV_ABS, ABS_Y, 0}});
        check_frame(mouse, 2, {{EV_REL, REL_HWHEEL, 3}, {EV_REL, REL_WHEEL, -2}});
        check(fixture::write_calls == 6, "expected two descriptor writes and four whole event-frame writes");
    }
    check(fixture::devices.size() == 2, "unexpected descriptor count");
    for (const auto& pair : fixture::devices) {
        const auto& device = pair.second;
        check(device.destroyed && device.close_count == 1, "uinput device cleanup did not destroy and close each descriptor");
    }
}
void test_gamepad_devices_and_frames() {
    fixture::reset(true, 0);
    int metered = 0;
    {
        auto input = synergy::make_input(false, [&] { ++metered; });
        check(fixture::opened_fds.size() == 2, "gamepad backend starts with only keyboard and mouse devices");
        const auto keyboard_fd = fixture::opened_fds[0];
        const auto mouse_fd = fixture::opened_fds[1];
        const auto& keyboard = fixture::devices.at(keyboard_fd);
        const auto& mouse = fixture::devices.at(mouse_fd);
        check(std::string(keyboard.descriptor.name) == "SynergyWebOS Native Keyboard" &&
              std::string(mouse.descriptor.name) == "SynergyWebOS Native Mouse" &&
              has_capability(mouse, UI_SET_PROPBIT, INPUT_PROP_POINTER),
              "adding controller support preserves keyboard and pointer descriptors");

        synergy::bridge::GamepadState neutral{};
        check(input->gamepad(2, neutral), "disconnected absent slot is accepted as neutral");
        check(fixture::opened_fds.size() == 2 && fixture::scan_calls == 1,
              "disconnected absent slot allocates no device and triggers no rescan");
        check(metered == 1, "absent neutral controller report is counted once logically");

        synergy::bridge::GamepadState state{};
        state.buttons = 0xf3f9; // All canonical buttons, D-pad up and right.
        state.lx = -32768;
        state.ly = 32767;
        state.rx = -12345;
        state.ry = 23456;
        state.lt = 255;
        state.rt = 128;
        state.connected = true;
        check(input->gamepad(2, state), "connected controller frame is accepted");
        check(fixture::opened_fds.size() == 3 && fixture::scan_calls == 2 &&
              fixture::created_at_scan == 3 && fixture::created_at_dlopen == 3 &&
              !fixture::event_write_before_scan && !fixture::library_open,
              "first connected frame creates one slot device and rescans after creation");
        check(metered == 2, "connected controller frame is metered exactly once");
        const auto gamepad_fd = fixture::opened_fds[2];
        const auto& gamepad = fixture::devices.at(gamepad_fd);
        check(std::string(gamepad.descriptor.name) == "SynergyWebOS Gamepad 3" &&
              gamepad.descriptor_written && gamepad.created,
              "slot two has its own lazy uinput gamepad device");
        check(has_capability(gamepad, UI_SET_EVBIT, EV_KEY) && has_capability(gamepad, UI_SET_EVBIT, EV_ABS),
              "gamepad advertises key and absolute event classes");
        for (int code : {BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH, BTN_TL, BTN_TR,
                         BTN_SELECT, BTN_START, BTN_THUMBL, BTN_THUMBR})
            check(has_capability(gamepad, UI_SET_KEYBIT, code), "gamepad canonical button capability missing");
        for (int axis : {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y})
            check(has_capability(gamepad, UI_SET_ABSBIT, axis), "gamepad axis capability missing");
        check(!has_capability(gamepad, UI_SET_PROPBIT, INPUT_PROP_POINTER),
              "gamepad is not advertised as an absolute pointer");
        for (int axis : {ABS_X, ABS_Y, ABS_RX, ABS_RY})
            check(gamepad.descriptor.absmin[axis] == -32768 && gamepad.descriptor.absmax[axis] == 32767,
                  "gamepad stick range is signed 16-bit");
        for (int axis : {ABS_Z, ABS_RZ})
            check(gamepad.descriptor.absmin[axis] == 0 && gamepad.descriptor.absmax[axis] == 255,
                  "gamepad trigger range is 0..255");
        for (int axis : {ABS_HAT0X, ABS_HAT0Y})
            check(gamepad.descriptor.absmin[axis] == -1 && gamepad.descriptor.absmax[axis] == 1,
                  "gamepad hat range is -1..1");
        check(gamepad.frames.size() == 1, "new gamepad writes its first complete state frame");
        check_frame(gamepad, 0, {
            {EV_KEY, BTN_SOUTH, 1}, {EV_KEY, BTN_EAST, 1}, {EV_KEY, BTN_WEST, 1}, {EV_KEY, BTN_NORTH, 1},
            {EV_KEY, BTN_TL, 1}, {EV_KEY, BTN_TR, 1}, {EV_KEY, BTN_SELECT, 1}, {EV_KEY, BTN_START, 1},
            {EV_KEY, BTN_THUMBL, 1}, {EV_KEY, BTN_THUMBR, 1},
            {EV_ABS, ABS_X, -32768}, {EV_ABS, ABS_Y, 32767}, {EV_ABS, ABS_RX, -12345}, {EV_ABS, ABS_RY, 23456},
            {EV_ABS, ABS_Z, 255}, {EV_ABS, ABS_RZ, 128}, {EV_ABS, ABS_HAT0X, 1}, {EV_ABS, ABS_HAT0Y, -1}
        });

        check(input->gamepad(2, neutral), "disconnect of an existing gamepad is accepted");
        check(gamepad.frames.size() == 2 && metered == 3,
              "existing gamepad disconnect emits one fully neutral frame");
        check_frame(gamepad, 1, {
            {EV_KEY, BTN_SOUTH, 0}, {EV_KEY, BTN_EAST, 0}, {EV_KEY, BTN_WEST, 0}, {EV_KEY, BTN_NORTH, 0},
            {EV_KEY, BTN_TL, 0}, {EV_KEY, BTN_TR, 0}, {EV_KEY, BTN_SELECT, 0}, {EV_KEY, BTN_START, 0},
            {EV_KEY, BTN_THUMBL, 0}, {EV_KEY, BTN_THUMBR, 0},
            {EV_ABS, ABS_X, 0}, {EV_ABS, ABS_Y, 0}, {EV_ABS, ABS_RX, 0}, {EV_ABS, ABS_RY, 0},
            {EV_ABS, ABS_Z, 0}, {EV_ABS, ABS_RZ, 0}, {EV_ABS, ABS_HAT0X, 0}, {EV_ABS, ABS_HAT0Y, 0}
        });

        const auto devices_before_invalid = fixture::opened_fds.size();
        auto invalid_mask = state;
        invalid_mask.buttons = 0x0c00;
        bool mask_rejected = false;
        try { (void)input->gamepad(2, invalid_mask); }
        catch (const std::runtime_error&) { mask_rejected = true; }
        bool slot_rejected = false;
        try { (void)input->gamepad(4, neutral); }
        catch (const std::runtime_error&) { slot_rejected = true; }
        auto invalid_disconnected = neutral;
        invalid_disconnected.lt = 1;
        bool state_rejected = false;
        try { (void)input->gamepad(2, invalid_disconnected); }
        catch (const std::runtime_error&) { state_rejected = true; }
        check(mask_rejected && slot_rejected && state_rejected &&
              fixture::opened_fds.size() == devices_before_invalid && fixture::scan_calls == 2 &&
              gamepad.frames.size() == 2,
              "invalid button mask, slot, and disconnected state fail without allocation or events");

        check(input->gamepad(3, state), "fourth controller slot is supported");
        check(fixture::opened_fds.size() == 4 && fixture::scan_calls == 3 && fixture::created_at_scan == 4,
              "a second connected slot creates its own device and triggers a rescan");
        const auto second_pad_fd = fixture::opened_fds[3];
        check(std::string(fixture::devices.at(second_pad_fd).descriptor.name) == "SynergyWebOS Gamepad 4",
              "fourth slot keeps its own device identity");
    }
    check(fixture::devices.size() == 4, "gamepad fixture created only two base devices and two requested pads");
    for (const auto& pair : fixture::devices) {
        const auto& device = pair.second;
        check(device.created && device.destroyed && device.close_count == 1,
              "keyboard, mouse, and gamepad descriptors are destroyed and closed once");
    }

    fixture::reset(true, 0);
    {
        auto input = synergy::make_input(false);
        fixture::discovery_result = -1;
        synergy::bridge::GamepadState state{};
        state.connected = true;
        bool failed = false;
        try { (void)input->gamepad(0, state); }
        catch (const std::runtime_error& e) {
            failed = std::string(e.what()).find("webOS input device scan request failed") != std::string::npos;
        }
        check(failed && fixture::opened_fds.size() == 3 && fixture::scan_calls == 2 &&
              fixture::created_at_scan == 3 && !fixture::library_open,
              "failed lazy-gamepad rescan reports failure and closes the loader");
        const auto& partial = fixture::devices.at(fixture::opened_fds[2]);
        check(partial.created && partial.destroyed && partial.close_count == 1,
              "gamepad created before a failed rescan is destroyed and closed");
        input.reset();
    }
    for (const auto& pair : fixture::devices) {
        const auto& device = pair.second;
        check(device.destroyed && device.close_count == 1,
              "base devices are cleaned up after gamepad discovery failure");
    }
}
void test_absolute_positions_and_clamping() {
    fixture::reset(true, 0);
    int metered = 0;
    {
        auto input = synergy::make_input(false, [&] { ++metered; }, 1920, 1080);
        check(fixture::opened_fds.size() == 2, "1920x1080 absolute backend did not create both devices");
        const auto mouse_fd = fixture::opened_fds[1];
        const auto& mouse = fixture::devices.at(mouse_fd);
        check(mouse.descriptor.absmax[ABS_X] == 1919 && mouse.descriptor.absmax[ABS_Y] == 1079,
              "1920x1080 absolute axis bounds are incorrect");

        check(input->position(123, 456), "production backend rejected absolute positioning");
        check(input->position(-40, 2000), "production backend rejected clamped absolute positioning");
        input->move(INT_MAX, INT_MIN);
        input->move(INT_MIN, INT_MAX);
        input->move(0, 0);
        check(mouse.frames.size() == 4, "zero relative move emitted a frame or clamped moves were lost");
        check_frame(mouse, 0, {{EV_ABS, ABS_X, 123}, {EV_ABS, ABS_Y, 456}});
        check_frame(mouse, 1, {{EV_ABS, ABS_X, 0}, {EV_ABS, ABS_Y, 1079}});
        check_frame(mouse, 2, {{EV_ABS, ABS_X, 1919}, {EV_ABS, ABS_Y, 0}});
        check_frame(mouse, 3, {{EV_ABS, ABS_X, 0}, {EV_ABS, ABS_Y, 1079}});
        check(metered == 4, "absolute positioning and accumulated movement metering changed");
        input.reset();
    }

    fixture::reset(true, 0);
    {
        auto input = synergy::make_input(false, {}, 3840, 2160);
        check(fixture::opened_fds.size() == 2, "3840x2160 absolute backend did not create both devices");
        const auto& mouse = fixture::devices.at(fixture::opened_fds[1]);
        check(mouse.descriptor.absmin[ABS_X] == 0 && mouse.descriptor.absmin[ABS_Y] == 0 &&
              mouse.descriptor.absmax[ABS_X] == 3839 && mouse.descriptor.absmax[ABS_Y] == 2159,
              "3840x2160 absolute axis bounds are incorrect");
        input->position(3839, 2159);
        check_frame(mouse, 0, {{EV_ABS, ABS_X, 3839}, {EV_ABS, ABS_Y, 2159}});
    }

    fixture::reset(true, 0);
    {
        auto input = synergy::make_input(false, {}, 1, 1);
        check(fixture::opened_fds.size() == 2, "1x1 absolute backend did not create both devices");
        const auto& mouse = fixture::devices.at(fixture::opened_fds[1]);
        check(mouse.descriptor.absmin[ABS_X] == 0 && mouse.descriptor.absmin[ABS_Y] == 0 &&
              mouse.descriptor.absmax[ABS_X] == 1 && mouse.descriptor.absmax[ABS_Y] == 1,
              "1x1 absolute descriptor did not retain a valid minimum axis range");
        check(input->position(INT_MAX, INT_MIN), "1x1 absolute positioning was rejected");
        input->move(INT_MAX, INT_MIN);
        check(mouse.frames.size() == 2, "1x1 clamped absolute positioning or move frame missing");
        check_frame(mouse, 0, {{EV_ABS, ABS_X, 0}, {EV_ABS, ABS_Y, 0}});
        check_frame(mouse, 1, {{EV_ABS, ABS_X, 0}, {EV_ABS, ABS_Y, 0}});
    }
}
void test_invalid_dimensions_have_no_device_side_effects() {
    for (const auto& dimensions : {std::pair<int, int>{0, 1080}, {1920, 0}, {-1, 1080}, {1920, -1}, {32768, 1080}, {1920, 32768}}) {
        fixture::reset(true, 0);
        bool refused = false;
        try { (void)synergy::make_input(false, {}, dimensions.first, dimensions.second); }
        catch (const std::runtime_error&) { refused = true; }
        check(refused, "invalid screen dimensions were accepted");
        check(fixture::stat_calls == 0 && fixture::open_calls == 0 && fixture::ioctl_calls == 0 && fixture::write_calls == 0,
              "invalid screen dimensions caused uinput syscalls");
        check_no_discovery_calls();
    }
}
void test_absolute_nudge_restores_each_corner() {
    fixture::reset(true, 0);
    auto input = synergy::make_input(false, {}, 1920, 1080);
    const auto& mouse = fixture::devices.at(fixture::opened_fds[1]);
    const auto assert_corner = [&](int x, int y, int nudge_x, int nudge_y) {
        const size_t before = mouse.frames.size();
        input->position(x, y);
        input->nudge();
        check(mouse.frames.size() == before + 3, "absolute nudge did not emit a movement and exact restoration");
        check_frame(mouse, before + 1, {{EV_ABS, ABS_X, nudge_x}, {EV_ABS, ABS_Y, nudge_y}});
        check_frame(mouse, before + 2, {{EV_ABS, ABS_X, x}, {EV_ABS, ABS_Y, y}});
    };
    assert_corner(0, 0, 1, 0);
    assert_corner(1919, 0, 1918, 0);
    assert_corner(0, 1079, 1, 1079);
    assert_corner(1919, 1079, 1918, 1079);
}
}

int main() {
    try {
        test_discovery_refusal_ordering();
        test_discovery_load_failure_cleanup();
        test_discovery_symbol_failure_cleanup();
        test_discovery_request_failure_cleanup();
        test_discovery_repeats_for_reconnected_backend();
        test_nonroot_refusal();
        test_missing_device_refusal();
        test_dry_run_never_touches_device();
        test_production_uinput_devices_and_frames();
        test_gamepad_devices_and_frames();
        test_absolute_positions_and_clamping();
        test_invalid_dimensions_have_no_device_side_effects();
        test_absolute_nudge_restores_each_corner();
        std::cout << "PASS: production input.cpp host syscall fixture; 13 scenarios\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
