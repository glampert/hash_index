
// ================================================================================================
// -*- C++ -*-
// File: tests.cpp
// Author: Guilherme R. Lampert
// Created on: 03/05/16
// Brief: Minimal unit tests for the hash_index.
// ================================================================================================

// Compiles with:
// c++ -std=c++11 -Wall -Wextra -Weffc++ -pedantic -O3 tests.cpp -o hash_idx_tests

#include "hash_index.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

// ========================================================
// Basic tests (relying on assert(), so leave it on!):
// ========================================================

template<typename HashIndexType>
static void fill_random_keys(HashIndexType * out_hash_idx, std::vector<std::size_t> * out_hash_keys = nullptr)
{
    constexpr std::size_t count = 1024;

    static std::random_device rand_dev;
    static std::mt19937 rand_engine{ rand_dev() };

    std::hash<std::size_t> hasher;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto key = hasher(i) ^ (hasher(rand_engine()) << 1u);

        out_hash_idx->insert(static_cast<typename HashIndexType::key_type>(key),
                             static_cast<typename HashIndexType::index_type>(i));

        if (out_hash_keys != nullptr)
        {
            out_hash_keys->push_back(key);
        }
    }
}

template<typename HashIndexType>
static void test_copy_move_assign()
{
    HashIndexType h1;
    assert(h1.is_allocated() == false);
    assert(h1.allocated_bytes() == 0);

    fill_random_keys(&h1);
    assert(h1.is_allocated() == true);
    assert(h1.allocated_bytes() != 0);

    // Copy constructor:
    HashIndexType h2{ h1 };
    assert(h2.is_allocated()      == true);
    assert(h2.allocated_bytes()   == h1.allocated_bytes());
    assert(h2.index_chain_size()  == h1.index_chain_size());
    assert(h2.hash_buckets_size() == h1.hash_buckets_size());
    assert(h2 == h1); // Deep comparison
    assert(h1 == h2); // Deep comparison

    // Move constructor (steal h1):
    HashIndexType h3{ std::move(h1) };

    // h3 should now be equal to h2 (which is a copy of former h1):
    assert(h3.is_allocated()      == true);
    assert(h3.allocated_bytes()   == h2.allocated_bytes());
    assert(h3.index_chain_size()  == h2.index_chain_size());
    assert(h3.hash_buckets_size() == h2.hash_buckets_size());
    assert(h3 == h2);
    assert(h2 == h3);

    // h1 must now be an empty hash_index:
    assert(h1.is_allocated() == false);
    assert(h1.allocated_bytes() == 0);

    // Assignment operator:
    HashIndexType h4;
    assert(h4.is_allocated() == false);
    assert(h4.allocated_bytes() == 0);

    h4 = h3;
    assert(h4.is_allocated() == true);
    assert(h4.allocated_bytes() != 0);

    assert(h4 == h3);
    assert(h3 == h4);
}

template<typename HashIndexType>
static void test_insertion()
{
    HashIndexType h1;
    std::vector<std::size_t> keys;

    // fill will do the insertions:
    fill_random_keys(&h1, &keys);

    // now we just validate the inserted keys are there:
    for (auto k : keys)
    {
        assert(h1.first(k) != h1.null_index);
    }
}

template<typename HashIndexType>
static void test_erasure()
{
    HashIndexType h1;
    std::vector<std::size_t> keys;

    // fill with some sample keys:
    fill_random_keys(&h1, &keys);

    // and erase them:
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        h1.erase(static_cast<typename HashIndexType::key_type>(keys[i]),
                 static_cast<typename HashIndexType::index_type>(i));
    }

    // now first() should yield null_index for all keys.
    for (auto k : keys)
    {
        assert(h1.first(k) == h1.null_index);
    }
}

template<typename HashIndexType>
static void test_lookup()
{
    HashIndexType h1;
    std::vector<std::size_t> keys;
    std::vector<bool> found_keys;

    // fill with some sample keys:
    fill_random_keys(&h1, &keys);
    found_keys.resize(keys.size(), false);

    // and find them:
    for (std::size_t k = 0; k < keys.size(); ++k)
    {
        for (auto i = h1.first(keys[k]); i != h1.null_index; i = h1.next(i))
        {
            if (static_cast<std::size_t>(i) == k)
            {
                found_keys[k] = true;
                break;
            }
        }
    }

    // check all found:
    for (std::size_t i = 0; i < found_keys.size(); ++i)
    {
        assert(found_keys[i] == true);
    }
}

template<typename HashIndexType>
static void test_key_collisions()
{
    HashIndexType h1;

    constexpr std::size_t count = 1024;
    constexpr std::size_t key   = 0xCAFED00D;

    // insert a bunch of indexes at the same key/bucket:
    for (std::size_t i = 0; i < count; ++i)
    {
        h1.insert(static_cast<typename HashIndexType::key_type>(key),
                  static_cast<typename HashIndexType::index_type>(i));
    }

    // Ensure they are still reachable:
    std::vector<bool> found_keys(count, false);

    for (std::size_t k = 0; k < count; ++k)
    {
        for (auto i = h1.first(key); i != h1.null_index; i = h1.next(i))
        {
            if (static_cast<std::size_t>(i) == k)
            {
                found_keys[k] = true;
                break;
            }
        }
    }

    for (std::size_t i = 0; i < found_keys.size(); ++i)
    {
        assert(found_keys[i] == true);
    }
}

// ========================================================
// main() - Test driver:
// ========================================================

// Should work all the same regardless of the integer types used.
using HashIndexDefault = hash_index<>;
using HashIndexSInt32  = hash_index<std::int32_t,  std::int32_t,  std::int32_t>;
using HashIndexUInt32  = hash_index<std::uint32_t, std::uint32_t, std::uint32_t>;
using HashIndexSInt64  = hash_index<std::int64_t,  std::int64_t,  std::int64_t>;
using HashIndexUInt64  = hash_index<std::uint64_t, std::uint64_t, std::uint64_t>;

// Calls the same test once for each specialized hash_index instantiation.
#define TEST(func)                                            \
    do                                                        \
    {                                                         \
        std::cout << "> Testing " << #func << "...\n";        \
        test_##func<HashIndexDefault>();                      \
        test_##func<HashIndexSInt32>();                       \
        test_##func<HashIndexUInt32>();                       \
        test_##func<HashIndexSInt64>();                       \
        test_##func<HashIndexUInt64>();                       \
        std::cout << "> " << #func << " test completed!\n\n"; \
    }                                                         \
    while (0)

int main()
{
    std::cout << "\nRunning unit tests for hash_index...\n\n";

    TEST(copy_move_assign);
    TEST(insertion);
    TEST(erasure);
    TEST(lookup);
    TEST(key_collisions);

    std::cout << "All tests passed!\n\n";
}
