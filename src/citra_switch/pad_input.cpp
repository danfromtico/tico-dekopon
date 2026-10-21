// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <utility>

#include "citra_switch/pad_input.h"

namespace SwitchFrontend {

namespace {

constexpr std::array<std::pair<u64, SwitchFrontend::InputButton>, 16> button_map{{
    {HidNpadButton_A, SwitchFrontend::InputButton::A},
    {HidNpadButton_B, SwitchFrontend::InputButton::B},
    {HidNpadButton_X, SwitchFrontend::InputButton::X},
    {HidNpadButton_Y, SwitchFrontend::InputButton::Y},
    {HidNpadButton_Up, SwitchFrontend::InputButton::Up},
    {HidNpadButton_Down, SwitchFrontend::InputButton::Down},
    {HidNpadButton_Left, SwitchFrontend::InputButton::Left},
    {HidNpadButton_Right, SwitchFrontend::InputButton::Right},
    {HidNpadButton_L, SwitchFrontend::InputButton::L},
    {HidNpadButton_R, SwitchFrontend::InputButton::R},
    {HidNpadButton_Plus, SwitchFrontend::InputButton::Start},
    {HidNpadButton_Minus, SwitchFrontend::InputButton::Select},
    {HidNpadButton_ZL, SwitchFrontend::InputButton::ZL},
    {HidNpadButton_ZR, SwitchFrontend::InputButton::ZR},
    {HidNpadButton_StickL, SwitchFrontend::InputButton::L3},
    {HidNpadButton_StickR, SwitchFrontend::InputButton::R3},
}};

constexpr std::size_t kHandheldSensor = 0;
constexpr std::size_t kFullKeySensor = 1;
constexpr std::size_t kJoyDualLeftSensor = 2;
constexpr std::size_t kJoyDualRightSensor = 3;
constexpr std::size_t kJoyLeftSensor = 4;
constexpr std::size_t kJoyRightSensor = 5;
std::array<HidSixAxisSensorHandle, 6> six_axis_handles{};
bool seven_six_axis_initialized{};
bool seven_six_axis_started{};

} // namespace

void StartSixAxis() {
    hidGetSixAxisSensorHandles(&six_axis_handles[kHandheldSensor], 1, HidNpadIdType_Handheld,
                               HidNpadStyleTag_NpadHandheld);
    hidGetSixAxisSensorHandles(&six_axis_handles[kFullKeySensor], 1, HidNpadIdType_No1,
                               HidNpadStyleTag_NpadFullKey);
    hidGetSixAxisSensorHandles(&six_axis_handles[kJoyDualLeftSensor], 2, HidNpadIdType_No1,
                               HidNpadStyleTag_NpadJoyDual);
    hidGetSixAxisSensorHandles(&six_axis_handles[kJoyLeftSensor], 1, HidNpadIdType_No1,
                               HidNpadStyleTag_NpadJoyLeft);
    hidGetSixAxisSensorHandles(&six_axis_handles[kJoyRightSensor], 1, HidNpadIdType_No1,
                               HidNpadStyleTag_NpadJoyRight);
    for (const HidSixAxisSensorHandle& handle : six_axis_handles) {
        hidStartSixAxisSensor(handle);
    }

    seven_six_axis_initialized = R_SUCCEEDED(hidInitializeSevenSixAxisSensor());
    if (seven_six_axis_initialized) {
        seven_six_axis_started = R_SUCCEEDED(hidStartSevenSixAxisSensor());
    }
}

void StopSixAxis() {
    if (seven_six_axis_started) {
        hidStopSevenSixAxisSensor();
        seven_six_axis_started = false;
    }
    if (seven_six_axis_initialized) {
        hidFinalizeSevenSixAxisSensor();
        seven_six_axis_initialized = false;
    }
    for (const HidSixAxisSensorHandle& handle : six_axis_handles) {
        hidStopSixAxisSensor(handle);
    }
}

namespace {

SwitchFrontend::MotionState ReadSevenSixAxis() {
    if (!seven_six_axis_started) {
        return {};
    }

    HidSevenSixAxisSensorState sensor{};
    std::size_t count{};
    if (R_FAILED(hidGetSevenSixAxisSensorStates(&sensor, 1, &count)) || count == 0) {
        return {};
    }

    // Un-named libnx values that  map to acceleration, angular velocity, and orientation.
    return {
        .active = true,
        .accel_x = sensor.unk_x18[0],
        .accel_y = sensor.unk_x18[1],
        .accel_z = sensor.unk_x18[2],
        .gyro_x = sensor.unk_x18[3],
        .gyro_y = sensor.unk_x18[4],
        .gyro_z = sensor.unk_x18[5],
    };
}

SwitchFrontend::MotionState PollMotion(PadState& pad) {
    const SwitchFrontend::GyroSource source = SwitchFrontend::GetGyroSource();
    if (source == SwitchFrontend::GyroSource::Console) {
        return ReadSevenSixAxis();
    }

    const u64 style_set = padGetStyleSet(&pad);
    // Some compatible Joy-Cons leave the Handheld motion data empty so give them (and old
    // firmwares), a fallback path.
    if ((style_set & HidNpadStyleTag_NpadHandheld) != 0 &&
        source == SwitchFrontend::GyroSource::Automatic) {
        const SwitchFrontend::MotionState motion = ReadSevenSixAxis();
        if (motion.active) {
            return motion;
        }
    }

    HidSixAxisSensorState sensor{};
    bool read = false;

    if ((style_set & HidNpadStyleTag_NpadHandheld) != 0) {
        read = hidGetSixAxisSensorStates(six_axis_handles[kHandheldSensor], &sensor, 1) > 0;
    } else if ((style_set & HidNpadStyleTag_NpadFullKey) != 0) {
        read = hidGetSixAxisSensorStates(six_axis_handles[kFullKeySensor], &sensor, 1) > 0;
    } else if ((style_set & HidNpadStyleTag_NpadJoyDual) != 0) {
        const u64 attributes = padGetAttributes(&pad);
        const bool left_connected = (attributes & HidNpadAttribute_IsLeftConnected) != 0;
        const bool right_connected = (attributes & HidNpadAttribute_IsRightConnected) != 0;
        if (source != SwitchFrontend::GyroSource::RightController && left_connected) {
            read = hidGetSixAxisSensorStates(six_axis_handles[kJoyDualLeftSensor], &sensor, 1) > 0;
        }
        if (!read && source != SwitchFrontend::GyroSource::LeftController && right_connected) {
            read = hidGetSixAxisSensorStates(six_axis_handles[kJoyDualRightSensor], &sensor, 1) > 0;
        }
    } else if ((style_set & HidNpadStyleTag_NpadJoyLeft) != 0 &&
               source != SwitchFrontend::GyroSource::RightController) {
        read = hidGetSixAxisSensorStates(six_axis_handles[kJoyLeftSensor], &sensor, 1) > 0;
    } else if ((style_set & HidNpadStyleTag_NpadJoyRight) != 0 &&
               source != SwitchFrontend::GyroSource::LeftController) {
        read = hidGetSixAxisSensorStates(six_axis_handles[kJoyRightSensor], &sensor, 1) > 0;
    }

    if (!read) {
        return {};
    }
    return {
        .active = true,
        .accel_x = sensor.acceleration.x,
        .accel_y = sensor.acceleration.y,
        .accel_z = sensor.acceleration.z,
        .gyro_x = sensor.angular_velocity.x,
        .gyro_y = sensor.angular_velocity.y,
        .gyro_z = sensor.angular_velocity.z,
    };
}

} // namespace

u64 PollInput(PadState& pad, SwitchFrontend::InputState& state) {
    padUpdate(&pad);
    const u64 held = padGetButtons(&pad);
    const HidAnalogStickState left = padGetStickPos(&pad, 0);
    const HidAnalogStickState right = padGetStickPos(&pad, 1);

    state = SwitchFrontend::InputState{
        .left_x = left.x,
        .left_y = left.y,
        .right_x = right.x,
        .right_y = right.y,
        .motion = PollMotion(pad),
    };
    for (const auto& [source, target] : button_map) {
        if ((held & source) != 0) {
            state.buttons |= SwitchFrontend::ButtonMask(target);
        }
    }

    HidTouchScreenState touch{};
    if (hidGetTouchScreenStates(&touch, 1) != 0 && touch.count > 0) {
        state.touch_pressed = true;
        state.touch_x = touch.touches[0].x;
        state.touch_y = touch.touches[0].y;
    }
    return held;
}

} // namespace SwitchFrontend
