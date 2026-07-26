#pragma once

#include <cinttypes>
#include <immintrin.h>
#include "key_arena.h"
#include <limits>

#define ERR_EXIT(m) \
    std::printf(m); \
    exit(-1);

using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

static constexpr u64 GROUP_SIZE = 32;
static constexpr u64 SLOT_NONE = std::numeric_limits<u64>::max();
static constexpr u32 VALUE_NIL = std::numeric_limits<u32>::max();

struct Entry {
    char* key = nullptr;
    u32 value = 0;
};

struct SwissTable {
    // Metadata is only 7 bits. Any valid metadata byte will look like
    // 0b0xxxxxxx, i.e. MSB always 0. This is why sentinel values such
    // as SLOT_EMPTY and SLOT_TOMBSTONE need to have MSB set.
    static constexpr u8 SLOT_EMPTY = 0x80;
    static constexpr u8 SLOT_TOMBSTONE = 0xfe;

    u64 total_slots;
    u64 total_groups;
    __m256i empty_slot_vec;
    __m256i tombstone_slot_vec;
    Entry* table;
    u8* metadata;
    KeyArena arena;

    explicit SwissTable(u64 slots = 512);
    ~SwissTable();
    void insert(const char* key, u32 value);
    u32 lookup(const char* key);
    u32 remove(const char* key);
    void dump();

private:
    u64 hash(const char* key);
    // Searches for key using the given meta tag, potentially scanning the entire hash table.
    u64 lookup_slot(const char* key, u64 meta, u64 slot);
    // Searches for key between the members of a group whose meta tag matches the meta tag of key.
    u64 lookup_group(const char* key, u32 match_mask, u64 slot_leader);
    // Searches for a sentinel slot within the group that begins at slot_leader.
    u64 lookup_sentinel_slot_in_group(__m256i sentinel_vec, __m256i leader_vec, u64 slot_leader);
};
