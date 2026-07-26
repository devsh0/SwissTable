#pragma once

#include <chrono>
#include <cstdint>
#include <cstring>
#include <unordered_set>
#include <string>

using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

struct Entry {
    char* key = nullptr;
    u32 value = 0;
};

struct EntryGen {
    u64 state;

    explicit EntryGen(u64 seed) : state(seed) {}

    u64 next_u64() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }

    u32 next_u32() {
        return (u32)next_u64();
    }

    u32 next_range(u32 lo, u32 hi) {
        return lo + (next_u32() % (hi - lo + 1));
    }

    Entry generate() {
        u32 len = next_range(3, 40);
        char* buf = new char[len + 1];
        for (u32 i = 0; i < len; i++) {
            buf[i] = 'a' + (next_u32() % 26);
        }
        buf[len] = '\0';
        Entry e;
        e.key = buf;
        e.value = next_u32();
        return e;
    }

    void generate_n(Entry* out, u32 n) {
        std::unordered_set<std::string> seen;
        for (u32 i = 0; i < n; i++) {
            Entry e;
            do { e = generate(); } while (!seen.insert(e.key).second);
            out[i] = e;
        }
    }
};

inline u64 ns() {
    auto t = std::chrono::high_resolution_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t).count();
}

inline u64 ms() {
    auto t = std::chrono::high_resolution_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
}
