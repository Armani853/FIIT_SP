#ifndef MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BUDDIES_SYSTEM_H
#define MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BUDDIES_SYSTEM_H

#include <pp_allocator.h>
#include <allocator_test_utils.h>
#include <allocator_with_fit_mode.h>
#include <cstring>
#include <mutex>
#include <cmath>

namespace __detail
{
    constexpr size_t nearest_greater_k_of_2(size_t size) noexcept
    {
        int ones_counter = 0, index = -1;

        constexpr const size_t o = 1;

        for (int i = sizeof(size_t) * 8 - 1; i >= 0; --i)
        {
            if (size & (o << i))
            {
                if (ones_counter == 0)
                    index = i;
                ++ones_counter;
            }
        }

        return ones_counter <= 1 ? index : index + 1;
    }
}

class allocator_buddies_system final:
    public smart_mem_resource,
    public allocator_test_utils,
    public allocator_with_fit_mode
{

private:


    struct block_metadata
    {
        bool occupied : 1;
        unsigned char size : 7;
    };

    void *_trusted_memory;

    struct allocator_meta {
        allocator_dbg_helper* dbg_helper;
        allocator_with_fit_mode::fit_mode fit_mode;
        std::pmr::memory_resource* parent_allocator;
        unsigned char max_k;
        std::mutex sync_mutex;
    };

    static allocator_meta* get_meta(void* trusted) {
        return static_cast<allocator_meta*>(trusted);
    }

    static void** get_free_lists(void* trusted) {
        return reinterpret_cast<void**>(static_cast<char*>(trusted) + sizeof(allocator_meta));
    }

    static void*& get_free_list(void* trusted, unsigned char k) {
        auto* meta = get_meta(trusted);
        void** base = get_free_lists(trusted);
        size_t index = k - min_k;
        return base[index];
    }

    static size_t block_size_from_k(unsigned char k) {
        return static_cast<size_t>(1) << k;
    }

    static unsigned char get_k_for_size(size_t size, unsigned char max_k) {
        unsigned char k = min_k;
        while (block_size_from_k(k) < size && k <= max_k) {
            k++;
        }
        return k;
    }

    static void* get_buddy(void* block, size_t block_size, void* first_block_addr) {
        uintptr_t relative_addr = reinterpret_cast<uintptr_t>(block) - reinterpret_cast<uintptr_t>(first_block_addr);
        uintptr_t buddy_relative_addr = relative_addr ^ block_size;
        void* buddy = reinterpret_cast<void*>(buddy_relative_addr + reinterpret_cast<uintptr_t>(first_block_addr));
        return buddy;
    }

    static block_metadata* get_block_metadata(void* block) {
        return static_cast<block_metadata*>(block);
    }

    static bool is_block_occupied(void* block) {
        return static_cast<block_metadata*>(block)->occupied;
    }

    static unsigned char get_block_k(void* block) {
        return static_cast<block_metadata*>(block)->size;
    }

    static void set_block_metadata(void* block, bool occupied, unsigned char k) {
        auto* meta = get_block_metadata(block);
        meta->occupied = occupied;
        meta->size = k;
    }

    static void*& get_block_next(void* block) {
        return *reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(block_metadata));
    }

    static void* get_user_ptr(void* block) {
        return static_cast<char*>(block) + occupied_block_metadata_size;
    }

    static void* get_block_ptr(void* user_ptr) {
        return static_cast<char*>(user_ptr) - occupied_block_metadata_size;
    }

    void insert_into_free_list(void* trusted, void* block) {
        unsigned char k = get_block_k(block);
        void*& head = get_free_list(trusted, k);
        get_block_next(block) = head;
        head = block;
    }

    void remove_from_free_list(void* trusted, void* block, void* prev) {
        if (!block) return;
        unsigned char k = get_block_k(block);
        void*& head = get_free_list(trusted, k);
        if (prev == nullptr && head != block) {
            prev = find_prev_in_free_list(trusted, block);
        }
        if (head == block) {
            head = get_block_next(block);
        } else if (prev != nullptr) {
            get_block_next(prev) = get_block_next(block);
        } else {
            return; 
        }
        get_block_next(block) = nullptr;
    }

    void* find_prev_in_free_list(void* trusted, void* block) {
        if (!block) {
            return nullptr;
        }
        unsigned char k = get_block_k(block);
        void* curr = get_free_list(trusted, k);
        void* prev = nullptr;
        while (curr && curr != block) {
            prev = curr;
            curr = get_block_next(curr);
        }
        return (curr == block) ? prev : nullptr;
    }

    static constexpr const size_t allocator_metadata_size = sizeof(allocator_meta);

    static constexpr const size_t occupied_block_metadata_size = sizeof(block_metadata) + sizeof(void*);
    
    static constexpr const size_t free_block_metadata_size = sizeof(block_metadata);

    static constexpr const size_t min_k = __detail::nearest_greater_k_of_2(occupied_block_metadata_size);

public:

    explicit allocator_buddies_system(
            size_t space_size_power_of_two,
            std::pmr::memory_resource *parent_allocator = nullptr,
            allocator_with_fit_mode::fit_mode allocate_fit_mode = allocator_with_fit_mode::fit_mode::first_fit);

    allocator_buddies_system(
        allocator_buddies_system const &other);
    
    allocator_buddies_system &operator=(
        allocator_buddies_system const &other);
    
    allocator_buddies_system(
        allocator_buddies_system &&other) noexcept;
    
    allocator_buddies_system &operator=(
        allocator_buddies_system &&other) noexcept;

    ~allocator_buddies_system() override;

private:
    
    [[nodiscard]] void *do_allocate_sm(
        size_t size) override;
    
    void do_deallocate_sm(
        void *at) override;

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    inline void set_fit_mode(
        allocator_with_fit_mode::fit_mode mode) override;


    std::vector<allocator_test_utils::block_info> get_blocks_info() const noexcept override;

private:

    std::vector<allocator_test_utils::block_info> get_blocks_info_inner() const override;


    
    class buddy_iterator
    {
        void* _block;

    public:

        using iterator_category = std::forward_iterator_tag;
        using value_type = void*;
        using reference = void*&;
        using pointer = void**;
        using difference_type = ptrdiff_t;

        bool operator==(const buddy_iterator&) const noexcept;

        bool operator!=(const buddy_iterator&) const noexcept;

        buddy_iterator& operator++() & noexcept;

        buddy_iterator operator++(int n);

        size_t size() const noexcept;

        bool occupied() const noexcept;

        void* operator*() const noexcept;

        buddy_iterator();

        buddy_iterator(void* start);
    };

    friend class buddy_iterator;

    buddy_iterator begin() const noexcept;

    buddy_iterator end() const noexcept;
    
};

#endif //MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BUDDIES_SYSTEM_H
