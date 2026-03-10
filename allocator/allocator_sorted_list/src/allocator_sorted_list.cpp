#include <not_implemented.h>
#include "../include/allocator_sorted_list.h"

allocator_sorted_list::~allocator_sorted_list()
{
    if (!_trusted_memory) {
        return;
    }
    auto* meta = get_meta(_trusted_memory);
    meta->sync_mutex.~mutex();
    if (meta->parent_allocator) {
        meta->parent_allocator->deallocate(_trusted_memory, meta->total_size, alignof(std::max_align_t));
    } else {
        ::operator delete(_trusted_memory);
    }
    _trusted_memory = nullptr;
}

allocator_sorted_list::allocator_sorted_list(
    allocator_sorted_list &&other) noexcept :_trusted_memory(other._trusted_memory)
{
    other._trusted_memory = nullptr;
}

allocator_sorted_list &allocator_sorted_list::operator=(
    allocator_sorted_list &&other) noexcept
{

    if (this != &other) {
        this->~allocator_sorted_list();
        _trusted_memory = other._trusted_memory;
        other._trusted_memory = nullptr;
    }
    return *this;
}

allocator_sorted_list::allocator_sorted_list(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    size_t full_size = allocator_metadata_size + space_size;
    void* raw_memory = nullptr;
    if (parent_allocator) {
        raw_memory = parent_allocator->allocate(full_size, alignof(std::max_align_t));
        if (!raw_memory) {
            throw std::bad_alloc();
        }
    } else {
        raw_memory = ::operator new(full_size, std::nothrow);
        if (!raw_memory) {
            throw std::bad_alloc();
        }
    }
    _trusted_memory = raw_memory;
    auto* meta = new (_trusted_memory) allocator_meta {
        parent_allocator,
        allocate_fit_mode,
        full_size,
        {},
        nullptr,
    };
    void* first_block = allocator_metadata_size + static_cast<char*>(_trusted_memory);
    meta->first_free_block = first_block;
    get_block_size(first_block) = space_size;
    get_block_next(first_block) = nullptr;
}

[[nodiscard]] void *allocator_sorted_list::do_allocate_sm(
    size_t size)
{
    if (size == 0) {
        return nullptr;
    }
    auto* meta = get_meta(_trusted_memory);
    std::lock_guard<std::mutex> lock(meta->sync_mutex);
    size_t required_size = size + block_metadata_size;
    void* best_prev = nullptr;
    void* best_block = nullptr;
    size_t best_remainder = SIZE_MAX;
    size_t best_size = 0;
    void* prev = nullptr;
    void* curr = meta->first_free_block;
    bool found = false;
    while (curr && !found) {
        size_t b_size = get_block_size(curr);
        if (b_size >= required_size) {
            size_t remainder = b_size - required_size;
            switch(meta->fit_mode) {
                case allocator_with_fit_mode::fit_mode::first_fit:
                    best_block = curr;
                    best_prev = prev;
                    found = true;
                    break;
                case allocator_with_fit_mode::fit_mode::the_best_fit:
                    if (remainder < best_remainder) {
                        best_remainder = remainder;
                        best_block = curr;
                        best_prev = prev;
                    }
                    break;
                case allocator_with_fit_mode::fit_mode::the_worst_fit:
                    if (b_size > best_size) {
                        best_size = b_size;
                        best_block = curr;
                        best_prev = prev;
                    }                
                    break;
            }
        }
        if (found) {
            break;
        }
        prev = curr;
        curr = get_block_next(curr);
    }

    if (!best_block) {
        throw std::bad_alloc();
    }
    curr = best_block;
    prev = best_prev;
    size_t block_size = get_block_size(curr);
    size_t remaining = block_size - required_size;
    if (remaining >= block_metadata_size + 1) {
        void* new_block = static_cast<char*>(curr) + required_size;
        get_block_size(new_block) = remaining;
        get_block_next(new_block) = get_block_next(curr);
        get_block_size(curr) = required_size;

        if (prev) {
            get_block_next(prev) = new_block;
        } else {
            meta->first_free_block = new_block;
        }
    } else {
        if (prev) {
            get_block_next(prev) = get_block_next(curr);
        } else {
            meta->first_free_block = get_block_next(curr);
        }        
    }
    return get_user_ptr(curr);
}

allocator_sorted_list::allocator_sorted_list(const allocator_sorted_list &other)
{
    if (!other._trusted_memory) {
        _trusted_memory = nullptr;
        return;
    }
    auto* other_meta = get_meta(other._trusted_memory);
    std::lock_guard<std::mutex> lock(other_meta->sync_mutex);
    if (other_meta->parent_allocator) {
        _trusted_memory = other_meta->parent_allocator->allocate(other_meta->total_size, alignof(std::max_align_t));
    } else {
        _trusted_memory = ::operator new(other_meta->total_size, std::nothrow);
    }
    if (!_trusted_memory) {
        throw std::bad_alloc();
    }
    std::memcpy(_trusted_memory, other._trusted_memory, other_meta->total_size);
    auto* meta = get_meta(_trusted_memory);
    new (&meta->sync_mutex) std::mutex();
    ptrdiff_t offset = static_cast<char*>(_trusted_memory) - static_cast<char*>(other._trusted_memory);
    if (meta->first_free_block != nullptr) {
        meta->first_free_block = static_cast<char*>(meta->first_free_block) + offset;
        void* curr = meta->first_free_block;
        while (curr != nullptr) {
            void*& next = get_block_next(curr);
            if (next != nullptr) {
                next = static_cast<char*>(next) + offset;
            }
            curr = next;
        }
    }
}

allocator_sorted_list &allocator_sorted_list::operator=(const allocator_sorted_list &other)
{
    if (this != &other) {
        this->~allocator_sorted_list();
        new (this) allocator_sorted_list(other);
    }
    return *this;
}

bool allocator_sorted_list::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    return this == &other;
}

void allocator_sorted_list::do_deallocate_sm(
    void *at)
{
    if (!at) {
        return;
    }
    auto* meta = get_meta(_trusted_memory);
    std::lock_guard<std::mutex> lock(meta->sync_mutex);
    void* block = get_block_ptr(at);
    void* start = _trusted_memory;
    void* end = static_cast<char*>(_trusted_memory) + meta->total_size;
    if (block < start || block >= end) {
        return;
    }
    void* prev = nullptr;
    void* curr = meta->first_free_block;
    while (curr && curr < block) {
        prev = curr;
        curr = get_block_next(curr);
    }
    get_block_next(block) = curr;
    if (prev) {
        get_block_next(prev) = block;
    } else {
        meta->first_free_block = block;
    }
    if (curr && static_cast<char*>(block) + get_block_size(block) == static_cast<char*>(curr)) {
        get_block_size(block) += get_block_size(curr);
        get_block_next(block) = get_block_next(curr);
    }
    if (prev && static_cast<char*>(block) == static_cast<char*>(prev) + get_block_size(prev)) {
        get_block_size(prev) += get_block_size(block);
        get_block_next(prev) = get_block_next(block);
    }
}

inline void allocator_sorted_list::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    auto* meta = get_meta(_trusted_memory);
    std::lock_guard<std::mutex> lock(meta->sync_mutex);
    meta->fit_mode = mode;
}

std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info() const noexcept
{
    return get_blocks_info_inner();
}


std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> result;
    if (!_trusted_memory) {
        return result;
    }
    auto* meta = get_meta(_trusted_memory);
    void* curr = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    void* end = static_cast<char*>(_trusted_memory) + meta->total_size;
    while (curr < end) {
        allocator_test_utils::block_info info;
        info.block_size = get_block_size(curr) - block_metadata_size;
        bool is_free = false;
        void* free_curr = meta->first_free_block;
        while (free_curr != nullptr) {
            if (free_curr == curr) {
                is_free = true;
                break;
            }
            free_curr = get_block_next(free_curr);
        }
        info.is_block_occupied = !is_free;
        result.push_back(info);
        curr = static_cast<char*>(curr) + get_block_size(curr);
    }
    return result;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_begin() const noexcept
{
    return sorted_free_iterator(_trusted_memory);
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_end() const noexcept
{
    return sorted_free_iterator();
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::begin() const noexcept
{
    return sorted_iterator(_trusted_memory);
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::end() const noexcept
{
    return sorted_iterator();
}


bool allocator_sorted_list::sorted_free_iterator::operator==(
        const allocator_sorted_list::sorted_free_iterator & other) const noexcept
{
    return _free_ptr == other._free_ptr;
}

bool allocator_sorted_list::sorted_free_iterator::operator!=(
        const allocator_sorted_list::sorted_free_iterator &other) const noexcept
{
    return !(*this == other);
}

allocator_sorted_list::sorted_free_iterator &allocator_sorted_list::sorted_free_iterator::operator++() & noexcept
{
    if (_free_ptr) {
        _free_ptr = get_block_next(_free_ptr);
    }
    return *this;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::sorted_free_iterator::operator++(int n)
{
    sorted_free_iterator tmp = *this;
    ++(*this);
    return tmp;
}

size_t allocator_sorted_list::sorted_free_iterator::size() const noexcept
{
    return (_free_ptr ? get_block_size(_free_ptr) - block_metadata_size : 0); 
}

void *allocator_sorted_list::sorted_free_iterator::operator*() const noexcept
{
    return _free_ptr;
}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator(): _free_ptr(nullptr)
{

}


allocator_sorted_list::sorted_free_iterator::sorted_free_iterator(void *trusted): _free_ptr(trusted ? (get_meta(trusted))->first_free_block : nullptr)
{

}

bool allocator_sorted_list::sorted_iterator::operator==(const allocator_sorted_list::sorted_iterator & other) const noexcept
{
    return _current_ptr == other._current_ptr;
}

bool allocator_sorted_list::sorted_iterator::operator!=(const allocator_sorted_list::sorted_iterator &other) const noexcept
{
    return !(*this == other);
}

allocator_sorted_list::sorted_iterator &allocator_sorted_list::sorted_iterator::operator++() & noexcept
{
    if (_current_ptr && _trusted_memory) {
        _current_ptr = static_cast<char*>(_current_ptr) + get_block_size(_current_ptr);
        void* end = static_cast<char*>(_trusted_memory) + get_meta(_trusted_memory)->total_size;
        if (_current_ptr >= end) {
            _current_ptr = nullptr;
        }
    }
    return *this;
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::sorted_iterator::operator++(int n)
{
    sorted_iterator tmp = *this;
    ++(*this);
    return tmp;
}

size_t allocator_sorted_list::sorted_iterator::size() const noexcept
{
    return _current_ptr ? get_block_size(_current_ptr) - block_metadata_size : 0;
}

void *allocator_sorted_list::sorted_iterator::operator*() const noexcept
{
    return get_user_ptr(_current_ptr);
}

allocator_sorted_list::sorted_iterator::sorted_iterator(): _free_ptr(nullptr), _current_ptr(nullptr), _trusted_memory(nullptr)
{

}

allocator_sorted_list::sorted_iterator::sorted_iterator(void *trusted): _free_ptr(nullptr), _current_ptr(trusted ? static_cast<char*>(trusted) + allocator_metadata_size : nullptr), _trusted_memory(trusted)
{

}

bool allocator_sorted_list::sorted_iterator::occupied() const noexcept
{
    if (!_trusted_memory || !_current_ptr) {
        return false;
    }
    auto* meta = get_meta(_trusted_memory);
    void* free_curr = meta->first_free_block;
    while (free_curr) {
        if (free_curr == _current_ptr) {
            return false;
        }
        free_curr = get_block_next(free_curr);
    }
    return true;
}
