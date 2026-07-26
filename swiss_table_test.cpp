#include <cstdarg>
#include <cstdio>
#include "swiss_table.h"

#define st_assert(cond) if (!(cond)) { fail("test=%s, line=%d", __func__, __LINE__); return; }
#define st_report_success() success(__func__)

void fail(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "\033[31m%-10s", "FAILED: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\033[0m\n");
    va_end(args);
}

void success(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stdout, "\033[32m%-10s", "SUCCESS: ");
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "\033[0m\n");
    va_end(args);
}

void test_simple_insertion_and_lookup() {
    SwissTable t;
    u32 v = t.lookup("ten");
    st_assert(v == VALUE_NIL);
    t.insert("ten", 10);
    v = t.lookup("ten");
    st_assert(v == 10);
    st_report_success();
}

void test_simple_removal() {
    SwissTable t;
    u32 v = t.remove("ten");
    st_assert(v == VALUE_NIL);
    t.insert("ten", 10);
    v = t.remove("ten");
    st_assert(v == 10);
    st_report_success();
}

void test_duplicate_insertion_overrides() {
    SwissTable t;
    t.insert("value", 10);
    t.insert("value", 20);
    u32 v = t.lookup("value");
    st_assert(v == 20);
    st_report_success();
}

void test_double_deletion_not_allowed() {
    SwissTable t;
    t.insert("ten", 10);
    u32 v = t.remove("ten");
    st_assert(v == 10);
    v = t.remove("ten");
    st_assert(v == VALUE_NIL);
    st_report_success();
}

void test_missing_key_lookup() {
    SwissTable t;
    u32 v = t.lookup("five");
    st_assert(v == VALUE_NIL);
    st_report_success();
}

void test_insert_after_remove() {
    SwissTable t;
    t.insert("ten", 10);
    t.remove("ten");
    t.insert("ten", 20);
    u32 v = t.lookup("ten");
    st_assert(v == 20);
    st_report_success();
}

void test_lookup_past_tombstone() {
    // "k0" and "k2640" hash to the same group with the same meta.
    // Insert both, remove the first, verify the second is still found.
    SwissTable t;
    t.insert("k0", 1);
    t.insert("k2640", 2);
    t.remove("k0");
    u32 v = t.lookup("k2640");
    st_assert(v == 2);
    st_report_success();
}

void test_probe_chain_across_groups() {
    // These 35 keys all hash to group 0 in a 512-slot table.
    // Inserting all of them forces spill into the next group.
    const char* keys[] = {
        "key18",  "key19",  "key170", "key171", "key172",
        "key173", "key174", "key175", "key250", "key251",
        "key254", "key255", "key256", "key257", "key258",
        "key322", "key323", "key390", "key391", "key392",
        "key393", "key395", "key396", "key397", "key400",
        "key401", "key409", "key586", "key587", "key600",
        "key601", "key602", "key603", "key606", "key607",
    };
    SwissTable t;
    for (u32 i = 0; i < 35; i++) {
        t.insert(keys[i], i);
    }
    for (u32 i = 0; i < 35; i++) {
        u32 v = t.lookup(keys[i]);
        st_assert(v == i);
    }
    st_report_success();
}

void test_dump_after_removal() {
    SwissTable t;
    t.insert("ten", 10);
    t.insert("twenty", 20);
    t.remove("ten");
    t.dump();
    st_report_success();
}

void test_high_load_factor() {
    SwissTable t { /* slot_capacity */ 512 };
    char buf[16];
    for (u32 i = 0; i < 400; i++) {
        snprintf(buf, sizeof(buf), "k%u", i);
        t.insert(buf, i);
    }
    for (u32 i = 0; i < 400; i++) {
        snprintf(buf, sizeof(buf), "k%u", i);
        u32 v = t.lookup(buf);
        st_assert(v == i);
    }
    st_report_success();
}

void test_size() {
    SwissTable t;
    st_assert(t.size() == 0);
    t.insert("one", 1);
    t.insert("two", 2);
    st_assert(t.size() == 2);
    t.remove("one");
    st_assert(t.size() == 1);
    t.remove("two");
    st_assert(t.size() == 0);
    st_report_success();
}

void test_resize_grow() {
    SwissTable t { 64 };
    char buf[16];
    // Insert past the initial capacity to force multiple grows.
    for (u32 i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "k%u", i);
        t.insert(buf, i);
    }
    st_assert(t.size() == 1000);
    for (u32 i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "k%u", i);
        st_assert(t.lookup(buf) == i);
    }
    st_report_success();
}

void test_resize_shrink() {
    SwissTable t { 64 };
    char buf[16];
    for (u32 i = 0; i < 500; i++) {
        snprintf(buf, sizeof(buf), "k%u", i);
        t.insert(buf, i);
    }
    // Remove most entries to trigger shrinks.
    for (u32 i = 0; i < 490; i++) {
        snprintf(buf, sizeof(buf), "k%u", i);
        t.remove(buf);
    }
    st_assert(t.size() == 10);
    // Surviving entries must still be intact.
    for (u32 i = 490; i < 500; i++) {
        snprintf(buf, sizeof(buf), "k%u", i);
        st_assert(t.lookup(buf) == i);
    }
    // Removed entries must be gone.
    st_assert(t.lookup("k0") == VALUE_NIL);
    st_report_success();
}

void test_resize_grow_shrink_grow() {
    SwissTable t { 64 };
    char buf[16];

    // Grow.
    for (u32 i = 0; i < 400; i++) {
        snprintf(buf, sizeof(buf), "g%u", i);
        t.insert(buf, i);
    }

    // Shrink.
    for (u32 i = 0; i < 380; i++) {
        snprintf(buf, sizeof(buf), "g%u", i);
        t.remove(buf);
    }

    // Grow again with new keys.
    for (u32 i = 0; i < 400; i++) {
        snprintf(buf, sizeof(buf), "h%u", i);
        t.insert(buf, i + 1000);
    }

    st_assert(t.size() == 20 + 400);
    // Check survivors from first batch.
    for (u32 i = 380; i < 400; i++) {
        snprintf(buf, sizeof(buf), "g%u", i);
        st_assert(t.lookup(buf) == i);
    }

    // Check second batch.
    for (u32 i = 0; i < 400; i++) {
        snprintf(buf, sizeof(buf), "h%u", i);
        st_assert(t.lookup(buf) == i + 1000);
    }
    st_report_success();
}

void test_arena_compaction() {
    SwissTable t;
    char buf[16];
    // Insert a few permanent entries.
    for (u32 i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "perm%u", i);
        t.insert(buf, i);
    }
    // Churn: insert and immediately remove a key, 10000 times.
    // Each cycle leaks a dead allocation in the arena. The arena
    // for 512 slots holds ~20K bytes, so this will exhaust it
    // multiple times, forcing same-capacity resizes to compact.
    for (u32 i = 0; i < 10000; i++) {
        snprintf(buf, sizeof(buf), "churn%u", i);
        t.insert(buf, i);
        t.remove(buf);
    }
    st_assert(t.size() == 10);
    // Permanent entries must survive all compactions.
    for (u32 i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "perm%u", i);
        st_assert(t.lookup(buf) == i);
    }
    // Churned keys must be gone.
    st_assert(t.lookup("churn0") == VALUE_NIL);
    st_assert(t.lookup("churn9999") == VALUE_NIL);
    st_report_success();
}

int main() {
    test_simple_insertion_and_lookup();
    test_simple_removal();
    test_duplicate_insertion_overrides();
    test_double_deletion_not_allowed();
    test_missing_key_lookup();
    test_insert_after_remove();
    test_lookup_past_tombstone();
    test_probe_chain_across_groups();
    test_dump_after_removal();
    test_high_load_factor();
    test_size();
    test_resize_grow();
    test_resize_shrink();
    test_resize_grow_shrink_grow();
    test_arena_compaction();
    return 0;
}