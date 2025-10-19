#pragma once

extern "C" {

#include "freertos/FreeRTOS.h"

}

template<typename T>
inline constexpr bool always_false_v = false;

// helper type for visitors
template<class... Ts>
struct overloads : Ts... { using Ts::operator()...; };

constexpr TickType_t kNoWait = pdMS_TO_TICKS(0);