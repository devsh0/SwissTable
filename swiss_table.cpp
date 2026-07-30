#include <cstdio>
#include <cstring>
#include "swiss_table.h"
#include <sys/mman.h>

SwissTable::SwissTable(u64 initial_capacity) :
    current_size(0),
    capacity(initial_capacity),
    total_groups(initial_capacity / GROUP_SIZE),
    arena(initial_capacity)
{
    if (initial_capacity < GROUP_SIZE || (initial_capacity & (initial_capacity - 1)) != 0) {
        ERR_EXIT("Capacity must be a power of two >= 32\n");
    }
    empty_slot_vec = _mm256_set1_epi8(SLOT_EMPTY);
    tombstone_slot_vec = _mm256_set1_epi8(SLOT_TOMBSTONE);
    table = new Entry[capacity];
    metadata = new u8[capacity];
    memset(metadata, SLOT_EMPTY, capacity);
}

SwissTable::~SwissTable() {
    delete[] table;
    delete[] metadata;
}

bool SwissTable::should_grow() {
    return current_size >= (capacity * 7 / 8);
}

bool SwissTable::should_shrink() {
    return capacity > 512 && current_size < (capacity / 4);
}

void SwissTable::resize(u64 new_capacity) {
    u64 old_capacity = capacity;
    Entry* new_table = new Entry[new_capacity];
    Entry* old_table = table;
    u8* new_metadata = new u8[new_capacity];
    memset(new_metadata, SLOT_EMPTY, new_capacity);
    u8* old_metadata = metadata;

    capacity = new_capacity;
    table = new_table;
    metadata = new_metadata;
    total_groups = capacity / GROUP_SIZE;
    current_size = 0;

    arena.begin_resize(new_capacity);
    for (u64 i = 0; i < old_capacity; i++) {
        if (old_metadata[i] != SLOT_TOMBSTONE && old_metadata[i] != SLOT_EMPTY) {
            Entry& current_entry = old_table[i];
            insert(current_entry.key, current_entry.value);
        }
    }
    arena.finish_resize();

    delete[] old_table;
    delete[] old_metadata;
}

u64 SwissTable::size() {
    return current_size;
}

void SwissTable::insert(const char* key, u32 value) {
    int key_len = strlen(key);
    if (key_len >= MAX_KEY_LEN) {
        ERR_EXIT("Key too large!\n");
    }
    if (should_grow()) {
        resize(2 * capacity);
    } else if (arena.is_full()) {
        // We are accumulating dead keys in the arena. It's possible
        // the arena gets exhausted before we reach the threshold for
        // next resize. Therefore, check the arena and trigger a same
        // capacity resize to compactly pack the entries.
        resize(capacity);
    }
    u64 h = hash(key);
    // Does (h >> 7) % capacity, given capacity is a power of 2.
    u64 slot = (h >> 7) & (capacity - 1);
    u64 slot_leader = (slot / GROUP_SIZE) * GROUP_SIZE;
    u64 meta = h & 0x0000007f;
    slot = lookup_slot(key, meta, slot);
    if (slot != SLOT_NONE) {
        // This key already exists. Just update the value.
        table[slot].value = value;
        return;
    }
    for (u64 i = 0; i < total_groups; i++) {
        __m256i leader_vec = _mm256_loadu_si256((__m256i*)(metadata + slot_leader));
        slot = lookup_sentinel_slot_in_group(empty_slot_vec, leader_vec, slot_leader);
        if (slot != SLOT_NONE) {
            // Empty slot found.
            table[slot].key = arena.allocate(key, key_len);
            table[slot].value = value;
            metadata[slot] = meta;
            current_size += 1;
            return;
        }
        slot = lookup_sentinel_slot_in_group(tombstone_slot_vec, leader_vec, slot_leader);
        if (slot != SLOT_NONE) {
            // Tombstone slot found.
            table[slot].key = arena.allocate(key, key_len);
            table[slot].value = value;
            metadata[slot] = meta;
            current_size += 1;
            return;
        }
        slot_leader += GROUP_SIZE;
        slot_leader %= capacity;
    }
    ERR_EXIT("All slots occupied!\n");
}

u32 SwissTable::lookup(const char* key) {
    u64 h = hash(key);
    // Does (h >> 7) % capacity, given capacity is a power of 2.
    u64 slot = (h >> 7) & (capacity - 1);
    u64 meta = h & 0x0000007f;
    slot = lookup_slot(key, meta, slot);
    return slot == SLOT_NONE ? VALUE_NIL : table[slot].value;
}

u32 SwissTable::remove(const char* key) {
    u64 h = hash(key);
    // Does (h >> 7) % capacity, given capacity is a power of 2.
    u64 slot = (h >> 7) & (capacity - 1);
    u64 meta = h & 0x0000007f;
    slot = lookup_slot(key, meta, slot);
    if (slot != SLOT_NONE) {
        // The entry exists. Remove it.
        u32 old_value = table[slot].value;
        table[slot].key = nullptr;
        table[slot].value = VALUE_NIL;
        metadata[slot] = SLOT_TOMBSTONE;
        current_size -= 1;
        if (should_shrink()) {
            resize(capacity / 2);
        }
        return old_value;
    }
    return VALUE_NIL;
}

void SwissTable::dump() {
    u64 count = 0;
    u64 max_key_len = 3;
    for (u64 i = 0; i < capacity; i++) {
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
    std::printf("  %" PRIu64 "/%" PRIu64 " slots occupied (%.1f%% load)\n\n", count, capacity, 100.0 * count / capacity);
    std::printf("  %5s | %4s | %-*s | %10s\n", "slot", "meta", (int)max_key_len, "key", "value");
    std::printf("  ------+------+-%.*s-+------------\n", (int)max_key_len, "----------------------------------------");

    for (u64 i = 0; i < capacity; i++) {
        if (metadata[i] == SLOT_EMPTY || metadata[i] == SLOT_TOMBSTONE) {
            continue;
        }
        std::printf("  %5" PRIu64 " | 0x%02x | %-*s | %10u\n", i, metadata[i], (int)max_key_len, table[i].key, table[i].value);
    }

    std::printf("\n");
}

u64 SwissTable::hash(const char* key) {
    u64 h = 0xcbf29ce484222325ULL;
    while (*key) {
        h ^= static_cast<u8>(*key);
        h *= 0x100000001b3ULL;
        key += 1;
    }
    return h;
}

u64 SwissTable::lookup_slot(const char* key, u64 meta, u64 slot) {
    u64 slot_leader = (slot / GROUP_SIZE) * GROUP_SIZE;
    auto meta_vec = _mm256_set1_epi8(meta);
    for (u64 i = 0; i < total_groups; i++) {
        auto v2 = _mm256_loadu_si256((__m256i*)(metadata + slot_leader));
        auto res = _mm256_cmpeq_epi8(meta_vec, v2);
        u32 match_mask = (u32)_mm256_movemask_epi8(res);
        u64 match_slot = lookup_group(key, match_mask, slot_leader);
        if (match_slot != SLOT_NONE) {
            return match_slot;
        }
        // Key doesn't exist in this group. If this group has even a single
        // empty slot, that means this key was never inserted in the hashmap.
        // If the key were to be inserted, the empty slot would have been occupied.
        // The crucial thing to understand is that probe sequence of groups
        // across insert and lookup will always be the same. This implies
        // that during lookup, if the current group has an empty slot, this
        // key was never inserted.
        if (lookup_sentinel_slot_in_group(empty_slot_vec, v2, slot_leader) != SLOT_NONE) {
            return SLOT_NONE;
        }
        slot_leader += GROUP_SIZE;
        slot_leader %= capacity;
    }
    return SLOT_NONE;
}

u64 SwissTable::lookup_group(const char* key, u32 match_mask, u64 slot_leader) {
    u8 n_matches = __builtin_popcount(match_mask);
    for (int i = 0; i < n_matches; i++) {
        u8 slot_offset = __builtin_ctz(match_mask);
        u64 slot = slot_leader + slot_offset;
        int res = strcmp(table[slot].key, key);
        if (res == 0) {
            return slot;
        }
        match_mask &= ~(1u << slot_offset);
    }
    return SLOT_NONE;
}

u64 SwissTable::lookup_sentinel_slot_in_group(__m256i sentinel_vec, __m256i leader_vec, u64 slot_leader) {
    auto res = _mm256_cmpeq_epi8(sentinel_vec, leader_vec);
    u32 match_mask = _mm256_movemask_epi8(res);
    if (match_mask > 0) {
        u64 slot = __builtin_ctz(match_mask);
        slot += slot_leader;
        return slot;
    }
    return SLOT_NONE;
}
