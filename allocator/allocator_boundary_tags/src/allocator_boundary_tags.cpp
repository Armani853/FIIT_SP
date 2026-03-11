#include <not_implemented.h>
#include "../include/allocator_boundary_tags.h"

allocator_boundary_tags::~allocator_boundary_tags()
{
    if (!_trusted_memory) {
        return;
    }
    auto* meta = get_meta(_trusted_memory);
    meta->sync_mutex.~mutex();
    meta->parent_allocator->deallocate(_trusted_memory, meta->total_size, alignof(std::max_align_t));
    _trusted_memory = nullptr;
}

allocator_boundary_tags::allocator_boundary_tags(
    allocator_boundary_tags &&other) noexcept : _trusted_memory(other._trusted_memory)
{
    other._trusted_memory = nullptr;
}

allocator_boundary_tags &allocator_boundary_tags::operator=(
    allocator_boundary_tags &&other) noexcept
{
    if (this != &other) {
        this->~allocator_boundary_tags();
        _trusted_memory = other._trusted_memory;
        other._trusted_memory = nullptr;
    }
    return *this;
}


/** If parent_allocator* == nullptr you should use std::pmr::get_default_resource()
 */
allocator_boundary_tags::allocator_boundary_tags(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    if (parent_allocator == nullptr) {
        parent_allocator = std::pmr::get_default_resource();
    }
    size_t full_size = allocator_metadata_size + space_size;
    void* raw_memory = parent_allocator->allocate(full_size, alignof(std::max_align_t));
    if (!raw_memory) {
        throw std::bad_alloc();
    }
    _trusted_memory = raw_memory;
    auto* meta = new (_trusted_memory) allocator_meta {
        parent_allocator,
        allocate_fit_mode,
        full_size,
        {},
        nullptr
    };
    void* first_block = allocator_metadata_size + static_cast<char*>(_trusted_memory);
    meta->first_block = first_block;
    block_header* header = get_header(first_block);
    header->size = space_size;
    header->prev_block = nullptr;
    header->next_block = nullptr;
    header->is_free = true;
    // block_footer* footer = get_footer(first_block);
    // footer->size = space_size;
}

[[nodiscard]] void *allocator_boundary_tags::do_allocate_sm(
    size_t size)
{
    if (size == 0) {
        size = 0;
    }
    auto* meta = get_meta(_trusted_memory);
    std::lock_guard<std::mutex> lock(meta->sync_mutex);
    size_t required_size = size + FULL_BLOCK_META_SIZE;
    void* best_block = nullptr;
    size_t best_remainder = SIZE_MAX;
    size_t best_size = 0;
    void* curr = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    void* end = static_cast<char*>(_trusted_memory) + meta->total_size;
    bool found = false;
    while (curr < end) {
        block_header* header = get_header(curr);
        if (header->is_free && header->size >= required_size) {
            size_t remainder = header->size - required_size;
            switch (meta->fit_mode) {
                case allocator_with_fit_mode::fit_mode::first_fit:
                    best_block = curr;
                    found = true;
                    break;
                case allocator_with_fit_mode::fit_mode::the_best_fit:
                    if (remainder < best_remainder) {
                        best_remainder = remainder;
                        best_block = curr;
                    }
                    break;
                case allocator_with_fit_mode::fit_mode::the_worst_fit:
                    if (header->size > best_size) {
                        best_size = header->size;
                        best_block = curr;
                    }
                    break;
            }
        }
        if (found) {
            break;
        }
        curr = static_cast<char*>(curr) + header->size;
    }
    if (!best_block) {
        throw std::bad_alloc();
    }
    curr = best_block;
    block_header* header = get_header(curr);
    size_t remaining = header->size - required_size;
    if (remaining >= FULL_BLOCK_META_SIZE + 1) {
        void* new_block = static_cast<char*>(curr) + required_size;
        block_header* new_header = get_header(new_block);
        new_header->size = remaining;
        new_header->prev_block = curr;
        new_header->next_block = header->next_block;
        new_header->is_free = true;
        // block_footer* new_footer = get_footer(new_block);
        // new_footer->size = remaining;
        header->size = required_size;
        header->next_block = new_block;
        if (new_header->next_block) {
            block_header* next_header = get_header(new_header->next_block);
            next_header->prev_block = new_block;
        }
    }
    header->is_free = false;
    // block_footer* footer = get_footer(curr);
    // footer->size = header->size;
    return get_user_ptr(curr);
}

void allocator_boundary_tags::do_deallocate_sm(
    void *at)
{
    if (!at) {
        return;
    }
    auto* meta = get_meta(_trusted_memory);
    std::lock_guard<std::mutex> lock(meta->sync_mutex);
    void* block = get_block_start(at);
    void* start = _trusted_memory;
    void* end = static_cast<char*>(_trusted_memory) + meta->total_size;
    if (block < start || block >= end) {
        return;
    }
    block_header* header = get_header(block);
    header->is_free = true;
    if (header->next_block) {
        block_header* next_header = get_header(header->next_block);
        if (next_header->is_free) {
            header->size += next_header->size;
            header->next_block = next_header->next_block;
            if (header->next_block) {
                block_header* new_next = get_header(header->next_block);
                new_next->prev_block = block;
            }
            // block_footer* footer = get_footer(block);
            // footer->size = header->size;
        }
    }
    if (header->prev_block) {
        block_header* prev_header = get_header(header->prev_block);
        if (prev_header->is_free) {
            prev_header->size += header->size;
            prev_header->next_block = header->next_block;
            if (prev_header->next_block) {
                block_header* new_next = get_header(prev_header->next_block);
                new_next->prev_block = header->prev_block;
            }
            // block_footer* prev_footer = get_footer(header->prev_block);
            // prev_footer->size = prev_header->size;
        }
    }
}

inline void allocator_boundary_tags::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    auto* meta = get_meta(_trusted_memory);
    std::lock_guard<std::mutex> lock(meta->sync_mutex);
    meta->fit_mode = mode;
}


std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info() const
{
    return get_blocks_info_inner();
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::begin() const noexcept
{
    return boundary_iterator(_trusted_memory);
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::end() const noexcept
{
    return boundary_iterator();
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> result;
    if (!_trusted_memory) {
        return result;
    }
    auto* meta = get_meta(_trusted_memory);
    void* curr = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    void* end = static_cast<char*>(_trusted_memory) + meta->total_size;
    while (curr < end) {
        block_header* header = get_header(curr);
        allocator_test_utils::block_info info;
        info.block_size = header->size;
        info.is_block_occupied = !header->is_free;
        result.push_back(info);
        curr = static_cast<char*>(curr) + header->size;
    }
    return result;
}

allocator_boundary_tags::allocator_boundary_tags(const allocator_boundary_tags &other)
{
    if (!other._trusted_memory) {
        _trusted_memory = nullptr;
        return;
    }
    auto* other_meta = get_meta(other._trusted_memory);
    std::lock_guard<std::mutex> lock(other_meta->sync_mutex);
    _trusted_memory = other_meta->parent_allocator->allocate(other_meta->total_size, alignof(std::max_align_t));
    if (!_trusted_memory) {
        throw std::bad_alloc();
    }
    std::memcpy(_trusted_memory, other._trusted_memory, other_meta->total_size);
    auto* meta = get_meta(_trusted_memory);
    new (&meta->sync_mutex) std::mutex; 
    ptrdiff_t offset = static_cast<char*>(_trusted_memory) - static_cast<char*>(other._trusted_memory);
    if (meta->first_block) {
        meta->first_block = static_cast<char*>(meta->first_block) + offset;
        void* curr = meta->first_block;
        void* end = static_cast<char*>(_trusted_memory) + meta->total_size;
        while (curr && curr < end) {
            block_header* header = get_header(curr);
            if (header->prev_block) {
                header->prev_block = static_cast<char*>(header->prev_block) + offset;
            }
            if (header->next_block) {
                header->next_block = static_cast<char*>(header->next_block) + offset;
            }
            curr = header->next_block;
        }  
    }
}

allocator_boundary_tags &allocator_boundary_tags::operator=(const allocator_boundary_tags &other)
{
    if (this != &other) {
        this->~allocator_boundary_tags();
        new (this) allocator_boundary_tags(other);
    }
    return *this;
}

bool allocator_boundary_tags::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    return this == &other;
}

bool allocator_boundary_tags::boundary_iterator::operator==(
        const allocator_boundary_tags::boundary_iterator &other) const noexcept
{
    return _occupied_ptr == other._occupied_ptr;
}

bool allocator_boundary_tags::boundary_iterator::operator!=(
        const allocator_boundary_tags::boundary_iterator & other) const noexcept
{
    return !(*this == other);
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator++() & noexcept
{
    if (!_occupied_ptr || !_trusted_memory) {
        return *this;
    }
    void* next = get_next_block(_occupied_ptr);
    void* end = static_cast<char*>(_trusted_memory) + get_meta(_trusted_memory)->total_size;
    if (next == nullptr || next >= end) {
        _occupied_ptr = nullptr;
        _occupied = false;
    } else {
        _occupied_ptr = get_next_block(_occupied_ptr);
        _occupied = !is_block_free(_occupied_ptr);
    }
    return *this;
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator--() & noexcept
{
    if (!_occupied_ptr || !_trusted_memory) {
        return *this;
    }
    void* prev = get_prev_block(_occupied_ptr, _trusted_memory);
    if (prev == nullptr) {
        _occupied_ptr = nullptr;
        _occupied = false;
    } else {
        _occupied_ptr = get_prev_block(_occupied_ptr, _trusted_memory);
        _occupied = !is_block_free(_occupied_ptr);
    }
    return *this;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator++(int n)
{
    boundary_iterator tmp = *this;
    ++(*this);
    return tmp;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator--(int n)
{
    boundary_iterator tmp = *this;
    --(*this);
    return tmp;
}

size_t allocator_boundary_tags::boundary_iterator::size() const noexcept
{
    return _occupied_ptr ? get_header(_occupied_ptr)->size - FULL_BLOCK_META_SIZE : 0;
}

bool allocator_boundary_tags::boundary_iterator::occupied() const noexcept
{
    return _occupied;
}

void* allocator_boundary_tags::boundary_iterator::operator*() const noexcept
{
    return _occupied_ptr ? get_user_ptr(_occupied_ptr) : nullptr;
}

allocator_boundary_tags::boundary_iterator::boundary_iterator(): _occupied_ptr(nullptr), _occupied(false), _trusted_memory(nullptr)
{

}

allocator_boundary_tags::boundary_iterator::boundary_iterator(void *trusted): _occupied_ptr(trusted ? static_cast<char*>(trusted) + allocator_metadata_size : nullptr), _occupied(false), _trusted_memory(trusted)
{
    if (_occupied_ptr) {
        _occupied = !is_block_free(_occupied_ptr);
    }
}

void *allocator_boundary_tags::boundary_iterator::get_ptr() const noexcept
{
    return _occupied_ptr;
}

