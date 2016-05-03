
// ================================================================================================
// -*- C++ -*-
// File: benchmarks.cpp
// Author: Guilherme R. Lampert
// Created on: 03/05/16
// Brief: Benchmark tests comparing hash_index with std::map and std::unordered_map.
// ================================================================================================

// Compiles with:
// c++ -std=c++11 -Wall -Wextra -Weffc++ -pedantic -O3 benchmarks.cpp -o hash_idx_bench
//
// Asm listing:
// c++ -std=c++11 -S -mllvm --x86-asm-syntax=intel benchmarks.cpp

#include "hash_index.hpp"
#include <unordered_map>
#include <map>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <chrono>
#include <random>
#include <string>
#include <vector>

// ========================================================
// Test support code:
// ========================================================

using KeyType  = std::string;
using ValType  = std::pair<std::size_t, KeyType>;

using Clock    = std::chrono::high_resolution_clock;
using TimeUnit = std::chrono::nanoseconds;
using Times    = std::vector<TimeUnit>;

static const char * TimeUnitSuffix = " ns";

// Tells the compiler that all previous changes to memory shall be visible.
inline void clobber_memory() { asm volatile ( "" : : : "memory" ); }

// Tells the compiler that the value of 'var' shall be visible.
inline void use_variable(void * var) { asm volatile ( "" : : "rm"(var) : ); }

// ========================================================

static KeyType make_random_key()
{
    // Called inside the test loops, so we don't want to create a new engine each time.
    static std::random_device rand_dev;
    static std::mt19937 rand_engine{ rand_dev() };

    static std::uniform_int_distribution<int> dist_A_Z{ 'A', 'Z' };
    static std::uniform_int_distribution<int> dist_0_9{ '0', '9' };

    constexpr int key_length = 8;
    KeyType key;

    for (int i = 0; i < key_length / 2; ++i) // Half letter from A-Z
    {
        key.push_back(static_cast<char>(dist_A_Z(rand_engine)));
    }
    for (int i = 0; i < key_length / 2; ++i) // Half numbers from 0-9
    {
        key.push_back(static_cast<char>(dist_0_9(rand_engine)));
    }

    return key;
}

static std::vector<KeyType> make_random_key_vector(const long size)
{
    std::vector<KeyType> keys;
    keys.reserve(size);
    for (long i = 0; i < size; ++i)
    {
        keys.push_back(make_random_key());
    }
    return keys;
}

static void print_test_stats(const Times & times)
{
    // Take the median, minimum and maximum:
    auto sum     = TimeUnit::zero();
    auto minimum = TimeUnit::max();
    auto maximum = TimeUnit::min();

    for (auto t : times)
    {
        if (t < minimum)
        {
            minimum = t;
        }
        else if (t > maximum)
        {
            maximum = t;
        }
        sum += t;
    }

    const std::uint64_t mean = sum.count() / times.size();
    const std::uint64_t min  = minimum.count();
    const std::uint64_t max  = maximum.count();

    // Now we can print:
    std::cout << "\n";
    std::cout << "average time taken...: " << mean << TimeUnitSuffix << "\n";
    std::cout << "lowest time sample...: " << min  << TimeUnitSuffix << "\n";
    std::cout << "largest time sample..: " << max  << TimeUnitSuffix << "\n";
    std::cout << "\n";
}

// ========================================================
// Key/Value insertion:
// ========================================================

template<typename StdMapType>
static void test_insertion_std(const char * const map_type_name, const long num_iterations)
{
    std::cout << "\n";
    std::cout << "testing insertions on " << map_type_name << "\n";
    std::cout << num_iterations << " iterations\n";

    StdMapType test_map;

    Times times;
    times.reserve(num_iterations);

    for (long i = 0; i < num_iterations; ++i)
    {
        const KeyType key{ make_random_key() };
        const ValType val{ i, key };

        clobber_memory();

        const auto start = Clock::now();

        test_map.insert(std::make_pair(key, val));

        const auto end = Clock::now();

        use_variable(&test_map);

        times.push_back(end - start);
    }

    assert(long(test_map.size()) == num_iterations);

    print_test_stats(times);
    std::cout << "----------------------------------\n";
}

static void test_insertion_map(const long num_iterations)
{
    using MapType = std::map<KeyType, ValType>;
    test_insertion_std<MapType>("std::map", num_iterations);
}

static void test_insertion_unordered_map(const long num_iterations)
{
    using MapType = std::unordered_map<KeyType, ValType>;
    test_insertion_std<MapType>("std::unordered_map", num_iterations);
}

static void test_insertion_hash_index(const long num_iterations)
{
    std::cout << "\n";
    std::cout << "testing insertions on hash_index + std::vector\n";
    std::cout << num_iterations << " iterations\n";

    hash_index<> hash_idx;
    std::vector<ValType> values;

    Times times;
    times.reserve(num_iterations);

    for (long i = 0; i < num_iterations; ++i)
    {
        const KeyType key{ make_random_key() };
        const ValType val{ i, key };

        clobber_memory();

        // hash_index doesn't bind the values to the keys
        // like std::map/unordered_map, so adding to the
        // hash_index also implies adding to the value store.
        // We also measure the vector::push_back time by design.
        // We're also assuming a full copy of the value for
        // the worst case usage where a move is not possible.

        const auto start = Clock::now();

        values.push_back(val);
        hash_idx.insert(std::hash<KeyType>{}(key), values.size() - 1);

        const auto end = Clock::now();

        use_variable(&hash_idx);
        use_variable(&values);

        times.push_back(end - start);
    }

    assert(long(values.size()) == num_iterations);

    print_test_stats(times);
    std::cout << "----------------------------------\n";
}

// ========================================================
// Erasing by key:
// ========================================================

template<typename StdMapType>
static void test_erasure_std(const char * const map_type_name, const long num_iterations)
{
    std::cout << "\n";
    std::cout << "testing erasures on " << map_type_name << "\n";
    std::cout << num_iterations << " iterations\n";

    StdMapType test_map;
    const auto keys = make_random_key_vector(num_iterations);

    // Fill up the test map:
    for (long i = 0; i < num_iterations; ++i)
    {
        const ValType val{ i, keys[i] };
        test_map.insert(std::make_pair(keys[i], val));
    }

    // Now attempt to erase each key, then measure:
    Times times;
    times.reserve(num_iterations);
    for (long i = 0; i < num_iterations; ++i)
    {
        clobber_memory();

        const auto start = Clock::now();

        test_map.erase(keys[i]);

        const auto end = Clock::now();

        use_variable(&test_map);

        times.push_back(end - start);
    }

    assert(test_map.empty());

    print_test_stats(times);
    std::cout << "----------------------------------\n";
}

static void test_erasure_map(const long num_iterations)
{
    using MapType = std::map<KeyType, ValType>;
    test_erasure_std<MapType>("std::map", num_iterations);
}

static void test_erasure_unordered_map(const long num_iterations)
{
    using MapType = std::unordered_map<KeyType, ValType>;
    test_erasure_std<MapType>("std::unordered_map", num_iterations);
}

static void test_erasure_hash_index(const long num_iterations)
{
    std::cout << "\n";
    std::cout << "testing erasures on hash_index + std::vector\n";
    std::cout << num_iterations << " iterations\n";

    hash_index<> hash_idx;
    const auto keys = make_random_key_vector(num_iterations);

    // Fill up the hash_idx:
    for (long i = 0; i < num_iterations; ++i)
    {
        hash_idx.insert(std::hash<KeyType>{}(keys[i]), i);
    }
    assert(long(hash_idx.index_chain_size()) >= num_iterations);

    // Now attempt to erase each key, then measure:
    Times times;
    times.reserve(num_iterations);
    for (long i = 0; i < num_iterations; ++i)
    {
        clobber_memory();

        const auto start = Clock::now();

        // We are not removing a value from the value store,
        // so hash_idx::erase() will always beat the other standard
        // maps. In a real life use case, you'd probably also want
        // to erase a value from a vector, which will then take
        // linear time by itself on the number of elements shifted.
        hash_idx.erase(std::hash<KeyType>{}(keys[i]), i);

        const auto end = Clock::now();

        use_variable(&hash_idx);

        times.push_back(end - start);
    }

    print_test_stats(times);
    std::cout << "----------------------------------\n";
}

// ========================================================
// Lookup by key:
// ========================================================

template<typename StdMapType>
static void test_lookup_std(const char * const map_type_name, const long num_iterations)
{
    std::cout << "\n";
    std::cout << "testing lookup on " << map_type_name << "\n";
    std::cout << num_iterations << " iterations\n";

    StdMapType test_map;
    auto keys = make_random_key_vector(num_iterations);

    // Fill up the test map:
    for (long i = 0; i < num_iterations; ++i)
    {
        const ValType val{ i, keys[i] };
        test_map.insert(std::make_pair(keys[i], val));
    }

    // Scramble the keys so lookup is in a random order from the insertion:
    std::shuffle(std::begin(keys), std::end(keys), std::mt19937{});

    // Now attempt to lookup each key, then measure:
    Times times;
    times.reserve(num_iterations);
    for (long i = 0; i < num_iterations; ++i)
    {
        clobber_memory();

        const auto start = Clock::now();

        auto iter = test_map.find(keys[i]);

        const auto end = Clock::now();

        assert(iter != std::end(test_map));

        use_variable(&iter);
        use_variable(&test_map);

        times.push_back(end - start);
    }

    print_test_stats(times);
    std::cout << "----------------------------------\n";
}

static void test_lookup_map(const long num_iterations)
{
    using MapType = std::map<KeyType, ValType>;
    test_lookup_std<MapType>("std::map", num_iterations);
}

static void test_lookup_unordered_map(const long num_iterations)
{
    using MapType = std::unordered_map<KeyType, ValType>;
    test_lookup_std<MapType>("std::unordered_map", num_iterations);
}

static void test_lookup_hash_index(const long num_iterations)
{
    std::cout << "\n";
    std::cout << "testing lookup on hash_index + std::vector\n";
    std::cout << num_iterations << " iterations\n";

    hash_index<> hash_idx;
    std::vector<ValType> values;
    auto keys = make_random_key_vector(num_iterations);

    // hash_index::find() will need to perform comparisons
    // with the lookup key and an item in the value store,
    // in case some keys hash to the same bucket. Since the
    // key and value types differ, we need this helper predicate.
    auto find_predicate = [](const KeyType & key, const ValType & item)
    {
        return key == item.second;
    };

    // Fill up the test map:
    values.reserve(num_iterations);
    for (long i = 0; i < num_iterations; ++i)
    {
        values.push_back({ i, keys[i] });
        hash_idx.insert(std::hash<KeyType>{}(keys[i]), values.size() - 1);
    }
    assert(long(hash_idx.index_chain_size()) >= num_iterations);

    // Scramble the keys so lookup is in a random order from the insertion:
    std::shuffle(std::begin(keys), std::end(keys), std::mt19937{});

    // Now attempt to lookup each key, then measure:
    Times times;
    times.reserve(num_iterations);
    for (long i = 0; i < num_iterations; ++i)
    {
        clobber_memory();

        const auto start = Clock::now();

        auto index = hash_idx.find(std::hash<KeyType>{}(keys[i]), keys[i], values, find_predicate);

        const auto end = Clock::now();

        assert(index != hash_idx.null_index);

        use_variable(&index);
        use_variable(&hash_idx);
        use_variable(&values);

        times.push_back(end - start);
    }

    print_test_stats(times);
    std::cout << "----------------------------------\n";
}

// ========================================================
// main():
// ========================================================

int main(int argc, const char * argv[])
{
    long num_iterations = 1024; // default value if none provided via cmdline

    if (argc > 1)
    {
        try
        {
            num_iterations = std::stol(argv[1]);
        }
        catch (...)
        {
            std::cerr << "\nArgument must be a positive integer number!\n";
            std::cerr << "Usage: " << argv[0] << " <num_iterations>\n\n";
            return EXIT_FAILURE;
        }
    }

    // insert() method:
    test_insertion_map(num_iterations);
    test_insertion_unordered_map(num_iterations);
    test_insertion_hash_index(num_iterations);

    // erase() method:
    test_erasure_map(num_iterations);
    test_erasure_unordered_map(num_iterations);
    test_erasure_hash_index(num_iterations);

    // find() method:
    test_lookup_map(num_iterations);
    test_lookup_unordered_map(num_iterations);
    test_lookup_hash_index(num_iterations);
}

