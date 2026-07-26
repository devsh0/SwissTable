#pragma once

#include <cstdlib>
#include <cstring>
#include <cstdint>

using u64 = uint64_t;

static constexpr u64 MAX_KEY_LEN = 40;
static constexpr u64 SLOT_SIZE = MAX_KEY_LEN + 1;

struct KeyArena {
    char* base;
    u64 capacity;
    u64 cursor;

    explicit KeyArena(u64 n_slots) : capacity(SLOT_SIZE * n_slots), cursor(0) {
        base = (char*)(aligned_alloc(4096, (capacity + 4095) & ~4095ULL));
        for (u64 i = 0; i < capacity; i += 4096) {
            base[i] = 0;
        }
    }

    ~KeyArena() {
        free(base);
    }

    char* allocate(const char* key) {
        u64 len = strlen(key);
        char* dst = base + cursor;
        memcpy(dst, key, len + 1);
        cursor += len + 1;
        return dst;
    }

    void reset() {
        cursor = 0;
    }
};
