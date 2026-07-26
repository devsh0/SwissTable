#pragma once

#include <cinttypes>
#include <limits>

#define ERR_EXIT(m) do { std::printf(m); exit(-1); } while (0)

using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

static constexpr u32 VALUE_NIL = std::numeric_limits<u32>::max();
