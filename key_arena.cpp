#include <cstdlib>
#include <cstring>
#include "key_arena.h"

#include <cstdio>
#include <sys/mman.h>

KeyArena::KeyArena(u64 cap) :
    capacity(MAX_KEY_LEN * cap),
    cursor(0)
{
    map_and_prefault();
}

void KeyArena::map_and_prefault() {
    base = (u8*)mmap(nullptr, capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        perror("mmap");
        ERR_EXIT("Failed to map memory!\n");
    }
    for (u64 i = 0; i < capacity; i += 4096) {
        // Prefault all pages ahead of use.
        base[i] = 0;
    }
}

KeyArena::~KeyArena() {
    munmap(base, capacity);
}

void KeyArena::begin_resize(u64 new_capacity) {
    old_base = base;
    old_capacity = capacity;
    capacity = new_capacity * MAX_KEY_LEN;
    cursor = 0;
    map_and_prefault();
}

void KeyArena::finish_resize() {
    munmap(old_base, old_capacity);
    old_base = nullptr;
    old_capacity = 0;
}

char* KeyArena::allocate(const char* key, int key_len) {
    u8* dst = base + cursor;
    memcpy(dst, key, key_len + 1);
    cursor += key_len + 1;
    return (char*)dst;
}

bool KeyArena::is_full() {
    return capacity - cursor < MAX_KEY_LEN;
}

void KeyArena::reset() {
    cursor = 0;
}
