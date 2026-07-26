#include "util.h"
#include <unordered_map>
#include <string>

struct Result {
    u64 n_entries;
    u64 swiss_ns_per_lookup;
    u64 std_ns_per_lookup;
};

Result bench(u64 total_slots) {
    u64 n_entries = total_slots * 3 / 4;
    u64 n_lookups = 1'000'000;
    const char* bogus_key = "bogus";

    EntryGen generator(/* seed */12345);
    Entry* entries = new Entry[n_entries];
    generator.generate_n(entries, n_entries);

    // --- SwissTable ---
    SwissTable table(total_slots);
    for (u64 i = 0; i < n_entries; i++) {
        table.insert(entries[i].key, entries[i].value);
    }

    u64 t0 = ns();
    for (u64 i = 0; i < n_lookups; i++) {
        if (i % 21 == 0) {
            table.lookup(bogus_key);
            continue;
        }
        Entry e = entries[i % n_entries];
        u32 v = table.lookup(e.key);
        if (v != e.value) {
            ERR_EXIT("SwissTable: unexpected value!\n");
        }
    }
    u64 swiss_elapsed = ns() - t0;

    // --- std::unordered_map ---
    std::unordered_map<std::string, u32> stdmap;
    stdmap.reserve(n_entries);
    for (u64 i = 0; i < n_entries; i++) {
        stdmap[entries[i].key] = entries[i].value;
    }

    t0 = ns();
    for (u64 i = 0; i < n_lookups; i++) {
        if (i % 21 == 0) {
            stdmap.find(bogus_key);
            continue;
        }
        Entry e = entries[i % n_entries];
        auto it = stdmap.find(e.key);
        if (it == stdmap.end() || it->second != e.value) {
            ERR_EXIT("stdmap: unexpected value!\n");
        }
    }
    u64 std_elapsed = ns() - t0;

    for (u64 i = 0; i < n_entries; i++) {
        delete[] entries[i].key;
    }
    delete[] entries;
    return { n_entries, swiss_elapsed / n_lookups, std_elapsed / n_lookups };
}

int main() {
    std::printf("\n");
    std::printf("  %10s | %10s | %12s | %12s | %7s\n", "entries", "slots", "swiss (ns)", "stdmap (ns)", "speedup");
    std::printf("  -----------+------------+--------------+--------------+--------\n");
    for (u64 slots = 512; slots <= 1'048'576; slots *= 2) {
        Result r = bench(slots);
        double speedup = (double)r.std_ns_per_lookup / (double)r.swiss_ns_per_lookup;
        std::printf("  %10" PRIu64 " | %10" PRIu64 " | %12" PRIu64 " | %12" PRIu64 " | %6.2fx\n", r.n_entries, slots, r.swiss_ns_per_lookup, r.std_ns_per_lookup, speedup);
    }
    std::printf("\n");
}
