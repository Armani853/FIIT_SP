#ifndef MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BOUNDARY_TAGS_H
#define MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BOUNDARY_TAGS_H

#include <allocator_test_utils.h>
#include <allocator_with_fit_mode.h>
#include <pp_allocator.h>
#include <cstring>
#include <iterator>
#include <mutex>

class allocator_boundary_tags final :
    public smart_mem_resource,
    public allocator_test_utils,
    public allocator_with_fit_mode
{

private:

    void *_trusted_memory;

    struct allocator_meta {
        std::pmr::memory_resource* parent_allocator;
        allocator_with_fit_mode::fit_mode fit_mode;
        size_t total_size;
        std::mutex sync_mutex;
        void* first_block;
    };

    struct block_header {
        size_t size;
        void* prev_block;
        void* next_block;
        size_t is_free;
    };

    struct block_footer {
        size_t size;
    };

    static constexpr size_t HEADER_SIZE = sizeof(block_header);
    static constexpr size_t FOOTER_SIZE = 0;
    static constexpr size_t FULL_BLOCK_META_SIZE = HEADER_SIZE + FOOTER_SIZE;

    static constexpr const size_t allocator_metadata_size = sizeof(allocator_meta);

    static constexpr const size_t occupied_block_metadata_size = sizeof(size_t) + sizeof(void*) + sizeof(void*) + sizeof(void*);

    static constexpr const size_t free_block_metadata_size = 0;


    static allocator_meta* get_meta(void* trusted) {
        return static_cast<allocator_meta*>(trusted);
    }

    static block_header* get_header(void* block_start) {
        return static_cast<block_header*>(block_start);
    }    

    static block_footer* get_footer(void* block_start) {
        return reinterpret_cast<block_footer*>(static_cast<char*>(block_start) + get_header(block_start)->size);
    }

    static void* get_user_ptr(void* block_start) {
        return static_cast<char*>(block_start) + HEADER_SIZE;
    }

    static void* get_block_start(void* user_ptr) {
        return static_cast<char*>(user_ptr) - HEADER_SIZE;
    }

    static void* get_next_block(void* block_start) {
        return static_cast<char*>(block_start) + get_header(block_start)->size;
    }

    static void* get_prev_block(void* block_start, void* trusted) {
        if (static_cast<char*>(trusted) + allocator_metadata_size == block_start) {
            return nullptr;
        }
        return get_header(block_start)->prev_block;
    }

    static bool is_block_free(void* block_start) {
        return get_header(block_start)->is_free;
    }


public:
    
    ~allocator_boundary_tags() override;
    
    allocator_boundary_tags(allocator_boundary_tags const &other);
    
    allocator_boundary_tags &operator=(allocator_boundary_tags const &other);
    
    allocator_boundary_tags(
        allocator_boundary_tags &&other) noexcept;
    
    allocator_boundary_tags &operator=(
        allocator_boundary_tags &&other) noexcept;

public:
    
    explicit allocator_boundary_tags(
            size_t space_size,
            std::pmr::memory_resource *parent_allocator = nullptr,
            allocator_with_fit_mode::fit_mode allocate_fit_mode = allocator_with_fit_mode::fit_mode::first_fit);

private:
    
    [[nodiscard]] void *do_allocate_sm(
        size_t bytes) override;
    
    void do_deallocate_sm(
        void *at) override;

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

public:
    
    inline void set_fit_mode(
        allocator_with_fit_mode::fit_mode mode) override;

public:
    
    std::vector<allocator_test_utils::block_info> get_blocks_info() const override;

private:

    std::vector<allocator_test_utils::block_info> get_blocks_info_inner() const override;

/** TODO: Highly recommended for helper functions to return references */

    class boundary_iterator
    {
        void* _occupied_ptr;
        bool _occupied;
        void* _trusted_memory;

    public:

        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = void*;
        using reference = void*&;
        using pointer = void**;
        using difference_type = ptrdiff_t;

        bool operator==(const boundary_iterator&) const noexcept;

        bool operator!=(const boundary_iterator&) const noexcept;

        boundary_iterator& operator++() & noexcept;

        boundary_iterator& operator--() & noexcept;

        boundary_iterator operator++(int n);

        boundary_iterator operator--(int n);

        size_t size() const noexcept;

        bool occupied() const noexcept;

        void* operator*() const noexcept;

        void* get_ptr() const noexcept;

        boundary_iterator();

        boundary_iterator(void* trusted);
    };

    friend class boundary_iterator;

    boundary_iterator begin() const noexcept;

    boundary_iterator end() const noexcept;
};

#endif //MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BOUNDARY_TAGS_H