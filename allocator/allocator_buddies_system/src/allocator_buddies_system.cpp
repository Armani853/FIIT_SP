#include <not_implemented.h>
#include <cstddef>
#include "../include/allocator_buddies_system.h"

allocator_buddies_system::~allocator_buddies_system()
{
    if (!_trusted_memory) {
        return;
    }
    auto* meta = get_meta(_trusted_memory);
    size_t levels_count = meta->max_k - min_k + 1;
    size_t actual_space = static_cast<size_t>(1) << meta->max_k;
    size_t full_size = allocator_metadata_size + sizeof(void*) * levels_count + actual_space;
    meta->sync_mutex.~mutex();
    meta->parent_allocator->deallocate(_trusted_memory, full_size, alignof(std::max_align_t));
    _trusted_memory = nullptr;
}

allocator_buddies_system::allocator_buddies_system(
    allocator_buddies_system &&other) noexcept : _trusted_memory(other._trusted_memory)
{
    other._trusted_memory = nullptr;
}

allocator_buddies_system &allocator_buddies_system::operator=(
    allocator_buddies_system &&other) noexcept
{
    if (this != &other) {
        this->~allocator_buddies_system();
        _trusted_memory = other._trusted_memory;
        other._trusted_memory = nullptr;
    }
    return *this;
}

allocator_buddies_system::allocator_buddies_system(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    if (space_size < (1 << min_k)) {
        throw std::logic_error("Размер мал");
    }
    if (parent_allocator == nullptr) {
        parent_allocator = std::pmr::get_default_resource();
    }
    size_t actual_space = 1;
    unsigned char max_k = 0;
    while (actual_space < space_size) {
        actual_space <<= 1;
        max_k++;
    }
    size_t levels_count = max_k - min_k + 1;
    size_t full_size = allocator_metadata_size + sizeof(void*) * levels_count + actual_space;
    void* raw_memory = parent_allocator->allocate(full_size, alignof(std::max_align_t));
    if (!raw_memory) {
        throw std::bad_alloc();
    }
    std::memset(raw_memory, 0, allocator_metadata_size + sizeof(void*) * levels_count);
    _trusted_memory = raw_memory;
    auto* meta = new (_trusted_memory) allocator_meta{
        nullptr,
        allocate_fit_mode,
        parent_allocator,
        max_k,
        {}
    };
    for (unsigned char k = min_k; k <= max_k; k++) {
        get_free_list(_trusted_memory, k) = nullptr;
    }
    void* first_block = static_cast<char*>(_trusted_memory) + allocator_metadata_size + sizeof(void*) * levels_count;
    set_block_metadata(first_block, false, max_k);
    get_free_list(_trusted_memory, max_k) = first_block;

}

[[nodiscard]] void *allocator_buddies_system::do_allocate_sm(
    size_t size)
{
    auto* meta = get_meta(_trusted_memory);
    std::lock_guard<std::mutex> lock(meta->sync_mutex);
    size_t required_with_meta = size + occupied_block_metadata_size;
    unsigned char required_k = get_k_for_size(required_with_meta, meta->max_k);
    if (required_k > meta->max_k) {
        return nullptr;
    }
    unsigned char best_k = required_k;
    void* best_block = nullptr;
    void* best_prev = nullptr;
    if (meta->fit_mode == allocator_with_fit_mode::fit_mode::first_fit) {
        for (unsigned char k = required_k; k <= meta->max_k && !best_block; k++) {
            best_block = get_free_list(_trusted_memory, k);
            if (best_block) {
                best_k = k;
                break;
            }
        }
    } else if (meta->fit_mode == allocator_with_fit_mode::fit_mode::the_best_fit) {
        for (unsigned char k = required_k; k <= meta->max_k && !best_block; k++) {
            void* block = get_free_list(_trusted_memory, k);
            if (block) {
                best_block = block;
                best_k = k;
                break;
            }
        }
    } else if (meta->fit_mode == allocator_with_fit_mode::fit_mode::the_worst_fit) {
        for (int k = meta->max_k; k >= required_k; k--) {
            size_t levels_count = meta->max_k - min_k + 1;
            void* pool_start = static_cast<char*>(_trusted_memory) + allocator_metadata_size + sizeof(void*) * levels_count;
            void* pool_end = static_cast<char*>(pool_start) + (static_cast<size_t>(1) << meta->max_k);
            if (k < min_k) break;
            void* block = get_free_list(_trusted_memory, k);
            if (block) {
                if (block >= pool_start && block < pool_end) {
                    best_block = block;
                    best_k = (unsigned char)k;
                    break;
                }
            }
        }
    }
    if (!best_block) {
        return nullptr;
    }
    size_t levels_count = meta->max_k - min_k + 1;
    void* first_block_addr = static_cast<char*>(_trusted_memory) + allocator_metadata_size + sizeof(void*) * levels_count;
    void* prev = find_prev_in_free_list(_trusted_memory, best_block);
    remove_from_free_list(_trusted_memory, best_block, prev);
    void* current = best_block;
    unsigned char current_k = best_k;
    while (current_k > required_k) {
        --current_k;
        size_t half_size = block_size_from_k(current_k);
        void* buddy = get_buddy(current, half_size, first_block_addr);
        set_block_metadata(buddy, false, current_k);
        insert_into_free_list(_trusted_memory, buddy);
        set_block_metadata(current, false, current_k);
    }
    set_block_metadata(current, true, current_k);
    return get_user_ptr(current);
}

void allocator_buddies_system::do_deallocate_sm(void *at)
{
    if (!at) {
        return;
    }
    auto* meta = get_meta(_trusted_memory);
    std::lock_guard<std::mutex> lock(meta->sync_mutex);
    size_t levels_count = meta->max_k - min_k + 1;
    void* first_block_addr = static_cast<char*>(_trusted_memory) + allocator_metadata_size + sizeof(void*) * levels_count;
    size_t pool_size = static_cast<size_t>(1) << meta->max_k;
    void* storage_end = static_cast<char*>(first_block_addr) + pool_size;
    void* block = get_block_ptr(at);
    if (block < first_block_addr || block >= storage_end) {
        return;
    }
    unsigned char k = get_block_k(block);
    set_block_metadata(block, false, k);
    void* current = block;
    unsigned char current_k = k;
    while (current_k < meta->max_k) {
        size_t block_size = block_size_from_k(current_k);
        void* buddy = get_buddy(current, block_size, first_block_addr);
        void* pool_end = static_cast<char*>(first_block_addr) + (static_cast<size_t>(1) << meta->max_k);
        if (buddy < first_block_addr || buddy >= pool_end) {
            break;
        }
        unsigned char buddy_k = get_block_k(buddy);
        if (!is_block_occupied(buddy) && buddy_k == current_k) {
            void* prev = find_prev_in_free_list(_trusted_memory, buddy);
            bool buddy_is_in_list = (prev != nullptr) || (get_free_list(_trusted_memory, current_k) == buddy);
            if (buddy_is_in_list) {
                remove_from_free_list(_trusted_memory, buddy, prev);
                if (buddy < current) {
                    current = buddy;
                }
                current_k++;
                set_block_metadata(current, false, current_k);
            } else {
                break;
            }
        } else {
            break;
        }
    }
    insert_into_free_list(_trusted_memory, current);
}

allocator_buddies_system::allocator_buddies_system(const allocator_buddies_system &other)
{
    if (!other._trusted_memory) {
        _trusted_memory = nullptr;
        return;
    }
    auto* other_meta = get_meta(other._trusted_memory);
    std::lock_guard<std::mutex> lock(other_meta->sync_mutex);
    size_t levels_count = other_meta->max_k - min_k + 1;
    size_t total_size = allocator_metadata_size + levels_count * sizeof(void*) + block_size_from_k(other_meta->max_k);
    _trusted_memory = other_meta->parent_allocator->allocate(total_size, alignof(std::max_align_t));
    if (!_trusted_memory) {
        throw std::bad_alloc();
    }
    std::memcpy(_trusted_memory, other._trusted_memory, total_size);
    auto* meta = get_meta(_trusted_memory);
    new (&meta->sync_mutex) std::mutex();
}

allocator_buddies_system &allocator_buddies_system::operator=(const allocator_buddies_system &other)
{
    if (this != &other) {
        this->~allocator_buddies_system();
        new (this) allocator_buddies_system(other);
    }
    return *this;
}

bool allocator_buddies_system::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    return this == &other;
}

inline void allocator_buddies_system::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    auto* meta = get_meta(_trusted_memory);
    std::lock_guard<std::mutex> lock(meta->sync_mutex);
    meta->fit_mode = mode;
}


std::vector<allocator_test_utils::block_info> allocator_buddies_system::get_blocks_info() const noexcept
{
    return get_blocks_info_inner();
}

std::vector<allocator_test_utils::block_info> allocator_buddies_system::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> result;
    if (!_trusted_memory) {
        return result;
    }
    auto* meta = get_meta(_trusted_memory);
    size_t levels_count = meta->max_k - min_k + 1;
    void* curr = static_cast<char*>(_trusted_memory) + allocator_metadata_size + sizeof(void*) * levels_count;
    size_t pool_size = static_cast<size_t>(1) << meta->max_k;
    void* storage_end = static_cast<char*>(curr) + pool_size;
    while (curr < storage_end) {
        allocator_test_utils::block_info info;
        info.block_size = block_size_from_k(get_block_k(curr));
        info.is_block_occupied = is_block_occupied(curr);
        result.push_back(info);
        curr = static_cast<char*>(curr) + block_size_from_k(get_block_k(curr));
    }
    return result;
}

allocator_buddies_system::buddy_iterator allocator_buddies_system::begin() const noexcept
{
    if (!_trusted_memory) {
        return buddy_iterator();
    }
    allocator_meta* meta = get_meta(_trusted_memory);
    size_t levels_count = meta->max_k - min_k + 1;
    void* first_block = static_cast<char*>(_trusted_memory) + allocator_metadata_size + sizeof(void*) * levels_count;
    return buddy_iterator(first_block);
}

allocator_buddies_system::buddy_iterator allocator_buddies_system::end() const noexcept
{
    return buddy_iterator();
}

bool allocator_buddies_system::buddy_iterator::operator==(const allocator_buddies_system::buddy_iterator &other) const noexcept
{
    return _block == other._block;
}

bool allocator_buddies_system::buddy_iterator::operator!=(const allocator_buddies_system::buddy_iterator &other) const noexcept
{
    return !(*this == other);
}

allocator_buddies_system::buddy_iterator &allocator_buddies_system::buddy_iterator::operator++() & noexcept
{
    if (!_block) {
        return *this;
    }
    unsigned char k = get_block_k(_block);
    size_t size = block_size_from_k(k);
    _block = static_cast<char*>(_block) + size;
    return *this;
}

allocator_buddies_system::buddy_iterator allocator_buddies_system::buddy_iterator::operator++(int n)
{
    buddy_iterator tmp = *this;
    ++(*this);
    return tmp;
}

size_t allocator_buddies_system::buddy_iterator::size() const noexcept
{
    unsigned char k = get_block_k(_block);
    size_t size = block_size_from_k(k) - occupied_block_metadata_size;
    return size;
}

bool allocator_buddies_system::buddy_iterator::occupied() const noexcept
{
    if (!_block) {
        return false;
    }
    return is_block_occupied(_block);
}

void *allocator_buddies_system::buddy_iterator::operator*() const noexcept
{
    if (!_block) {
        return nullptr;
    }
    return get_user_ptr(_block);
}

allocator_buddies_system::buddy_iterator::buddy_iterator(void *start): _block(start)
{

}

allocator_buddies_system::buddy_iterator::buddy_iterator(): _block(nullptr)
{

}
