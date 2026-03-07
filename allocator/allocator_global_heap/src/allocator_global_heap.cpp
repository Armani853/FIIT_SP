#include <not_implemented.h>
#include "../include/allocator_global_heap.h"

allocator_global_heap::allocator_global_heap() = default;

allocator_global_heap::~allocator_global_heap() = default;

[[nodiscard]] void *allocator_global_heap::do_allocate_sm(
    size_t size)
{
    if (size == 0) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mtx);
    void* ptr = ::operator new(size, std::nothrow);
    if (ptr == nullptr) {
        return nullptr;
    }
    return ptr;
}

void allocator_global_heap::do_deallocate_sm(
    void *at)
{
    if (at == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mtx);
    ::operator delete(at);
}

bool allocator_global_heap::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    return this == &other; 
}