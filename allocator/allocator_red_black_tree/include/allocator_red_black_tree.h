#ifndef MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_RED_BLACK_TREE_H
#define MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_RED_BLACK_TREE_H

#include <pp_allocator.h>
#include <allocator_test_utils.h>
#include <allocator_with_fit_mode.h>
#include <mutex>

class allocator_red_black_tree final:
    public smart_mem_resource,
    public allocator_test_utils,
    public allocator_with_fit_mode
{

private:

    enum class block_color : unsigned char
    { RED, BLACK };

    struct block_data
    {
        bool occupied : 4;
        block_color color : 4;
    };

    void *_trusted_memory;

    struct allocator_meta {
        std::pmr::memory_resource* parent_allocator;
        allocator_with_fit_mode::fit_mode fit_mode;
        size_t total_size;
        std::mutex sync_mutex;
        void* root;
        void* first_block;
    };

    static allocator_meta* get_meta(void* trusted) {
        return static_cast<allocator_meta*>(trusted);
    }

    static size_t& get_block_size(void* block) {
        return *reinterpret_cast<size_t*>(static_cast<char*>(block) + sizeof(block_data));
    }
    
    static void*& get_block_parent(void* block) {
        return *reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(block_data) + sizeof(size_t));
    }

    static void*& get_block_left(void* block) {
        return *reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(block_data) + sizeof(size_t) + sizeof(void*));
    }

    static void*& get_block_right(void* block) {
        return *reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(block_data) + sizeof(size_t) + 2 * sizeof(void*));
    }

    static void*& get_block_prev(void* block) {
        return *reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(block_data) + sizeof(size_t) + 3 * sizeof(void*));
    }

    static void*& get_block_next(void* block) {
        return *reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(block_data) + sizeof(size_t) + 4 * sizeof(void*));
    }

    static block_data* get_block_data(void* block) {
        return static_cast<block_data*>(block);
    }

    static bool is_block_occupied(void* block) {
        if (!block) {
            return true;
        }
        return static_cast<block_data*>(block)->occupied;
    }

    static block_color get_block_color(void* block) {
        if (!block) {
            return block_color::BLACK;
        }
        return static_cast<block_data*>(block)->color;
    }
    
    static void set_block_occupied(void* block, bool occupied) {
        static_cast<block_data*>(block)->occupied = occupied;
    }

    static void set_block_color(void* block, block_color color) {
        static_cast<block_data*>(block)->color = color;
    }

    static void* get_user_ptr(void* block) {
        return static_cast<char*>(block) + free_block_metadata_size;
    }

    static void* get_block_ptr(void* user_ptr) {
        return static_cast<char*>(user_ptr) - free_block_metadata_size;
    }

    void rotate_left(void*& root, void* x) {
        void* y = get_block_right(x);
        get_block_right(x) = get_block_left(y);
        if (get_block_left(y)) {
            get_block_parent(get_block_left(y)) = x;
        }
        get_block_parent(y) = get_block_parent(x);
        if (get_block_parent(x) == nullptr) {
            root = y;
        } else if (x == get_block_left(get_block_parent(x))) {
            get_block_left(get_block_parent(x)) = y;
        } else {
            get_block_right(get_block_parent(x)) = y;
        }
        get_block_left(y) = x;
        get_block_parent(x) = y;
    }

    void rotate_right(void*& root, void* x) {
        void* y = get_block_left(x);
        get_block_left(x) = get_block_right(y);
        if (get_block_right(y)) {
            get_block_parent(get_block_right(y)) = x;
        }
        get_block_parent(y) = get_block_parent(x);
        if (get_block_parent(x) == nullptr) {
            root = y;
        } else if (x == get_block_right(get_block_parent(x))) {
            get_block_right(get_block_parent(x)) = y;
        } else {
            get_block_left(get_block_parent(x)) = y;
        }
        get_block_right(y) = x;
        get_block_parent(x) = y;
    }

    void rb_insert_fixup(void*& root, void* z) {
        while (get_block_parent(z) && get_block_color(get_block_parent(z)) == block_color::RED) {
            if (get_block_parent(z) == get_block_right(get_block_parent(get_block_parent(z)))) {
                void* y = get_block_right(get_block_parent(get_block_parent(z)));
                if (y && get_block_color(y) == block_color::RED) {
                    set_block_color(get_block_parent(z), block_color::BLACK);
                    set_block_color(y, block_color::BLACK);
                    set_block_color(get_block_parent(get_block_parent(z)), block_color::RED);
                    z = get_block_parent(get_block_parent(z));
                } else {
                    if (z == get_block_right(get_block_parent(z))) {
                        z = get_block_parent(z);
                        rotate_left(root, z);
                    }
                    set_block_color(get_block_parent(z), block_color::BLACK);
                    set_block_color(get_block_parent(get_block_parent(z)), block_color::RED);
                    rotate_right(root, get_block_parent(get_block_parent(z)));
                }
            } else { 
                void* y = get_block_left(get_block_parent(get_block_parent(z)));
                if (y && get_block_color(y) == block_color::RED) {
                    set_block_color(get_block_parent(z), block_color::BLACK);
                    set_block_color(y, block_color::BLACK);
                    set_block_color(get_block_parent(get_block_parent(z)), block_color::RED);
                    z = get_block_parent(get_block_parent(z));
                } else {
                    if (z == get_block_left(get_block_parent(z))) {
                        z = get_block_parent(z);
                        rotate_left(root, z);
                    }
                    set_block_color(get_block_parent(z), block_color::BLACK);
                    set_block_color(get_block_parent(get_block_parent(z)), block_color::RED);
                    rotate_right(root, get_block_parent(get_block_parent(z)));
                }
            }  
        }
        set_block_color(root, block_color::BLACK);
    }

    void rb_insert(void*& root, void* z, size_t size) {
        get_block_size(z) = size;
        set_block_color(z, block_color::RED);
        get_block_left(z) = get_block_right(z) = get_block_parent(z) = nullptr;
        void* y = nullptr;
        void* x = root;
        while (x) {
            y = x;
            if (get_block_size(z) < get_block_size(x)) {
                x = get_block_left(x);
            } else {
                x = get_block_right(x);
            }
        }
        get_block_parent(z) = y;
        if (y == nullptr) {
            root = z;
        } else if (get_block_size(z) < get_block_size(y)) {
            get_block_left(y) = z;
        } else {
            get_block_right(y) = z;
        }
        rb_insert_fixup(root, z);
    }

    void* rb_find(void* root, size_t size, allocator_with_fit_mode::fit_mode mode) {
        if (!root) {
            return nullptr;
        } 
        if (mode == allocator_with_fit_mode::fit_mode::first_fit) {
            void* curr = root;
            void* result = nullptr;
            while (curr) {
                if (size <= get_block_size(curr)) {
                    result = curr;
                    curr = get_block_left(curr);
                } else {
                    curr = get_block_right(curr);
                }
            }
            return result;
        }
        else if (mode == allocator_with_fit_mode::fit_mode::the_best_fit) {
            void* curr = root;
            void* best = nullptr;
            size_t best_size = SIZE_MAX;
            while (curr) {
                size_t curr_size = get_block_size(curr);
                if (size <= curr_size && curr_size < best_size) {
                    best_size = curr_size;
                    best = curr;
                }
                if (size < curr_size) {
                    curr = get_block_left(curr);
                } else {
                    curr = get_block_right(curr);
                }
            }
            return best;
        } else if (mode == allocator_with_fit_mode::fit_mode::the_worst_fit) {
            void* curr = root;
            void* worst = nullptr;
            while (curr) {
                worst = curr;
                curr = get_block_right(curr);
            }
            if (worst && get_block_size(worst) >= size) {
                return worst;
            }
            return nullptr;
        }
        return nullptr;
    }

    static constexpr const size_t allocator_metadata_size = sizeof(allocator_dbg_helper*) + sizeof(fit_mode) + sizeof(size_t) + sizeof(std::mutex) + sizeof(void*);
    static constexpr const size_t occupied_block_metadata_size = sizeof(block_data) + 3 * sizeof(void*);
    static constexpr const size_t free_block_metadata_size = sizeof(block_data) + 5 * sizeof(void*);

public:
    
    ~allocator_red_black_tree() override;
    
    allocator_red_black_tree(
        allocator_red_black_tree const &other);
    
    allocator_red_black_tree &operator=(
        allocator_red_black_tree const &other);
    
    allocator_red_black_tree(
        allocator_red_black_tree &&other) noexcept;
    
    allocator_red_black_tree &operator=(
        allocator_red_black_tree &&other) noexcept;

public:
    
    explicit allocator_red_black_tree(
            size_t space_size,
            std::pmr::memory_resource *parent_allocator = nullptr,
            allocator_with_fit_mode::fit_mode allocate_fit_mode = allocator_with_fit_mode::fit_mode::first_fit);

private:
    
    [[nodiscard]] void *do_allocate_sm(
        size_t size) override;
    
    void do_deallocate_sm(
        void *at) override;

    bool do_is_equal(const std::pmr::memory_resource&) const noexcept override;

    std::vector<allocator_test_utils::block_info> get_blocks_info() const override;
    
    inline void set_fit_mode(allocator_with_fit_mode::fit_mode mode) override;

private:

    std::vector<allocator_test_utils::block_info> get_blocks_info_inner() const override;

    class rb_iterator
    {
        void* _block_ptr;
        void* _trusted;

    public:

        using iterator_category = std::forward_iterator_tag;
        using value_type = void*;
        using reference = void*&;
        using pointer = void**;
        using difference_type = ptrdiff_t;

        bool operator==(const rb_iterator&) const noexcept;

        bool operator!=(const rb_iterator&) const noexcept;

        rb_iterator& operator++() & noexcept;

        rb_iterator operator++(int n);

        size_t size() const noexcept;

        void* operator*() const noexcept;

        bool occupied()const noexcept;

        rb_iterator();

        rb_iterator(void* trusted);
    };

    friend class rb_iterator;

    rb_iterator begin() const noexcept;
    rb_iterator end() const noexcept;

};

#endif //MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_RED_BLACK_TREE_H