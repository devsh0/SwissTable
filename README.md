# SwissTable

A minimal Swiss Table hash map implementation for learning purposes. Uses AVX2 intrinsics to perform SIMD-parallel metadata
lookups within 32-slot groups, following the same core design as Google's Abseil `flat_hash_map`.

## Design

- **Metadata array**: Each slot has a 1-byte control tag. The lower 7 bits store a fragment of the key's hash; the high bit
distinguishes empty (0x80) from occupied slots.
- **SIMD probing**: `_mm256_cmpeq_epi8` compares a lookup's hash fragment against all 32 metadata bytes in a group simultaneously,
producing a bitmask of candidate slots.
- **Group-linear probing**: On collision, probing advances one full group (32 slots) at a time.
- **FNV-1a hash**: Bits 7+ select the group; bits 0-6 become the metadata tag.

## Deliberate simplifications

- No resize (capacity set at construction)
- Group-aligned probing (real Swiss Tables use unaligned starting positions)
- Keys stored as borrowed pointers, not owned copies

## Build

Requires an x86-64 target with AVX2:

```
g++ -O2 -mavx2 -std=c++17 -o bench bench.cpp
```
