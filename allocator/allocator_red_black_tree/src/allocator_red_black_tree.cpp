#include <not_implemented.h>

#include "../include/allocator_red_black_tree.h"

allocator_red_black_tree::~allocator_red_black_tree()
{
    if (!_trusted_memory) {
        return;
    }
    auto* meta = get_meta(_trusted_memory);
    meta->sync_mutex.~mutex();
    meta->parent_allocator->deallocate(_trusted_memory, meta->total_size, alignof(std::max_align_t));
    _trusted_memory = nullptr;
}

allocator_red_black_tree::allocator_red_black_tree(
    allocator_red_black_tree &&other) noexcept :_trusted_memory(other._trusted_memory)
{
    other._trusted_memory = nullptr;
}

allocator_red_black_tree &allocator_red_black_tree::operator=(
    allocator_red_black_tree &&other) noexcept
{
    if (this != &other) {
        this->~allocator_red_black_tree();
        _trusted_memory = other._trusted_memory;
        other._trusted_memory = nullptr;
    }
    return *this;
}

allocator_red_black_tree::allocator_red_black_tree(
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
        nullptr,
        nullptr
    };
    void* first_block = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    meta->first_block = first_block;
    meta->root = first_block;

    block_data* data = get_block_data(first_block);
    data->occupied = false;
    data->color = block_color::BLACK;
    get_block_size(first_block) = space_size;
    get_block_parent(first_block) = nullptr;
    get_block_left(first_block) = nullptr;
    get_block_right(first_block) = nullptr;
    get_block_prev(first_block) = nullptr;
    get_block_next(first_block) = nullptr;
}

allocator_red_black_tree::allocator_red_black_tree(const allocator_red_black_tree &other)
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
    new (&meta->sync_mutex) std::mutex();
}

allocator_red_black_tree &allocator_red_black_tree::operator=(const allocator_red_black_tree &other)
{
    if (this != &other) {
        this->~allocator_red_black_tree();
        new (this) allocator_red_black_tree(other);
    }
    return *this;
}

bool allocator_red_black_tree::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    return this == &other;
}

[[nodiscard]] void *allocator_red_black_tree::do_allocate_sm(
    size_t size)
{
    auto* meta = get_meta(_trusted_memory);
    std::lock_guard<std::mutex> lock(meta->sync_mutex);
    void* block = nullptr;
    switch (meta->fit_mode) {
        case (allocator_with_fit_mode::fit_mode::first_fit):
            block = rb_find_first_fit(meta->root, size);
            break;
        case (allocator_with_fit_mode::fit_mode::the_best_fit):
            block = rb_find_best_fit(meta->root, size);
            break;
        case (allocator_with_fit_mode::fit_mode::the_worst_fit):
            block = rb_find_worst_fit(meta->root, size);
            break;
    }
    if (!block) {
        return nullptr;
    }

    rb_delete(meta->root, block);
    size_t block_size = get_block_size(block);
    size_t remaining = block_size - size;
    if (remaining >= free_block_metadata_size) {
        void* new_block = static_cast<char*>(block) + size + free_block_metadata_size;
        block_data* new_data = get_block_data(new_block);
        new_data->occupied = false;
        new_data->color = block_color::RED;
        get_block_size(new_block) = remaining - free_block_metadata_size;
        get_block_parent(new_block) = nullptr;
        get_block_left(new_block) = nullptr;
        get_block_right(new_block) = nullptr;
        get_block_next(new_block) = get_block_next(block);
        if (get_block_next(block)) {
            get_block_prev(get_block_next(block)) = new_block;
        }
        get_block_prev(new_block) = block;
        get_block_next(block) = new_block;
        rb_insert(meta->root, new_block, get_block_size(new_block));
        get_block_size(block) = size;
    }
    block_data* data = get_block_data(block);
    data->occupied = true;
    return static_cast<char*>(block) + free_block_metadata_size;
}


void allocator_red_black_tree::do_deallocate_sm(
    void *at)
{
    if (!at) {
        return;
    }
    auto* meta = get_meta(_trusted_memory);
    std::lock_guard<std::mutex> lock(meta->sync_mutex);
    void* block = static_cast<char*>(at) - free_block_metadata_size;
    if (block < meta->first_block || block >= static_cast<char*>(_trusted_memory) + meta->total_size) {
        return;
    }
    block_data* data = get_block_data(block);
    data->occupied = false;
    if (get_block_next(block) && !is_block_occupied(get_block_next(block))) {
        void* next = get_block_next(block);
        rb_delete(meta->root, next);
        get_block_size(block) += get_block_size(next) + free_block_metadata_size;
        get_block_next(block) = get_block_next(next);
        if (get_block_next(block)) {
            get_block_prev(get_block_next(block)) = block;
        }
    }

    if (get_block_prev(block) && !is_block_occupied(get_block_prev(block))) {
        void* prev = get_block_prev(block);
        rb_delete(meta->root, prev);
        get_block_size(prev) += get_block_size(block) + free_block_metadata_size;
        get_block_next(prev) = get_block_next(block);
        if (get_block_next(block)) {
            get_block_prev(get_block_next(block)) = prev;
        }
        block = prev;
    }
    rb_insert(meta->root, block, get_block_size(block));
}

void allocator_red_black_tree::set_fit_mode(allocator_with_fit_mode::fit_mode mode)
{
    auto* meta = get_meta(_trusted_memory);
    std::lock_guard<std::mutex> lock(meta->sync_mutex);
    meta->fit_mode = mode;
}


std::vector<allocator_test_utils::block_info> allocator_red_black_tree::get_blocks_info() const
{
    return get_blocks_info_inner();
}

std::vector<allocator_test_utils::block_info> allocator_red_black_tree::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> result;
    if (!_trusted_memory) {
        return result;
    }
    auto* meta = get_meta(_trusted_memory);
    void* curr = meta->first_block;
    while (curr) {
        allocator_test_utils::block_info info;
        info.block_size = get_block_size(curr);
        info.is_block_occupied = is_block_occupied(curr);
        result.push_back(info);
        curr = get_block_next(curr);
    }
    return result;
}


allocator_red_black_tree::rb_iterator allocator_red_black_tree::begin() const noexcept
{
    return rb_iterator(_trusted_memory);
}

allocator_red_black_tree::rb_iterator allocator_red_black_tree::end() const noexcept
{
    return rb_iterator();
}


bool allocator_red_black_tree::rb_iterator::operator==(const allocator_red_black_tree::rb_iterator &other) const noexcept
{
    return _block_ptr == other._block_ptr;
}

bool allocator_red_black_tree::rb_iterator::operator!=(const allocator_red_black_tree::rb_iterator &other) const noexcept
{
    return !(*this == other);
}

allocator_red_black_tree::rb_iterator &allocator_red_black_tree::rb_iterator::operator++() & noexcept
{
    if (_block_ptr) {
        _block_ptr = get_block_next(_block_ptr);
    }
    return *this;
}

allocator_red_black_tree::rb_iterator allocator_red_black_tree::rb_iterator::operator++(int n)
{
    rb_iterator tmp = *this;
    ++(*this);
    return tmp;

}

size_t allocator_red_black_tree::rb_iterator::size() const noexcept
{
    return _block_ptr ? get_block_size(_block_ptr) : 0;
}

void *allocator_red_black_tree::rb_iterator::operator*() const noexcept
{
    return _block_ptr ? static_cast<char*>(_block_ptr) + free_block_metadata_size : nullptr;
}

allocator_red_black_tree::rb_iterator::rb_iterator(): _block_ptr(nullptr), _trusted(nullptr)
{

}

allocator_red_black_tree::rb_iterator::rb_iterator(void *trusted): _block_ptr(trusted ? get_meta(trusted)->first_block : nullptr), _trusted(trusted)
{

}

bool allocator_red_black_tree::rb_iterator::occupied() const noexcept
{
    return _block_ptr ? is_block_occupied(_block_ptr) : false;
}
