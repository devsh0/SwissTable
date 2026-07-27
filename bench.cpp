#include <string>
#include <unordered_map>
#include "util.h"
#include <vector>

struct Result {
    u64 n_entries;
    u64 total_slots;
    u64 swiss_ns;
    u64 std_ns;
};

// Pre-populate both maps with n_entries, returning generated entries for reuse.
// Caller owns the returned entries and must free them.
static Entry* populate(SwissTable& st, std::unordered_map<std::string, u32>& stdmap, u64 n_entries, u64 seed) {
    EntryGen gen(seed);
    Entry* entries = new Entry[n_entries];
    gen.generate_n(entries, n_entries);
    for (u64 i = 0; i < n_entries; i++) {
        st.insert(entries[i].key, entries[i].value);
        stdmap[entries[i].key] = entries[i].value;
    }
    return entries;
}

static void free_entries(Entry* entries, u64 n) {
    for (u64 i = 0; i < n; i++) delete[] entries[i].key;
    delete[] entries;
}

// ---------------------------------------------------------------------------
// Bench 1: Lookup-heavy
//   Pre-populate the map, then do 1M lookups. ~95% hit existing keys,
//   ~5% miss (bogus keys). No inserts or deletes during the timed section.
// ---------------------------------------------------------------------------
static Result bench_lookup(u64 total_slots) {
    u64 n_entries = total_slots * 3 / 4;
    u64 n_ops = 1'000'000;
    const char* bogus_key = "key_that_does_not_exist";

    SwissTable st(total_slots);
    std::unordered_map<std::string, u32> stdmap;
    stdmap.reserve(n_entries);
    Entry* entries = populate(st, stdmap, n_entries, 12345);

    // --- SwissTable ---
    u64 t0 = ns();
    for (u64 i = 0; i < n_ops; i++) {
        if (i % 20 == 0) {
            st.lookup(bogus_key);
        } else {
            Entry& e = entries[i % n_entries];
            u32 v = st.lookup(e.key);
            if (v != e.value) {
                ERR_EXIT("bench_lookup: swiss mismatch\n");
            }
        }
    }
    u64 swiss_elapsed = ns() - t0;

    // --- std::unordered_map ---
    t0 = ns();
    for (u64 i = 0; i < n_ops; i++) {
        if (i % 20 == 0) {
            stdmap.find(bogus_key);
        } else {
            Entry& e = entries[i % n_entries];
            auto it = stdmap.find(e.key);
            if (it == stdmap.end() || it->second != e.value) {
                ERR_EXIT("bench_lookup: stdmap mismatch\n");
            }
        }
    }
    u64 std_elapsed = ns() - t0;

    free_entries(entries, n_entries);
    return { n_entries, total_slots, swiss_elapsed / n_ops, std_elapsed / n_ops };
}

// ---------------------------------------------------------------------------
// Bench 2: Insert-delete heavy
//   Pre-populate the map to 50% load, then do alternating insert/delete.
//   The deletion schedule is pre-computed so the timed loop is pure map ops.
// ---------------------------------------------------------------------------
static Result bench_insert_delete(u64 total_slots) {
    u64 n_prefill = total_slots / 2;
    u64 n_ops = n_prefill < 500'000 ? n_prefill : 500'000;

    EntryGen gen(54321);
    Entry* prefill = new Entry[n_prefill];
    gen.generate_n(prefill, n_prefill);

    Entry* extra = new Entry[n_ops];
    gen.generate_n(extra, n_ops);

    // Pre-compute which prefill key to delete at each step.
    const char** delete_keys = new const char*[n_ops];
    {
        std::vector<u64> live;
        live.reserve(n_prefill);
        for (u64 i = 0; i < n_prefill; i++) live.push_back(i);
        EntryGen idx_gen(99999);
        for (u64 i = 0; i < n_ops; i++) {
            u64 pick = idx_gen.next_u64() % live.size();
            delete_keys[i] = prefill[live[pick]].key;
            live[pick] = live.back();
            live.pop_back();
        }
    }

    // --- SwissTable ---
    SwissTable st(total_slots);
    for (u64 i = 0; i < n_prefill; i++) {
        st.insert(prefill[i].key, prefill[i].value);
    }

    u64 t0 = ns();
    for (u64 i = 0; i < n_ops; i++) {
        st.insert(extra[i].key, extra[i].value);
        st.remove(delete_keys[i]);
    }
    u64 swiss_elapsed = ns() - t0;

    // --- std::unordered_map ---
    std::unordered_map<std::string, u32> stdmap;
    stdmap.reserve(n_prefill);
    for (u64 i = 0; i < n_prefill; i++) {
        stdmap[prefill[i].key] = prefill[i].value;
    }

    t0 = ns();
    for (u64 i = 0; i < n_ops; i++) {
        stdmap[extra[i].key] = extra[i].value;
        stdmap.erase(delete_keys[i]);
    }
    u64 std_elapsed = ns() - t0;

    u64 ops_total = n_ops * 2;
    delete[] delete_keys;
    free_entries(prefill, n_prefill);
    free_entries(extra, n_ops);
    return { n_prefill, total_slots, swiss_elapsed / ops_total, std_elapsed / ops_total };
}

// ---------------------------------------------------------------------------
// Bench 3: Mixed workload
//   Pre-populate to 50% load, then do equal parts insert, delete, lookup.
//   All schedules are pre-computed so the timed loop is pure map ops.
// ---------------------------------------------------------------------------
static Result bench_mixed(u64 total_slots) {
    u64 n_prefill = total_slots / 2;
    u64 n_rounds = n_prefill < 300'000 ? n_prefill : 300'000;

    EntryGen gen(67890);
    Entry* prefill = new Entry[n_prefill];
    gen.generate_n(prefill, n_prefill);

    Entry* extra = new Entry[n_rounds];
    gen.generate_n(extra, n_rounds);

    // Pre-compute delete and lookup schedules.
    const char** delete_keys = new const char*[n_rounds];
    const char** lookup_keys = new const char*[n_rounds];
    {
        std::vector<u64> live;
        live.reserve(n_prefill);
        for (u64 i = 0; i < n_prefill; i++) live.push_back(i);
        EntryGen idx_gen(11111);
        for (u64 i = 0; i < n_rounds; i++) {
            u64 pick = idx_gen.next_u64() % live.size();
            delete_keys[i] = prefill[live[pick]].key;
            live[pick] = live.back();
            live.pop_back();
            lookup_keys[i] = prefill[i % n_prefill].key;
        }
    }

    // --- SwissTable ---
    SwissTable st(total_slots);
    for (u64 i = 0; i < n_prefill; i++) {
        st.insert(prefill[i].key, prefill[i].value);
    }

    u64 t0 = ns();
    for (u64 i = 0; i < n_rounds; i++) {
        st.insert(extra[i].key, extra[i].value);
        st.remove(delete_keys[i]);
        st.lookup(lookup_keys[i]);
    }
    u64 swiss_elapsed = ns() - t0;

    // --- std::unordered_map ---
    std::unordered_map<std::string, u32> stdmap;
    stdmap.reserve(n_prefill);
    for (u64 i = 0; i < n_prefill; i++) {
        stdmap[prefill[i].key] = prefill[i].value;
    }

    t0 = ns();
    for (u64 i = 0; i < n_rounds; i++) {
        stdmap[extra[i].key] = extra[i].value;
        stdmap.erase(delete_keys[i]);
        stdmap.find(lookup_keys[i]);
    }
    u64 std_elapsed = ns() - t0;

    u64 ops_total = n_rounds * 3;
    delete[] delete_keys;
    delete[] lookup_keys;
    free_entries(prefill, n_prefill);
    free_entries(extra, n_rounds);
    return { n_prefill, total_slots, swiss_elapsed / ops_total, std_elapsed / ops_total };
}

// ---------------------------------------------------------------------------
// Bench 4: Resize-heavy
//   Start from a small table (32 slots) and insert n_entries keys, forcing
//   many grow-resizes. Then delete most of them, forcing shrink-resizes.
//   Measures total wall time for the whole sequence.
// ---------------------------------------------------------------------------
static Result bench_resize(u64 n_entries) {
    EntryGen gen(24680);
    Entry* entries = new Entry[n_entries];
    gen.generate_n(entries, n_entries);

    u64 n_deletes = n_entries * 3 / 4;

    // --- SwissTable ---
    SwissTable st(32);

    u64 t0 = ns();
    for (u64 i = 0; i < n_entries; i++) {
        st.insert(entries[i].key, entries[i].value);
    }
    for (u64 i = 0; i < n_deletes; i++) {
        st.remove(entries[i].key);
    }
    u64 swiss_elapsed = ns() - t0;

    // --- std::unordered_map ---
    std::unordered_map<std::string, u32> stdmap;

    t0 = ns();
    for (u64 i = 0; i < n_entries; i++) {
        stdmap[entries[i].key] = entries[i].value;
    }
    for (u64 i = 0; i < n_deletes; i++) {
        stdmap.erase(entries[i].key);
    }
    u64 std_elapsed = ns() - t0;

    u64 ops_total = n_entries + n_deletes;
    free_entries(entries, n_entries);
    return { n_entries, 32, swiss_elapsed / ops_total, std_elapsed / ops_total };
}

static void print_header(const char* title) {
    std::printf("\n  --- %s ---\n", title);
    std::printf("  %10s | %10s | %12s | %12s | %7s\n", "entries", "slots", "swiss (ns)", "stdmap (ns)", "speedup");
    std::printf("  -----------+------------+--------------+--------------+--------\n");
}

static void print_row(Result& r) {
    double speedup = (double)r.std_ns / (double)r.swiss_ns;
    std::printf("  %10" PRIu64 " | %10" PRIu64 " | %12" PRIu64 " | %12" PRIu64 " | %6.2fx\n",
                r.n_entries, r.total_slots, r.swiss_ns, r.std_ns, speedup);
}

int main() {
    // Bench 1: Lookup-heavy
    print_header("Lookup-heavy (95% hit, 5% miss)");
    for (u64 slots = 1024; slots <= 1'048'576; slots *= 2) {
        Result r = bench_lookup(slots);
        print_row(r);
    }

    // Bench 2: Insert-delete heavy
    print_header("Insert-delete heavy (alternating insert + delete)");
    for (u64 slots = 1024; slots <= 1'048'576; slots *= 2) {
        Result r = bench_insert_delete(slots);
        print_row(r);
    }

    // Bench 3: Mixed workload
    print_header("Mixed (insert + delete + lookup, equal parts)");
    for (u64 slots = 1024; slots <= 1'048'576; slots *= 2) {
        Result r = bench_mixed(slots);
        print_row(r);
    }

    // Bench 4: Resize-heavy
    print_header("Resize-heavy (grow from 32 slots, then shrink)");
    for (u64 n = 768; n <= 786'432; n *= 2) {
        Result r = bench_resize(n);
        print_row(r);
    }

    std::printf("\n");
}
