#pragma once

#include "types.h"

#include <vector>
#include <thread>
#include <cstring>

struct HashTableEntry {
    u64 key;
    u64 visits;
    float q;

    HashTableEntry() {
        key = 0;
        visits = 0;
        q = 0;
    }
    HashTableEntry(const u64 key, const u64 visits, const float q) {
        this->key = key;
        this->visits = visits;
        this->q = q;
    }
};

class TranspositionTable {
    HashTableEntry* table;

   public:
    u64 size;

    explicit TranspositionTable(const usize sizeInMB = 16) {
        table = nullptr;
        size = 0;
        reserve(sizeInMB);
    }

    ~TranspositionTable() {
        if (table != nullptr)
            std::free(table);
    }


    void clear(const usize threadCount = 1) {
        assert(threadCount > 0);

        std::vector<std::thread> threads;

        auto clearTT = [&](const usize threadId) {
            // The segment length is the number of entries each thread must clear
            // To find where your thread should start (in entries), you can do threadId * segmentLength
            // Converting segment length into the number of entries to clear can be done via length * bytes per entry

            const usize start = (size * threadId) / threadCount;
            const usize end   = std::min((size * (threadId + 1)) / threadCount, size);

            std::memset(table + start, 0, (end - start) * sizeof(HashTableEntry));
        };

        for (usize thread = 1; thread < threadCount; thread++)
            threads.emplace_back(clearTT, thread);

        clearTT(0);

        for (std::thread& t : threads)
            if (t.joinable())
                t.join();
    }

    void reserve(const usize newSizeMiB) {
        assert(newSizeMiB > 0);
        // Find number of bytes allowed
        size = newSizeMiB * 1024 * 1024 / sizeof(HashTableEntry);
        if (table != nullptr)
            std::free(table);
        table = static_cast<HashTableEntry*>(std::malloc(size * sizeof(HashTableEntry)));
    }

    u64 index(const u64 key) const {
        return static_cast<u64>((static_cast<u128>(key) * static_cast<u128>(size)) >> 64);
    }

    void prefetch(const u64 key) const { __builtin_prefetch(&this->getEntry(key)); }

    HashTableEntry& getEntry(const u64 key) const { return table[index(key)]; }

    void update(const u64 key, const u64 visits, const double q) {
        HashTableEntry& entry = getEntry(key);
        if (key != entry.key || visits > entry.visits)
            entry = HashTableEntry(key, entry.visits, q);
    }

    float hashfull() const {
        const usize samples = std::min<u64>(1000, size);
        usize hits = 0;
        for (usize sample = 0; sample < samples; sample++)
            hits += table[sample].key != 0;
        return static_cast<float>(hits) / samples;
    }
};