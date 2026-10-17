#pragma once

#include <cstdint>

// Open-loop XYZ one-pulse PWM2: TIM4/PB6, TIM3/PB4, TIM10/PB8.
// One software step is one rising edge. High DIR = +machine. Z capped at 5 kHz.
namespace stepgen
{

enum class Axis : std::uint8_t
{
    X = 0,
    Y = 1,
    Z = 2
};

void init();

// Chase raw pose. Call once per 1 ms while motion is allowed.
void followRawPose(float raw_x_mm, float raw_y_mm, float raw_z_mm);

void stopAndResync(float raw_x_mm, float raw_y_mm, float raw_z_mm);
void emergencyStop();

// Bench: ~2 kHz square wave on one STEP pin for duration_ms.
void startTestTone(Axis axis, std::uint32_t duration_ms);
void stopTestTone();
bool testToneActive();

void tickMillisecond();

std::int32_t stepCount(Axis axis);
std::int32_t targetCount(Axis axis);
bool outputsActive();
std::uint32_t pulseCount();

} // namespace stepgen
