
// ================================================================================================
// -*- C++ -*-
// File: hash_index.hpp
// Author: Guilherme R. Lampert
// Created on: 03/05/16
//
// About:
//  hash_index, a fast hash table template for array indexes.
//
// License:
//  hash_index is work derived from a similar class found on the source code release of
//  DOOM 3 BFG by id Software, available at <https://github.com/id-Software/DOOM-3-BFG>,
//  and therefore is released under the GNU General Public License version 3 to comply
//  with the original work. See the accompanying LICENSE file for full disclosure.
//
// ================================================================================================

#ifndef HASH_INDEX_HPP
#define HASH_INDEX_HPP

// Defining this before including the file prevents pulling the Standard headers.
// Useful to be able to place this file inside a user-defined namespace or to simply
// avoid redundant inclusions. User is responsible for providing all the necessary
// Standard headers before #including this one.
#ifndef HASH_INDEX_NO_STD_INCLUDES
    #include <type_traits>
    #include <functional>
    #include <algorithm>
    #include <memory>
    #include <vector>
#endif // HASH_INDEX_NO_STD_INCLUDES

// Hook to allow providing a custom assert() before including this file.
#ifndef HASH_INDEX_ASSERT
    #ifndef HASH_INDEX_NO_STD_INCLUDES
        #include <cassert>
    #endif // HASH_INDEX_NO_STD_INCLUDES
    #define HASH_INDEX_ASSERT assert
#endif // HASH_INDEX_ASSERT

//
// -----------------------
//  hash_index<> template
// -----------------------
//
// Brief:
//  Provides an efficient way of indexing into arrays by a key
//  other than the integer element index. hash_index<> allows
//  associating a hash key (returned by a function like std::hash)
//  with an integer index of an external array where the mapped
//  value is stored.
//
// Template arguments:
//  IndexType is the type used to store the value array indexes.
//  It must be an integer type. By default set to unsigned int
//  to save memory on platforms where that type is 32-bits wide,
//  since most of the time the full 64-bits indexing range is
//  not required.
//
//  KeyType is the type of a hash key/hash-code used to
//  lookup into hash_index. This type is only used in the
//  public interface. By default set to std::size_t to match
//  the return value of std::hash.
//
//  SizeType is used internally to store array sizes and is also
//  visible in the public interface on functions that deal in sizes.
//  Set to std::size_t by default to match the Standard library but
//  could also be replaced by a 32-bits type to save memory if the
//  full 64-bits range is not required.
//
//  Allocator is the Standard-compatible allocator used to alloc
//  and dealloc the underlaying arrays used by hash_index<>.
//  We inherit privately from it to take advantage of the Empty
//  Base Class Optimization (EBO) if it is an empty class, so
//  if providing a custom one, make sure it is inheritable
//  (i.e., not final). By default std::allocator is used. The
//  only methods we require are allocate() and deallocate().
//
//  All the template parameters are optional and you can simply
//  declare an instance of this class as 'hash_index<>' if the
//  defaults described above fit your needs.
//
// ----------------
//  Usage examples
// ----------------
//
//  struct Thing
//  {
//      std::string name;
//      ...
//  };
//
//  hash_index<>       hash_idx;
//  std::vector<Thing> values;
//
// Insertion:
//
//  Thing t = make_thing();
//  values.push_back(t);
//  hash_idx.insert(std::hash<std::string>{}(t.name), values.size() - 1);
//
// Lookup:
//
//  std::string key = ...;
//  const auto index = hash_idx.find(std::hash<std::string>{}(key), key, values,
//                                   [](const std::string & key, const Thing & item)
//                                   {
//                                       return key == item.name;
//                                   });
//  index == hash_index<>::null_index if not found, index into 'values[]' otherwise.
//
// Removal:
//
//  std::string  key   = ...;
//  unsigned int index = ...;
//  hash_idx.erase(std::hash<std::string>{}(key), index);
//
template
<
    typename IndexType = unsigned int,
    typename KeyType   = std::size_t,
    typename SizeType  = std::size_t,
    typename Allocator = std::allocator<IndexType>
>
class hash_index final
    : private Allocator // Take advantage of EBO for the default empty std::allocator
{
public:

    static_assert(std::is_integral<IndexType>::value, "Integer type required for IndexType!");
    static_assert(std::is_integral<KeyType>::value,   "Integer type required for KeyType!");
    static_assert(std::is_integral<SizeType>::value,  "Integer type required for SizeType!");

    //
    // index_type / null_index:
    //
    // You can use any signed or unsigned integral type for the underlaying
    // index type of hash_index. By default index_type is an unsigned int, so
    // on most platforms each index will only take 32-bits. If you need the
    // full 64-bits indexing range, then instantiate the class with int64_t
    // or equivalent. The bit pattern ~0 (0xFFFFF...) is reserved as an
    // internal sentinel value to indicate an unused hash bucket, so this
    // value cannot be used as an index.
    //
    using index_type = IndexType;
    static constexpr index_type null_index = ~static_cast<index_type>(0);

    //
    // key_type:
    //
    // Integral type of the hashed key value. By default it is set to std::size_t
    // to match the type returned by std::hash, but you can change it to any other
    // integral type, signed or unsigned, that better fits your hash function.
    //
    using key_type = KeyType;

    //
    // size_type:
    //
    // Type used to represent the internal array counts and sizes.
    // Set to std::size_t by default to be consistent with the C++
    // Standard Library, but it is rare the case where you actually
    // have a hash_index big enough to overflow a smaller integer type,
    // so this can probably be replaced by a 32-bits unsigned integer
    // if you care about saving space on each individual class instance.
    //
    using size_type = SizeType;

    //
    // default_initial_size / default_granularity:
    //
    // Default values used by the non-parameterized constructor
    // to set the initial number of hash buckets and size of the
    // index chain. Larger sizes reduce collisions and reallocations
    // but will of course consume more memory.
    //
    static constexpr size_type default_initial_size = 1024;
    static constexpr size_type default_granularity  = 1024;

    //
    // Constructors-destructor / copy-assignment:
    //

    hash_index()
    {
        internal_init(default_initial_size, default_initial_size);
    }

    hash_index(const size_type initial_hash_buckets_size,
               const size_type initial_index_chain_size)
    {
        internal_init(initial_hash_buckets_size, initial_index_chain_size);
    }

    ~hash_index()
    {
        clear_and_free();
    }

    hash_index(const hash_index & other)
    {
        // 'other' is empty/never allocated:
        if (other.m_lookup_mask == 0)
        {
            m_hash_buckets = m_invalid_index_dummy;
            m_index_chain  = m_invalid_index_dummy;
        }
        else // Copy data from 'other':
        {
            HASH_INDEX_ASSERT(other.is_allocated());
            HASH_INDEX_ASSERT(is_power_of_two(other.m_hash_buckets_size));

            m_hash_buckets = Allocator::allocate(other.m_hash_buckets_size);
            m_index_chain  = Allocator::allocate(other.m_index_chain_size);

            std::copy(other.m_hash_buckets, other.m_hash_buckets + other.m_hash_buckets_size, m_hash_buckets);
            std::copy(other.m_index_chain,  other.m_index_chain  + other.m_index_chain_size,  m_index_chain);
        }

        m_hash_buckets_size = other.m_hash_buckets_size;
        m_index_chain_size  = other.m_index_chain_size;
        m_hash_mask         = other.m_hash_mask;
        m_lookup_mask       = other.m_lookup_mask;
        m_granularity       = other.m_granularity;
    }

    hash_index & operator = (hash_index other)
    {
        swap(*this, other);
        return *this;
    }

    hash_index(hash_index && other)
        : hash_index{} // Init via default constructor (C++11 constructor delegation)
    {
        swap(*this, other);
    }

    // Non-throwing swap() overload for hash_index so we
    // can enable copy-and-swap in the above constructors.
    friend void swap(hash_index & lhs, hash_index & rhs) noexcept
    {
        using std::swap;
        swap(lhs.m_hash_buckets,      rhs.m_hash_buckets);
        swap(lhs.m_index_chain,       rhs.m_index_chain);
        swap(lhs.m_hash_buckets_size, rhs.m_hash_buckets_size);
        swap(lhs.m_index_chain_size,  rhs.m_index_chain_size);
        swap(lhs.m_hash_mask,         rhs.m_hash_mask);
        swap(lhs.m_lookup_mask,       rhs.m_lookup_mask);
        swap(lhs.m_granularity,       rhs.m_granularity);
    }

    //
    // Lookup:
    //

    index_type first(const key_type key) const
    {
        //
        // By ensuring the size of the hash buckets array is always a PoT
        // we can use as fast AND (key & m_hash_mask) instead of the slow
        // integer modulo to index the table with the incoming hash key.
        //
        // The lookup mask is either zero for an empty table, which is
        // the m_invalid_index_dummy[] in that case, so we always get
        // zero from the last AND and return the null_index that
        // m_invalid_index_dummy[] holds. Otherwise, when the hash_index table
        // is not empty lookup mask has all bits set to 1, so the last AND
        // simply yields (key & m_hash_mask), which is the right hash index.
        //
        return m_hash_buckets[key & m_hash_mask & m_lookup_mask];
    }

    index_type next(const index_type index) const
    {
        // The index chain is resized when new items are inserted
        // to match the largest index, so this check is mostly
        // for internal consistency.
        HASH_INDEX_ASSERT(static_cast<size_type>(index) < m_index_chain_size);
        return m_index_chain[index & m_lookup_mask];
    }

    template<typename ValueType, typename CollectionType, typename Predicate>
    index_type find(const key_type key, const ValueType & needle, const CollectionType & collection, Predicate pred) const
    {
        for (index_type i = first(key); i != null_index; i = next(i))
        {
            const auto & item = collection[i];
            if (pred(needle, item))
            {
                return i;
            }
        }
        return null_index;
    }

    template<typename ValueType, typename CollectionType>
    index_type find(const key_type key, const ValueType & needle, const CollectionType & collection) const
    {
        return find(key, needle, collection, std::equal_to<ValueType>{});
    }

    //
    // Insertion / removal:
    //

    void insert(const key_type key, const index_type index)
    {
        if (!is_allocated())
        {
            const size_type index_chain_size = ((static_cast<size_type>(index) >= m_index_chain_size) ?
                                                index + 1 : m_index_chain_size);
            internal_allocate(m_hash_buckets_size, index_chain_size);
        }
        else if (index >= m_index_chain_size)
        {
            resize_index_chain(index + 1);
        }

        const key_type k     = key & m_hash_mask;
        m_index_chain[index] = m_hash_buckets[k];
        m_hash_buckets[k]    = index;
    }

    void erase(const key_type key, const index_type index)
    {
        HASH_INDEX_ASSERT(static_cast<size_type>(index) < m_index_chain_size);

        if (!is_allocated())
        {
            return;
        }

        const key_type k = key & m_hash_mask;

        if (m_hash_buckets[k] == index)
        {
            m_hash_buckets[k] = m_index_chain[index];
        }
        else
        {
            for (index_type i = m_hash_buckets[k]; i != null_index; i = m_index_chain[i])
            {
                if (m_index_chain[i] == index)
                {
                    m_index_chain[i] = m_index_chain[index];
                    break;
                }
            }
        }

        m_index_chain[index] = null_index;
    }

    // Insert an entry into the index chain and add it to the hash, increasing all indexes >= index.
    void insert_at_index(const key_type key, const index_type index)
    {
        if (!is_allocated())
        {
            return;
        }

        size_type i;
        index_type max = index;

        for (i = 0; i < m_hash_buckets_size; ++i)
        {
            if (m_hash_buckets[i] >= index)
            {
                m_hash_buckets[i]++;
                if (m_hash_buckets[i] > max)
                {
                    max = m_hash_buckets[i];
                }
            }
        }

        for (i = 0; i < m_index_chain_size; ++i)
        {
            if (m_index_chain[i] >= index)
            {
                m_index_chain[i]++;
                if (m_index_chain[i] > max)
                {
                    max = m_index_chain[i];
                }
            }
        }

        if (max >= m_index_chain_size)
        {
            resize_index_chain(max + 1);
        }
        for (i = max; i > index; --i)
        {
            m_index_chain[i] = m_index_chain[i - 1];
        }
        m_index_chain[index] = null_index;

        insert(key, index);
    }

    // Remove an entry from the index chain and remove it from the hash, decreasing all indexes >= index.
    void erase_and_remove_index(const key_type key, const index_type index)
    {
        HASH_INDEX_ASSERT(static_cast<size_type>(index) < m_index_chain_size);

        if (!is_allocated())
        {
            return;
        }

        erase(key, index);

        size_type i;
        index_type max = index;

        for (i = 0; i < m_hash_buckets_size; ++i)
        {
            if (m_hash_buckets[i] >= index)
            {
                if (m_hash_buckets[i] > max)
                {
                    max = m_hash_buckets[i];
                }
                m_hash_buckets[i]--;
            }
        }

        for (i = 0; i < m_index_chain_size; ++i)
        {
            if (m_index_chain[i] >= index)
            {
                if (m_index_chain[i] > max)
                {
                    max = m_index_chain[i];
                }
                m_index_chain[i]--;
            }
        }

        for (i = index; i < max; ++i)
        {
            m_index_chain[i] = m_index_chain[i + 1];
        }
        m_index_chain[max] = null_index;
    }

    //
    // Memory management:
    //

    void clear() noexcept
    {
        if (m_hash_buckets != m_invalid_index_dummy)
        {
            const index_type fill_val = null_index;
            std::fill_n(m_hash_buckets, m_hash_buckets_size, fill_val);
        }
        // Clearing the index chain is not strictly necessary since
        // inserting new elements in the hash_index will overwrite
        // corresponding index chain entries.
    }

    void clear_and_resize(const size_type new_hash_buckets_size, const size_type new_index_chain_size)
    {
        HASH_INDEX_ASSERT(is_power_of_two(new_hash_buckets_size) && "Size of hash_index buckets array must be a power-of-2!");

        clear_and_free();
        m_hash_buckets_size = new_hash_buckets_size;
        m_index_chain_size  = new_index_chain_size;
    }

    void clear_and_free()
    {
        if (m_hash_buckets != m_invalid_index_dummy)
        {
            Allocator::deallocate(m_hash_buckets, m_hash_buckets_size);
            m_hash_buckets = m_invalid_index_dummy;
        }
        if (m_index_chain != m_invalid_index_dummy)
        {
            Allocator::deallocate(m_index_chain, m_index_chain_size);
            m_index_chain = m_invalid_index_dummy;
        }
        m_lookup_mask = 0;
    }

    void set_granularity(const size_type new_granularity)
    {
        HASH_INDEX_ASSERT(new_granularity > 0);
        m_granularity = new_granularity;
    }

    void resize_index_chain(const size_type new_index_chain_size)
    {
        if (new_index_chain_size <= m_index_chain_size)
        {
            return;
        }

        size_type new_size;
        const auto mod = new_index_chain_size % m_granularity;

        if (mod == 0)
        {
            new_size = new_index_chain_size;
        }
        else
        {
            new_size = new_index_chain_size + m_granularity - mod;
        }

        if (m_index_chain == m_invalid_index_dummy) // Not allocated yet; Defer.
        {
            m_index_chain_size = new_size;
            return;
        }

        const auto old_index_chain_size = m_index_chain_size;
        const auto old_index_chain      = m_index_chain;

        auto new_index_chain = Allocator::allocate(new_size);
        std::copy(old_index_chain, old_index_chain + old_index_chain_size, new_index_chain);

        // Newly allocated space must be filled with null_index
        const index_type fill_val = null_index;
        std::fill_n(new_index_chain + old_index_chain_size, new_size - old_index_chain_size, fill_val);

        Allocator::deallocate(old_index_chain, old_index_chain_size);
        m_index_chain = new_index_chain;
        m_index_chain_size = new_size;
    }

    //
    // Queries:
    //

    size_type compute_distribution_percentage() const
    {
        // Computes a number in the range [0,100] representing the spread over the hash table.
        // This method will allocate some temporary memory via std::vector.

        if (!is_allocated())
        {
            return 100;
        }

        long total_items = 0;
        std::vector<long> hash_items_count(m_hash_buckets_size);

        for (size_type i = 0; i < m_hash_buckets_size; ++i)
        {
            hash_items_count[i] = 0;
            for (index_type index = m_hash_buckets[i]; index != null_index; index = m_index_chain[index])
            {
                hash_items_count[i]++;
            }
            total_items += hash_items_count[i];
        }

        // If no items in the hash buckets...
        if (total_items <= 1)
        {
            return 100;
        }

        long error = 0;
        const long average = total_items / m_hash_buckets_size;

        for (size_type i = 0; i < m_hash_buckets_size; ++i)
        {
            long e = hash_items_count[i] - average;
            if (e < 0) { e = -e; } // absolute value of 'e'

            if (e > 1)
            {
                error += (e - 1);
            }
        }

        return 100 - (error * 100 / total_items);
    }

    size_type allocated_bytes() const noexcept
    {
        if (!is_allocated())
        {
            return 0;
        }
        return (m_hash_buckets_size * sizeof(index_type)) +
               (m_index_chain_size  * sizeof(index_type));
    }

    size_type hash_buckets_size() const noexcept
    {
        return m_hash_buckets_size;
    }

    size_type index_chain_size() const noexcept
    {
        return m_index_chain_size;
    }

    size_type granularity() const noexcept
    {
        return m_granularity;
    }

    bool is_allocated() const noexcept
    {
        return (m_hash_buckets != nullptr) &&
               (m_hash_buckets != m_invalid_index_dummy);
    }

    //
    // Deep comparison operators:
    //

    bool operator == (const hash_index & other) const noexcept
    {
        // Self comparison of comparison to pointer/ref to self?
        if (this == &other)
        {
            return true;
        }

        // Same sizes?
        if (m_hash_buckets_size != other.m_hash_buckets_size) { return false; }
        if (m_index_chain_size  != other.m_index_chain_size ) { return false; }
        if (m_hash_mask         != other.m_hash_mask        ) { return false; }
        if (m_lookup_mask       != other.m_lookup_mask      ) { return false; }
        if (m_granularity       != other.m_granularity      ) { return false; }

        // This or other could be pointing to the m_invalid_index_dummy.
        if ( is_allocated() && !other.is_allocated()) { return false; }
        if (!is_allocated() &&  other.is_allocated()) { return false; }

        // Same sizes, but do both have the same data?
        for (size_type i = 0; i < m_hash_buckets_size; ++i)
        {
            if (m_hash_buckets[i] != other.m_hash_buckets[i])
            {
                return false;
            }
        }
        for (size_type i = 0; i < m_index_chain_size; ++i)
        {
            if (m_index_chain[i] != other.m_index_chain[i])
            {
                return false;
            }
        }

        // The same sizes and contents.
        return true;
    }

    bool operator != (const hash_index & other) const noexcept
    {
        return !(*this == other);
    }

private:

    template<typename IntType>
    static bool is_power_of_two(const IntType num) noexcept
    {
        static_assert(std::is_integral<IntType>::value, "Integer type required!");
        return (num > 0) && ((num & (num - 1)) == 0);
    }

    void internal_init(const size_type initial_hash_buckets_size,
                       const size_type initial_index_chain_size)
    {
        HASH_INDEX_ASSERT(is_power_of_two(initial_hash_buckets_size) && "Size of hash_index buckets array must be a power-of-2!");
        HASH_INDEX_ASSERT(!is_allocated() && "Already initialized!");

        m_hash_buckets      = m_invalid_index_dummy;
        m_index_chain       = m_invalid_index_dummy;
        m_hash_buckets_size = initial_hash_buckets_size;
        m_index_chain_size  = initial_index_chain_size;
        m_hash_mask         = m_hash_buckets_size - 1;
        m_lookup_mask       = 0;
        m_granularity       = default_granularity;
    }

    void internal_allocate(const size_type new_hash_buckets_size,
                           const size_type new_index_chain_size)
    {
        HASH_INDEX_ASSERT(is_power_of_two(new_hash_buckets_size) && "Size of hash_index buckets array must be a power-of-2!");

        if (is_allocated())
        {
            clear_and_free();
        }

        m_hash_buckets      = Allocator::allocate(new_hash_buckets_size);
        m_index_chain       = Allocator::allocate(new_index_chain_size);
        m_hash_buckets_size = new_hash_buckets_size;
        m_index_chain_size  = new_index_chain_size;
        m_hash_mask         = m_hash_buckets_size - 1;
        m_lookup_mask       = ~static_cast<size_type>(0);

        const index_type fill_val = null_index;
        std::fill_n(m_hash_buckets, new_hash_buckets_size, fill_val);
        std::fill_n(m_index_chain,  new_index_chain_size,  fill_val);
    }

    //
    // m_hash_buckets[] is indexed directly from an incoming key ANDed with
    // m_hash_mask (which is just the cached result of m_hash_buckets_size - 1).
    // The resulting index will be either the index of the requested data or
    // an index into m_index_chain[]. Index chain will then either have the
    // index of the requested data or a successive chain of indexes to itself
    // until the data index is found or a null_index is encountered.
    // See first() and next() for more details on the lookup algorithm.
    //
    // m_hash_buckets[] can be of any power-of-two size (PoT is enforced).
    // m_index_chain[] must match the size of the external data array and
    // will be resized accordingly to accommodate the largest index inserted.
    //
    // Note: When the hash_index is empty, both m_hash_buckets and m_index_chain
    // will point to the m_invalid_index_dummy below. This allows simplifying some
    // things and avoiding additional IF checks for the empty hash_index case.
    //
    index_type * m_hash_buckets = nullptr;
    index_type * m_index_chain  = nullptr;
    size_type    m_hash_buckets_size = 0;
    size_type    m_index_chain_size  = 0;

    //
    // As mentioned above, m_hash_mask is the cached result of m_hash_buckets_size - 1.
    // Since we require a power-of-two size for the hash buckets array, we can use a
    // much cheaper AND to modulate the incoming hash key, but we need to AND it with
    // the num of buckets - 1, hence this cached value.
    //
    size_type m_hash_mask = 0;

    //
    // m_lookup_mask is a bit of magic... We use it to make the methods first() and
    // next() branch-less. When the hash_index is empty, the buckets and index chain
    // will point to the m_invalid_index_dummy[] array. In this case, we always want
    // the incoming hash key to map to index zero, so m_lookup_mask is set to zero
    // when hash_index is empty. It will get ANDed with the incoming hash key in
    // first() and next() yielding the 0th index of m_invalid_index_dummy[] (null_index),
    // so we've successfully managed to avoid an extra IF there for the empty case.
    // But when the table is not empty, buckets and the index chain will point to
    // heap-allocated arrays, so ANDing m_lookup_mask with a hash key should not
    // alter the resulting array index. Therefore, when the table is not empty,
    // m_lookup_mask is set to the bit pattern ~0 (0xFFFFF... or all 1s) so that
    // ANDing anything with it will just yield back the original input.
    //
    size_type m_lookup_mask = 0;

    //
    // Factor used to resize the index chain on demand.
    // Works best if using a power-of-two, but not required.
    //
    size_type m_granularity = 0;

    //
    // The initial empty hash_index allocates no heap memory, but to simplify
    // handling of the empty case we still want the hash buckets and
    // index chain to point to some array with at least one null_index
    // in it. That's where this placeholder comes in. The data in it
    // will not be modified by the hash_index, but since the m_hash_buckets
    // and m_index_chain pointers are not const, this has to be kept mutable.
    //
    static index_type m_invalid_index_dummy[1];
};

// This abomination here is the m_invalid_index_dummy[] static initializer:
template<typename IT, typename KT, typename ST, typename AT>
typename hash_index<IT, KT, ST, AT>::index_type hash_index<IT, KT, ST, AT>::m_invalid_index_dummy[1] = {
    hash_index<IT, KT, ST, AT>::null_index
};

#endif // HASH_INDEX_HPP
