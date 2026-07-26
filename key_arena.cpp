#include <cstdlib>
#include <cstring>
#include "key_arena.h"

KeyArena::KeyArena(u64 n_slots) : capacity(SLOT_SIZE * n_slots), cursor(0) {
    base = (char*)(aligned_alloc(4096, (capacity + 4095) & ~4095ULL));
    for (u64 i = 0; i < capacity; i += 4096) {
        // Prefault all pages ahead of use.
        base[i] = 0;
    }
}

KeyArena::~KeyArena() {
    free(base);
}

char* KeyArena::allocate(const char* key) {
    u64 len = strlen(key);
    char* dst = base + cursor;
    memcpy(dst, key, len + 1);
    cursor += len + 1;
    return dst;
}

void KeyArena::reset() {
    cursor = 0;
}
