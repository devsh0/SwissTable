#pragma once

#include <cstdint>
#include "misc.h"

static constexpr u64 MAX_KEY_LEN = 40 + 1;

struct KeyArena {
    u8* base;
    u64 capacity;
    u64 cursor;
    u8* old_base;
    u64 old_capacity;

    explicit KeyArena(u64 cap);
    ~KeyArena();
    char* allocate(const char* key);
    bool is_full();
    void reset();
    void map_and_prefault();
    void begin_resize(u64 new_capacity);
    void finish_resize();
};
