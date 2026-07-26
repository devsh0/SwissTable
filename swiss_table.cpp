#include <cassert>

#include "util.h"
#include <immintrin.h>
#include <cinttypes>
#include <iostream>
#include <limits>
#include <unistd.h>

#define ERR_EXIT(m) \
    std::printf(m); \
    exit(-1);

using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

static constexpr u64 GROUP_SIZE = 32;
static constexpr u64 SLOT_NONE = std::numeric_limits<u64>::max();
static constexpr u32 VALUE_NIL = std::numeric_limits<u32>::max();

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

    explicit SwissTable(u64 slots = 512) : total_slots(slots), total_groups(slots / GROUP_SIZE) {
        empty_slot_vec = _mm256_set1_epi8(SLOT_EMPTY);
        tombstone_slot_vec = _mm256_set1_epi8(SLOT_TOMBSTONE);
        table = new Entry[total_slots]();
        metadata = new u8[total_slots];
        memset(metadata, SLOT_EMPTY, total_slots);
    }

    ~SwissTable() {
        for (u64 i = 0; i < total_slots; i++) {
            free(table[i].key);
        }
        delete[] table;
        delete[] metadata;
    }

    void insert(const char* key, u32 value) {
        u64 h = hash(key);
        u64 slot = (h >> 7) % total_slots;
        u64 slot_leader = (slot / GROUP_SIZE) * GROUP_SIZE;
        u64 meta = h & 0x0000007f;
        slot = lookup_slot(key, meta, slot);
        if (slot != SLOT_NONE) {
            free(table[slot].key);
            table[slot].key = strdup(key);
            table[slot].value = value;
            return;
        }
        for (u64 i = 0; i < total_groups; i++) {
            __m256i leader_vec = _mm256_loadu_si256((__m256i*)(metadata + slot_leader));
            slot = lookup_sentinel_slot_in_group(empty_slot_vec, leader_vec, slot_leader);
            if (slot != SLOT_NONE) {
                // There's an empty slot available in this group.
                table[slot].key = strdup(key);
                table[slot].value = value;
                metadata[slot] = meta;
                return;
            }
            slot = lookup_sentinel_slot_in_group(tombstone_slot_vec, leader_vec, slot_leader);
            if (slot != SLOT_NONE) {
                // There's a tombstone slot available in this group.
                table[slot].key = strdup(key);
                table[slot].value = value;
                metadata[slot] = meta;
                return;
            }
            slot_leader += GROUP_SIZE;
            slot_leader %= total_slots;
        }
        ERR_EXIT("All slots occupied!\n");
    }

    u32 lookup(const char* key) {
        u64 h = hash(key);
        u64 slot = (h >> 7) % total_slots;
        u64 meta = h & 0x0000007f;
        slot = lookup_slot(key, meta, slot);
        return slot == SLOT_NONE ? VALUE_NIL : table[slot].value;
    }

    u32 remove(const char* key) {
        u64 h = hash(key);
        u64 slot = (h >> 7) % total_slots;
        u64 meta = h & 0x0000007f;
        slot = lookup_slot(key, meta, slot);
        if (slot != SLOT_NONE) {
            u32 old_value = table[slot].value;
            free(table[slot].key);
            table[slot].key = nullptr;
            table[slot].value = VALUE_NIL;
            metadata[slot] = SLOT_TOMBSTONE;
            return old_value;
        }
        return VALUE_NIL;
    }

    void dump() {
        u64 count = 0;
        u64 max_key_len = 3;
        for (u64 i = 0; i < total_slots; i++) {
            if (metadata[i] != SLOT_EMPTY && metadata[i] != SLOT_TOMBSTONE) {
                count++;
                u64 len = strlen(table[i].key);
                if (len > max_key_len) {
                    max_key_len = len;
                }
            }
        }

        max_key_len = max_key_len > 40 ? 40 : max_key_len;
        std::printf("\n");
        std::printf("  %" PRIu64 "/%" PRIu64 " slots occupied (%.1f%% load)\n\n", count, total_slots, 100.0 * count / total_slots);
        std::printf("  %5s | %4s | %-*s | %10s\n", "slot", "meta", (int)max_key_len, "key", "value");
        std::printf("  ------+------+-%.*s-+------------\n", (int)max_key_len, "----------------------------------------");

        for (u64 i = 0; i < total_slots; i++) {
            if (metadata[i] == SLOT_EMPTY || metadata[i] == SLOT_TOMBSTONE) {
                continue;
            }
            std::printf("  %5" PRIu64 " | 0x%02x | %-*s | %10u\n", i, metadata[i], (int)max_key_len, table[i].key, table[i].value);
        }

        std::printf("\n");
    }

private:
    u64 hash(const char* key) {
        u64 h = 0xcbf29ce484222325ULL;
        while (*key) {
            h ^= static_cast<u8>(*key);
            h *= 0x100000001b3ULL;
            key += 1;
        }
        return h;
    }

    u64 lookup_slot(const char* key, u64 meta, u64 slot) {
        u64 slot_leader = (slot / GROUP_SIZE) * GROUP_SIZE;
        auto meta_vec = _mm256_set1_epi8(meta);
        for (u64 i = 0; i < total_groups; i++) {
            auto v2 = _mm256_loadu_si256((__m256i*)(metadata + slot_leader));
            auto res = _mm256_cmpeq_epi8(meta_vec, v2);
            u32 match_mask = (u32)_mm256_movemask_epi8(res);
            u64 match_slot = lookup_group(key, match_mask, slot_leader);
            if (match_slot != SLOT_NONE) {
                // Match found.
                return match_slot;
            }
            // Doesn't exist in this group. If this group has even a single
            // empty slot, that means this key was never inserted. If the key
            // were to be inserted, the empty slot would have been occupied.
            // The crucial thing to understand is that probe sequence of groups
            // across insert and lookup will always be the same. This implies
            // that during lookup, if the current group has an empty slot, this
            // key was never inserted.
            if (lookup_sentinel_slot_in_group(empty_slot_vec, v2, slot_leader) != SLOT_NONE) {
                return SLOT_NONE;
            }
            slot_leader += GROUP_SIZE;
            slot_leader %= total_slots;
        }
        return SLOT_NONE;
    }

    u64 lookup_group(const char* key, u32 match_mask, u64 slot_leader) {
        u8 n_matches = __builtin_popcount(match_mask);
        for (int i = 0; i < n_matches; i++) {
            u8 slot_offset = __builtin_ctz(match_mask);
            u64 slot = slot_leader + slot_offset;
            int res = strcmp(table[slot].key, key);
            if (res == 0) {
                // match found
                return slot;
            }
            match_mask &= ~(1u << slot_offset);
        }
        return SLOT_NONE;
    }
    
    u64 lookup_sentinel_slot_in_group(__m256i sentinel_vec, __m256i leader_vec, u64 slot_leader) {
        auto res = _mm256_cmpeq_epi8(sentinel_vec, leader_vec);
        u32 match_mask = _mm256_movemask_epi8(res);
        if (match_mask > 0) {
            // There's a slot available in this group.
            u64 slot = __builtin_ctz(match_mask);
            slot += slot_leader;
            return slot;
        }
        return SLOT_NONE;
    }
};
