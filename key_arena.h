#pragma once

#include <cstdint>

using u64 = uint64_t;

static constexpr u64 MAX_KEY_LEN = 40;
static constexpr u64 SLOT_SIZE = MAX_KEY_LEN + 1;

struct KeyArena {
    char* base;
    u64 capacity;
    u64 cursor;

    explicit KeyArena(u64 n_slots);
    ~KeyArena();
    char* allocate(const char* key);
    void reset();
};
