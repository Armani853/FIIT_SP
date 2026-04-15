#ifndef SYS_PROG_BS_tree_H
#define SYS_PROG_BS_tree_H

#include <iterator>
#include <utility>
#include <boost/container/static_vector.hpp>
#include <stack>
#include <pp_allocator.h>
#include <associative_container.h>
#include <not_implemented.h>
#include <initializer_list>

template <typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5>
class BS_tree final : private compare // EBCO
{
public:

    using tree_data_type = std::pair<tkey, tvalue>;
    using tree_data_type_const = std::pair<const tkey, tvalue>;
    using value_type = tree_data_type_const;

private:

    static constexpr const size_t minimum_keys_in_node = (2 * t - 1) * 2 / 3;
    static constexpr const size_t maximum_keys_in_node = 2 * t - 1;
    static constexpr const size_t redistribute_threshold = maximum_keys_in_node;

    // region comparators declaration

    inline bool compare_keys(const tkey& lhs, const tkey& rhs) const;
    inline bool compare_pairs(const tree_data_type& lhs, const tree_data_type& rhs) const;

    // endregion comparators declaration


    struct bstree_node
    {
        boost::container::static_vector<tree_data_type, maximum_keys_in_node + 1> _keys;
        boost::container::static_vector<bstree_node*, maximum_keys_in_node + 2> _pointers;
        bstree_node() noexcept : _keys(), _pointers() {}
    };

    pp_allocator<value_type> _allocator;
    bstree_node* _root;
    size_t _size;

    
    pp_allocator<value_type> get_allocator() const noexcept;

    void swap(BS_tree& other) {
        using std::swap;
        swap(static_cast<compare&>(*this), static_cast<compare&>(other));
        swap(_allocator, other._allocator);
        swap(_root, other._root);
        swap(_size, other._size);
    }


    bstree_node* create_node() {
        size_t items_needed = (sizeof(bstree_node) + sizeof(value_type) - 1) / sizeof(value_type);
        void* raw_memory = _allocator.allocate(items_needed);
        bstree_node* node = reinterpret_cast<bstree_node*>(raw_memory);
        new (node) bstree_node(); 
        return node;
    }

    void destroy_node(bstree_node* node) {
        if (node == nullptr) return;
        size_t items_needed = (sizeof(bstree_node) + sizeof(value_type) - 1) / sizeof(value_type);
        node->~bstree_node();
        _allocator.deallocate(reinterpret_cast<value_type*>(node), items_needed);
    }

    bstree_node* copy_node(bstree_node* src, bstree_node* parent=nullptr) {
        if (!src) {
            return nullptr;
        }
        bstree_node* dst = create_node();
        dst->_keys = src->_keys;
        for (auto* child : src->_pointers) {
            dst->_pointers.push_back(copy_node(child, dst));
        }
        return dst;
    }

    void clear_subtree(bstree_node* node) {
        if (!node) {
            return;
        }
        for (auto* child: node->_pointers) {
            clear_subtree(child);
        }
        destroy_node(node);
    }

    size_t find_key_position(bstree_node* node, const tkey& key) const {
        size_t pos = 0;
        while (pos < node->_keys.size() && compare_keys(node->_keys[pos].first, key)) {
            ++pos;
        }
        return pos;
    }


    bool redistribute_with_sibling(bstree_node* parent, size_t child_index) {
        bstree_node* child = parent->_pointers[child_index];
        
        if (child_index > 0) {
            bstree_node* left_sibling = parent->_pointers[child_index - 1];
            if (left_sibling->_keys.size() > minimum_keys_in_node) {
                auto key_to_move = left_sibling->_keys.back();
                left_sibling->_keys.pop_back();
                
                child->_keys.insert(child->_keys.begin(), parent->_keys[child_index - 1]);
                parent->_keys[child_index - 1] = key_to_move;
                
                if (!left_sibling->_pointers.empty()) {
                    auto ptr_to_move = left_sibling->_pointers.back();
                    left_sibling->_pointers.pop_back();
                    child->_pointers.insert(child->_pointers.begin(), ptr_to_move);
                }
                return true;
            }
        }
        
        if (child_index + 1 < parent->_pointers.size()) {
            bstree_node* right_sibling = parent->_pointers[child_index + 1];
            if (right_sibling->_keys.size() > minimum_keys_in_node) {
                auto key_to_move = right_sibling->_keys.front();
                right_sibling->_keys.erase(right_sibling->_keys.begin());
                
                child->_keys.push_back(parent->_keys[child_index]);
                parent->_keys[child_index] = key_to_move;
                
                if (!right_sibling->_pointers.empty()) {
                    auto ptr_to_move = right_sibling->_pointers.front();
                    right_sibling->_pointers.erase(right_sibling->_pointers.begin());
                    child->_pointers.push_back(ptr_to_move);
                }
                return true;
            }
        }
        
        return false;
    }

    void split_child(bstree_node* parent, size_t child_index)
    {
        bstree_node* child = parent->_pointers[child_index];
        if (redistribute_with_sibling(parent, child_index)) {
            return;
        }
        bstree_node* new_node = create_node();
        size_t mid = t - 1; 
        
        auto key_to_up = std::move(child->_keys[mid]);

        for (size_t i = mid + 1; i < child->_keys.size(); ++i)
        {
            new_node->_keys.push_back(std::move(child->_keys[i]));
        }
        
        if (!child->_pointers.empty())
        {
            for (size_t i = mid + 1; i < child->_pointers.size(); ++i)
            {
                new_node->_pointers.push_back(child->_pointers[i]);
            }
        }

        child->_keys.erase(child->_keys.begin() + mid, child->_keys.end());
        if (!child->_pointers.empty())
        {
            child->_pointers.erase(child->_pointers.begin() + mid + 1, child->_pointers.end());
        }
        
        parent->_keys.insert(parent->_keys.begin() + child_index, std::move(key_to_up));
        parent->_pointers.insert(parent->_pointers.begin() + child_index + 1, new_node);
    }

    void insert_non_full(bstree_node* node, const tree_data_type& data)
    {
        size_t pos = find_key_position(node, data.first);
        
        if (pos < node->_keys.size() && node->_keys[pos].first == data.first)
        {
            node->_keys[pos] = data;
            return;
        }
        
        if (node->_pointers.empty())
        {
            node->_keys.insert(node->_keys.begin() + pos, data);
            ++_size;
        }
        else
        {
            bstree_node* child = node->_pointers[pos];
            
            if (child->_keys.size() == maximum_keys_in_node)
            {
                split_child(node, pos);
                if (compare_keys(data.first, node->_keys[pos].first))
                {
                    child = node->_pointers[pos];
                }
                else
                {
                    child = node->_pointers[pos + 1];
                }
            }
            insert_non_full(child, data);
        }
    }

    bool remove_key(const tkey& key)
    {
        if (!_root) return false;
        
        bool removed = remove_from_node(_root, key);
        if (removed) --_size;
        
        if (_root && _root->_keys.empty() && !_root->_pointers.empty())
        {
            bstree_node* old_root = _root;
            _root = _root->_pointers[0];
            destroy_node(old_root);
        }
        
        return removed;
    }

    bool remove_from_node(bstree_node* node, const tkey& key)
    {
        size_t pos = find_key_position(node, key);
        bool found = (pos < node->_keys.size() && node->_keys[pos].first == key);
        
        if (found && node->_pointers.empty())
        {
            node->_keys.erase(node->_keys.begin() + pos);
            return true;
        }
        
        if (found && !node->_pointers.empty())
        {
            bstree_node* predecessor = node->_pointers[pos];
            while (!predecessor->_pointers.empty())
            {
                predecessor = predecessor->_pointers.back();
            }
            tree_data_type pred_key = predecessor->_keys.back();
            remove_from_node(node->_pointers[pos], pred_key.first);
            node->_keys[pos] = pred_key;
            return true;
        }
        
        if (!found && node->_pointers.empty())
        {
            return false;
        }
        
        bstree_node* child = node->_pointers[pos];
        
        if (child->_keys.size() == minimum_keys_in_node)
        {
            if (pos > 0 && node->_pointers[pos - 1]->_keys.size() > minimum_keys_in_node)
            {
                bstree_node* left_sibling = node->_pointers[pos - 1];
                child->_keys.insert(child->_keys.begin(), node->_keys[pos - 1]);
                node->_keys[pos - 1] = left_sibling->_keys.back();
                left_sibling->_keys.pop_back();
                
                if (!left_sibling->_pointers.empty())
                {
                    child->_pointers.insert(child->_pointers.begin(), left_sibling->_pointers.back());
                    left_sibling->_pointers.pop_back();
                }
            }
            else if (pos + 1 < node->_pointers.size() && node->_pointers[pos + 1]->_keys.size() > minimum_keys_in_node)
            {
                bstree_node* right_sibling = node->_pointers[pos + 1];
                child->_keys.push_back(node->_keys[pos]);
                node->_keys[pos] = right_sibling->_keys.front();
                right_sibling->_keys.erase(right_sibling->_keys.begin());
                
                if (!right_sibling->_pointers.empty())
                {
                    child->_pointers.push_back(right_sibling->_pointers.front());
                    right_sibling->_pointers.erase(right_sibling->_pointers.begin());
                }
            }
            else if (pos > 0)
            {
                bstree_node* left_sibling = node->_pointers[pos - 1];
                left_sibling->_keys.push_back(node->_keys[pos - 1]);
                left_sibling->_keys.insert(left_sibling->_keys.end(), child->_keys.begin(), child->_keys.end());
                left_sibling->_pointers.insert(left_sibling->_pointers.end(), child->_pointers.begin(), child->_pointers.end());
                
                node->_keys.erase(node->_keys.begin() + pos - 1);
                node->_pointers.erase(node->_pointers.begin() + pos);
                destroy_node(child);
                child = left_sibling;
            }
            else if (pos + 1 < node->_pointers.size())
            {
                bstree_node* right_sibling = node->_pointers[pos + 1];
                child->_keys.push_back(node->_keys[pos]);
                child->_keys.insert(child->_keys.end(), right_sibling->_keys.begin(), right_sibling->_keys.end());
                child->_pointers.insert(child->_pointers.end(), right_sibling->_pointers.begin(), right_sibling->_pointers.end());
                
                node->_keys.erase(node->_keys.begin() + pos);
                node->_pointers.erase(node->_pointers.begin() + pos + 1);
                destroy_node(right_sibling);
            }
        }
        
        return remove_from_node(child, key);
    }

public:

    // region constructors declaration

    explicit BS_tree(const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>()): compare(cmp), _allocator(alloc), _root(nullptr), _size(0) {}

    explicit BS_tree(pp_allocator<value_type> alloc, const compare& comp = compare()): compare(comp), _allocator(alloc), _root(nullptr), _size(0) {}

    template<input_iterator_for_pair<tkey, tvalue> iterator>
    explicit BS_tree(iterator begin, iterator end, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>()): compare(cmp), _allocator(alloc), _root(nullptr), _size(0) {
        for (auto it = begin; it != end; it++) {
            insert(*it);
        }
    }

    BS_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>()): compare(cmp), _allocator(alloc), _root(nullptr), _size(0) {
        for (const auto& item : data) {
            insert(item);
        }
    }

    // endregion constructors declaration

    // region five declaration

    BS_tree(const BS_tree& other): compare(other), _allocator(other._allocator), _root(nullptr), _size(0)  {
        if (other._root) {
            _root = copy_node(other._root, nullptr);
            _size = other._size;
        }
    }

    BS_tree(BS_tree&& other) noexcept: compare(std::move(other)), _allocator(std::move(other._allocator)), _root(other._root), _size(other._size)  {
        other._root = nullptr;
        other._size = 0;
    }

    BS_tree& operator=(const BS_tree& other) {
        if (this != &other) {
            BS_tree temp(other);
            swap(temp);
        }
        return *this;
    }

    BS_tree& operator=(BS_tree&& other) noexcept {
        if (this != &other) {
            clear();
            compare::operator=(std::move(other));
            _allocator = std::move(other._allocator);
            _root = other._root;
            _size = other._size;
            other._root = nullptr;
            other._size = 0;
        }
        return *this;
    }

    ~BS_tree() noexcept {
        clear();
    }

    // endregion five declaration

    // region iterators declaration

    class bstree_iterator;
    class bstree_reverse_iterator;
    class bstree_const_iterator;
    class bstree_const_reverse_iterator;

    class bstree_iterator final
    {
        std::stack<std::pair<bstree_node**, size_t>> _path;
        size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = value_type&;
        using pointer = value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = bstree_iterator;

        friend class BS_tree;
        friend class bstree_reverse_iterator;
        friend class bstree_const_iterator;
        friend class bstree_const_reverse_iterator;

        reference operator*() const noexcept;
        pointer operator->() const noexcept;

        self& operator++();
        self operator++(int);

        self& operator--();
        self operator--(int);

        bool operator==(const self& other) const noexcept;
        bool operator!=(const self& other) const noexcept;

        size_t depth() const noexcept;
        size_t current_node_keys_count() const noexcept;
        bool is_terminate_node() const noexcept;
        size_t index() const noexcept;

        explicit bstree_iterator(const std::stack<std::pair<bstree_node**, size_t>>& path = std::stack<std::pair<bstree_node**, size_t>>(), size_t index = 0);

    };

    class bstree_const_iterator final
    {
        std::stack<std::pair<bstree_node* const*, size_t>> _path;
        size_t _index;

    public:

        using value_type = tree_data_type_const;
        using reference = const value_type&;
        using pointer = const value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = bstree_const_iterator;

        friend class BS_tree;
        friend class bstree_reverse_iterator;
        friend class bstree_iterator;
        friend class bstree_const_reverse_iterator;

        bstree_const_iterator(const bstree_iterator& it) noexcept;

        reference operator*() const noexcept;
        pointer operator->() const noexcept;

        self& operator++();
        self operator++(int);

        self& operator--();
        self operator--(int);

        bool operator==(const self& other) const noexcept;
        bool operator!=(const self& other) const noexcept;

        size_t depth() const noexcept;
        size_t current_node_keys_count() const noexcept;
        bool is_terminate_node() const noexcept;
        size_t index() const noexcept;

        explicit bstree_const_iterator(const std::stack<std::pair<bstree_node* const*, size_t>>& path = std::stack<std::pair<bstree_node* const*, size_t>>(), size_t index = 0);
    };

    class bstree_reverse_iterator final
    {
        std::stack<std::pair<bstree_node**, size_t>> _path;
        size_t _index;

    public:

        using value_type = tree_data_type_const;
        using reference = value_type&;
        using pointer = value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = bstree_reverse_iterator;

        friend class BS_tree;
        friend class bstree_iterator;
        friend class bstree_const_iterator;
        friend class bstree_const_reverse_iterator;

        bstree_reverse_iterator(const bstree_iterator& it);
        operator bstree_iterator() const noexcept;

        reference operator*() const noexcept;
        pointer operator->() const noexcept;

        self& operator++();
        self operator++(int);

        self& operator--();
        self operator--(int);

        bool operator==(const self& other) const noexcept;
        bool operator!=(const self& other) const noexcept;

        size_t depth() const noexcept;
        size_t current_node_keys_count() const noexcept;
        bool is_terminate_node() const noexcept;
        size_t index() const noexcept;

        explicit bstree_reverse_iterator(const std::stack<std::pair<bstree_node**, size_t>>& path = std::stack<std::pair<bstree_node**, size_t>>(), size_t index = 0);
    };

    class bstree_const_reverse_iterator final
    {
        std::stack<std::pair<bstree_node* const*, size_t>> _path;
        size_t _index;

    public:

        using value_type = tree_data_type_const;
        using reference = const value_type&;
        using pointer = const value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = bstree_const_reverse_iterator;

        friend class BS_tree;
        friend class bstree_reverse_iterator;
        friend class bstree_const_iterator;
        friend class bstree_iterator;

        bstree_const_reverse_iterator(const bstree_reverse_iterator& it) noexcept;
        operator bstree_const_iterator() const noexcept;

        reference operator*() const noexcept;
        pointer operator->() const noexcept;

        self& operator++();
        self operator++(int);

        self& operator--();
        self operator--(int);

        bool operator==(const self& other) const noexcept;
        bool operator!=(const self& other) const noexcept;

        size_t depth() const noexcept;
        size_t current_node_keys_count() const noexcept;
        bool is_terminate_node() const noexcept;
        size_t index() const noexcept;

        explicit bstree_const_reverse_iterator(const std::stack<std::pair<bstree_node* const*, size_t>>& path = std::stack<std::pair<bstree_node* const*, size_t>>(), size_t index = 0);
    };

    friend class bstree_iterator;
    friend class bstree_const_iterator;
    friend class bstree_reverse_iterator;
    friend class bstree_const_reverse_iterator;

    // endregion iterators declaration

    // region element access declaration

    /*
     * Returns a reference to the mapped value of the element with specified key. If no such element exists, an exception of type std::out_of_range is thrown.
     */
    tvalue& at(const tkey&);
    const tvalue& at(const tkey&) const;

    /*
     * If key not exists, makes default initialization of value
     */
    tvalue& operator[](const tkey& key);
    tvalue& operator[](tkey&& key);

    // endregion element access declaration
    // region iterator begins declaration

    bstree_iterator begin();
    bstree_iterator end();

    bstree_const_iterator begin() const;
    bstree_const_iterator end() const;

    bstree_const_iterator cbegin() const;
    bstree_const_iterator cend() const;

    bstree_reverse_iterator rbegin();
    bstree_reverse_iterator rend();

    bstree_const_reverse_iterator rbegin() const;
    bstree_const_reverse_iterator rend() const;

    bstree_const_reverse_iterator crbegin() const;
    bstree_const_reverse_iterator crend() const;

    // endregion iterator begins declaration

    // region lookup declaration

    size_t size() const noexcept;
    bool empty() const noexcept;

    /*
     * Returns end() if not exist
     */

    bstree_iterator find(const tkey& key);
    bstree_const_iterator find(const tkey& key) const;

    bstree_iterator lower_bound(const tkey& key);
    bstree_const_iterator lower_bound(const tkey& key) const;

    bstree_iterator upper_bound(const tkey& key);
    bstree_const_iterator upper_bound(const tkey& key) const;

    bool contains(const tkey& key) const;

    // endregion lookup declaration

    // region modifiers declaration

    void clear() noexcept;

    /*
     * Does nothing if key exists, delegates to emplace.
     * Second return value is true, when inserted
     */
    std::pair<bstree_iterator, bool> insert(const tree_data_type& data);
    std::pair<bstree_iterator, bool> insert(tree_data_type&& data);

    template <typename ...Args>
    std::pair<bstree_iterator, bool> emplace(Args&&... args);

    /*
     * Updates value if key exists, delegates to emplace.
     */
    bstree_iterator insert_or_assign(const tree_data_type& data);
    bstree_iterator insert_or_assign(tree_data_type&& data);

    template <typename ...Args>
    bstree_iterator emplace_or_assign(Args&&... args);

    /*
     * Return iterator to node next ro removed or end() if key not exists
     */
    bstree_iterator erase(bstree_iterator pos);
    bstree_iterator erase(bstree_const_iterator pos);

    bstree_iterator erase(bstree_iterator beg, bstree_iterator en);
    bstree_iterator erase(bstree_const_iterator beg, bstree_const_iterator en);


    bstree_iterator erase(const tkey& key);

    // endregion modifiers declaration
};

template<std::input_iterator iterator, comparator<typename std::iterator_traits<iterator>::value_type::first_type> compare = std::less<typename std::iterator_traits<iterator>::value_type::first_type>,
        std::size_t t = 5, typename U>
BS_tree(iterator begin, iterator end, const compare &cmp = compare(), pp_allocator<U> = pp_allocator<U>()) -> BS_tree<typename std::iterator_traits<iterator>::value_type::first_type, typename std::iterator_traits<iterator>::value_type::second_type, compare, t>;

template<typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5, typename U>
BS_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare &cmp = compare(), pp_allocator<U> = pp_allocator<U>()) -> BS_tree<tkey, tvalue, compare, t>;

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::compare_pairs(const BS_tree::tree_data_type &lhs,
                                                     const BS_tree::tree_data_type &rhs) const
{
    return compare_keys(lhs.first, rhs.first);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::compare_keys(const tkey &lhs, const tkey &rhs) const
{
    return compare::operator()(lhs, rhs);
}


template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
pp_allocator<typename BS_tree<tkey, tvalue, compare, t>::value_type> BS_tree<tkey, tvalue, compare, t>::get_allocator() const noexcept
{
    return _allocator;
}


template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
BS_tree<tkey, tvalue, compare, t>::bstree_iterator::bstree_iterator(
        const std::stack<std::pair<bstree_node**, size_t>>& path, size_t index): _path(path), _index(index)
{

}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator::reference
BS_tree<tkey, tvalue, compare, t>::bstree_iterator::operator*() const noexcept
{
    auto& top = _path.top();
    bstree_node* node = *(top.first);
    return *reinterpret_cast<value_type*>(&node->_keys[top.second]);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator::pointer
BS_tree<tkey, tvalue, compare, t>::bstree_iterator::operator->() const noexcept
{
    return &**this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator&
BS_tree<tkey, tvalue, compare, t>::bstree_iterator::operator++()
{

    if (_path.empty()) return *this;

    bstree_node* current = *(_path.top().first);
    size_t& idx = _path.top().second;

    if (!current->_pointers.empty())
    {
        bstree_node** next_ptr = &current->_pointers[idx + 1];
        bstree_node* next_node = *next_ptr;
        _path.push({next_ptr, 0});
        
        while (!next_node->_pointers.empty())
        {
            next_ptr = &next_node->_pointers[0];
            next_node = *next_ptr;
            _path.push({next_ptr, 0});
        }
        _index = 0;
        return *this;
    }

    if (idx + 1 < current->_keys.size())
    {
        ++idx;
        _index = idx;
        return *this;
    }

    while (!_path.empty())
    {
        bstree_node** finished_ptr = _path.top().first;
        _path.pop();
        
        if (_path.empty()) break;

        bstree_node* parent = *(_path.top().first);
        size_t child_idx = 0;
        while (child_idx < parent->_pointers.size() && &parent->_pointers[child_idx] != finished_ptr)
        {
            ++child_idx;
        }

        if (child_idx < parent->_keys.size())
        {
            _path.top().second = child_idx;
            _index = child_idx;
            return *this;
        }
    }
    _index = 0;
    return *this;
    
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator
BS_tree<tkey, tvalue, compare, t>::bstree_iterator::operator++(int)
{
    bstree_iterator tmp = *this;
    ++(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator&
BS_tree<tkey, tvalue, compare, t>::bstree_iterator::operator--()
{
    auto& top = _path.top();
    bstree_node* current = (*top.first);
    
    if (_index > 0)
    {
        --_index;
        
        if (!current->_pointers.empty())
        {
            _path.push({&current->_pointers[_index + 1], 0});
            current = current->_pointers[_index + 1];
            
            while (!current->_pointers.empty())
            {
                _path.push({&current->_pointers.back(), current->_pointers.size() - 1});
                current = current->_pointers.back();
            }
            _index = current->_keys.size() - 1;
        }
    }
    else
    {
        _path.pop();
        
        if (!_path.empty())
        {
            _index = _path.top().second;
        }
    }
    
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator
BS_tree<tkey, tvalue, compare, t>::bstree_iterator::operator--(int)
{
    bstree_iterator tmp = *this;
    --(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::bstree_iterator::operator==(const self& other) const noexcept
{
    if (_path.size() != other._path.size()) return false;
    if (_path.empty() && other._path.empty()) return true;
    if (_path.empty() || other._path.empty()) return false;
    bool nodes_equal = (*_path.top().first == *other._path.top().first);

    return nodes_equal && (_index == other._index);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::bstree_iterator::operator!=(const self& other) const noexcept
{
    return !(*this == other);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t BS_tree<tkey, tvalue, compare, t>::bstree_iterator::depth() const noexcept
{
    return _path.empty() ? 0 : _path.size() - 1;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t BS_tree<tkey, tvalue, compare, t>::bstree_iterator::current_node_keys_count() const noexcept
{
    if (_path.empty()) return 0;
    return (*_path.top().first)->_keys.size();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::bstree_iterator::is_terminate_node() const noexcept
{
    if (_path.empty()) return true;
    return (*_path.top().first)->_pointers.empty();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t BS_tree<tkey, tvalue, compare, t>::bstree_iterator::index() const noexcept
{
    return _index;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::bstree_const_iterator(
        const std::stack<std::pair<bstree_node* const*, size_t>>& path, size_t index): _path(path), _index(index)
{
    
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::bstree_const_iterator(
        const bstree_iterator& it) noexcept
{
    _index = it._index; 

    std::stack<std::pair<bstree_node**, size_t>> temp = it._path;
    std::stack<std::pair<bstree_node* const*, size_t>> inverted_stack;
    
    while (!temp.empty())
    {
        auto& p = temp.top();
        inverted_stack.push(std::pair<bstree_node* const*, size_t>(
            reinterpret_cast<bstree_node* const*>(p.first), 
            p.second
        ));
        
        temp.pop();
    }
    
    while (!inverted_stack.empty())
    {
        _path.push(inverted_stack.top());
        inverted_stack.pop();
    }
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::reference
BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::operator*() const noexcept
{
    auto& top = _path.top();

    bstree_node* node = const_cast<bstree_node*>(*(top.first));
    
    return *reinterpret_cast<const value_type*>(&node->_keys[top.second]);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::pointer
BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::operator->() const noexcept
{
    return &**this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator&
BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::operator++()
{
    if (_path.empty()) return *this;

    bstree_node* current = const_cast<bstree_node*>(*_path.top().first);
    size_t& idx = _path.top().second;

    if (!current->_pointers.empty())
    {
        bstree_node** next_ptr = &current->_pointers[idx + 1];
        bstree_node* next_node = *next_ptr;
        _path.push({next_ptr, 0});
        
        while (!next_node->_pointers.empty())
        {
            next_ptr = &next_node->_pointers[0];
            next_node = *next_ptr;
            _path.push({next_ptr, 0});
        }
        _index = 0;
        return *this;
    }

    if (idx + 1 < current->_keys.size())
    {
        ++idx;
        _index = idx;
        return *this;
    }

    while (!_path.empty())
    {
        bstree_node** finished_ptr = const_cast<bstree_node**>(_path.top().first);
        _path.pop();
        
        if (_path.empty()) break;

        bstree_node* parent = const_cast<bstree_node*>(*_path.top().first);
        size_t child_idx = 0;
        
        while (child_idx < parent->_pointers.size() && &parent->_pointers[child_idx] != finished_ptr)
        {
            ++child_idx;
        }

        if (child_idx < parent->_keys.size())
        {
            _path.top().second = child_idx;
            _index = child_idx;
            return *this;
        }
    }

    _index = 0;
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator
BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::operator++(int)
{
    bstree_const_iterator tmp = *this;
    ++(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator&
BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::operator--()
{
    auto& top = _path.top();
    bstree_node* current = const_cast<bstree_node*>(*top.first);
    
    if (_index > 0)
    {
        --_index;
        
        if (!current->_pointers.empty())
        {
            _path.push({&current->_pointers[_index + 1], 0});
            current = current->_pointers[_index + 1];
            
            while (!current->_pointers.empty())
            {
                _path.push({&current->_pointers.back(), current->_pointers.size() - 1});
                current = current->_pointers.back();
            }
            _index = current->_keys.size() - 1;
        }
    }
    else
    {
        _path.pop();
        
        if (!_path.empty())
        {
            _index = _path.top().second;
        }
    }
    
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator
BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::operator--(int)
{
    bstree_const_iterator tmp = *this;
    --(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::operator==(const self& other) const noexcept
{
    if (_path.size() != other._path.size()) return false;
    if (_path.empty() && other._path.empty()) return true;
    if (_path.empty() || other._path.empty()) return false;
    
    bool nodes_equal = (*_path.top().first == *other._path.top().first);

    return nodes_equal && (_index == other._index);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::operator!=(const self& other) const noexcept
{
    return !(*this == other);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::depth() const noexcept
{
    return _path.empty() ? 0 : _path.size() - 1;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::current_node_keys_count() const noexcept
{
    if (_path.empty()) return 0;
    return (*_path.top().first)->_keys.size();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::is_terminate_node() const noexcept
{
    if (_path.empty()) return true;
    return (*_path.top().first)->_pointers.empty();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator::index() const noexcept
{
    return _index;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::bstree_reverse_iterator(
        const std::stack<std::pair<bstree_node**, size_t>>& path, size_t index): _path(path), _index(index)
{
    
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::bstree_reverse_iterator(
        const bstree_iterator& it): _path(it._path), _index(it._index)
{
    
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::operator BS_tree<tkey, tvalue, compare, t>::bstree_iterator() const noexcept
{
    return bstree_iterator(_path, _index);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::reference
BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::operator*() const noexcept
{
    auto& top = _path.top();
    bstree_node* node = *(top.first);
    return reinterpret_cast<std::pair<const tkey, tvalue>&>(node->_keys[top.second]);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::pointer
BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::operator->() const noexcept
{
    return &**this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator&
BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::operator++()
{
    if (_index > 0)
    {
        --_index;
    }
    else
    {
        _path.pop();
        while (!_path.empty() && _index == 0)
        {
            _index = _path.top().second;
            _path.pop();
        }
        if (!_path.empty())
        {
            bstree_node* current = (*_path.top().first);
            while (!current->_pointers.empty())
            {
                _path.push({&current->_pointers.back(), current->_pointers.size() - 1});
                current = current->_pointers.back();
            }
            _index = current->_keys.size() - 1;
        }
    }
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator
BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::operator++(int)
{
    bstree_reverse_iterator tmp = *this;
    ++(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator&
BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::operator--()
{
    if (_index + 1 < (*_path.top().first)->_keys.size())
    {
        ++_index;
    }
    else
    {
        auto& top = _path.top();
        bstree_node* current = (*top.first);
        if (_index + 1 < current->_pointers.size())
        {
            _path.push({&current->_pointers[_index + 1], 0});
            current = current->_pointers[_index + 1];
            while (!current->_pointers.empty())
            {
                _path.push({&current->_pointers[0], 0});
                current = current->_pointers[0];
            }
            _index = 0;
        }
        else
        {
            _path.pop();
            if (!_path.empty())
            {
                _index = _path.top().second + 1;
            }
        }
    }
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator
BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::operator--(int)
{
    bstree_reverse_iterator tmp = *this;
    --(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::operator==(const self& other) const noexcept
{
    if (_path.size() != other._path.size()) return false;
    if (_path.empty() && other._path.empty()) return true;
    if (_path.empty() || other._path.empty()) return false;
    bool nodes_equal = (*_path.top().first == *other._path.top().first);

    return nodes_equal && (_index == other._index);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::operator!=(const self& other) const noexcept
{
    return !(*this == other);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::depth() const noexcept
{
    return _path.empty() ? 0 : _path.size() - 1;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::current_node_keys_count() const noexcept
{
    if (_path.empty()) return 0;
    return (*_path.top().first)->_keys.size();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::is_terminate_node() const noexcept
{
    if (_path.empty()) return true;
    return (*_path.top().first)->_pointers.empty();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator::index() const noexcept
{
    return _index;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::bstree_const_reverse_iterator(
        const std::stack<std::pair<bstree_node* const*, size_t>>& path, size_t index): _path(path), _index(index)
{
    
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::bstree_const_reverse_iterator(
        const bstree_reverse_iterator& it) noexcept
{
    std::stack<std::pair<bstree_node**, size_t>> temp = it._path;
    std::stack<std::pair<bstree_node* const*, size_t>> new_path;
    while (!temp.empty())
    {
        auto& p = temp.top();
        new_path.push({const_cast<const bstree_node**>(p.first), p.second});
        temp.pop();
    }
    while (!new_path.empty())
    {
        _path.push(new_path.top());
        new_path.pop();
    }
    _index = it._index;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::operator BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator() const noexcept
{
    return bstree_const_iterator(_path, _index);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::reference
BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::operator*() const noexcept
{
    auto& top = _path.top();
    bstree_node* node = *(top.first);
    return reinterpret_cast<const std::pair<const tkey, tvalue>&>(node->_keys[top.second]);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::pointer
BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::operator->() const noexcept
{
    return &**this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator&
BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::operator++()
{
    if (_index > 0)
    {
        --_index;
    }
    else
    {
        _path.pop();
        while (!_path.empty() && _index == 0)
        {
            _index = _path.top().second;
            _path.pop();
        }
        if (!_path.empty())
        {
            bstree_node* current = const_cast<bstree_node*>(*_path.top().first);
            while (!current->_pointers.empty())
            {
                _path.push({&current->_pointers.back(), current->_pointers.size() - 1});
                current = current->_pointers.back();
            }
            _index = current->_keys.size() - 1;
        }
    }
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator
BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::operator++(int)
{
    bstree_const_reverse_iterator tmp = *this;
    ++(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator&
BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::operator--()
{
    if (_index + 1 < (*_path.top().first)->_keys.size())
    {
        ++_index;
    }
    else
    {
        auto& top = _path.top();
        bstree_node* current = const_cast<bstree_node*>(*top.first);
        if (_index + 1 < current->_pointers.size())
        {
            _path.push({&current->_pointers[_index + 1], 0});
            current = current->_pointers[_index + 1];
            while (!current->_pointers.empty())
            {
                _path.push({&current->_pointers[0], 0});
                current = current->_pointers[0];
            }
            _index = 0;
        }
        else
        {
            _path.pop();
            if (!_path.empty())
            {
                _index = _path.top().second + 1;
            }
        }
    }
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator
BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::operator--(int)
{
    bstree_const_reverse_iterator tmp = *this;
    --(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::operator==(const self& other) const noexcept
{
    if (_path.size() != other._path.size()) return false;
    if (_path.empty() && other._path.empty()) return true;
    if (_path.empty() || other._path.empty()) return false;
    bool nodes_equal = (*_path.top().first == *other._path.top().first);
    

    return nodes_equal && (_index == other._index);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::operator!=(const self& other) const noexcept
{
    return !(*this == other);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::depth() const noexcept
{
    return _path.empty() ? 0 : _path.size() - 1;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::current_node_keys_count() const noexcept
{
    if (_path.empty()) return 0;
    return (*_path.top().first)->_keys.size();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::is_terminate_node() const noexcept
{
    if (_path.empty()) return true;
    return (*_path.top().first)->_pointers.empty();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator::index() const noexcept
{
    return _index;
}

// endregion iterators implementation

// region element access implementation

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
tvalue& BS_tree<tkey, tvalue, compare, t>::at(const tkey& key)
{
    auto it = find(key);
    if (it == end())
    {
        throw std::out_of_range("Key not found");
    }
    return const_cast<tvalue&>(it->second);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
const tvalue& BS_tree<tkey, tvalue, compare, t>::at(const tkey& key) const
{
    auto it = find(key);
    if (it == end())
    {
        throw std::out_of_range("Key not found");
    }
    return it->second;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
tvalue& BS_tree<tkey, tvalue, compare, t>::operator[](const tkey& key)
{
    auto it = find(key);
    if (it == end())
    {
        auto [new_it, inserted] = insert({key, tvalue{}});
        return const_cast<tvalue&>(new_it->second);
    }
    return const_cast<tvalue&>(it->second);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
tvalue& BS_tree<tkey, tvalue, compare, t>::operator[](tkey&& key)
{
    return (*this)[key];
}

// endregion element access implementation

// region iterator begins implementation

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator BS_tree<tkey, tvalue, compare, t>::begin()
{
    if (!_root) return end();
    
    std::stack<std::pair<bstree_node**, size_t>> path;
    
    path.push({&_root, 0});
    bstree_node* current = _root;
    
    while (!current->_pointers.empty())
    {

        path.push({&current->_pointers[0], 0});
        current = current->_pointers[0];
    }
    
   
    return bstree_iterator(path, 0);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator BS_tree<tkey, tvalue, compare, t>::end()
{
    return bstree_iterator();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator BS_tree<tkey, tvalue, compare, t>::begin() const
{
    return const_cast<BS_tree*>(this)->begin();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator BS_tree<tkey, tvalue, compare, t>::end() const
{
    return const_cast<BS_tree*>(this)->end();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator BS_tree<tkey, tvalue, compare, t>::cbegin() const
{
    return begin();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator BS_tree<tkey, tvalue, compare, t>::cend() const
{
    return end();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator BS_tree<tkey, tvalue, compare, t>::rbegin()
{
    return bstree_reverse_iterator(end());
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_reverse_iterator BS_tree<tkey, tvalue, compare, t>::rend()
{
    return bstree_reverse_iterator(begin());
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator BS_tree<tkey, tvalue, compare, t>::rbegin() const
{
    return bstree_const_reverse_iterator(end());
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator BS_tree<tkey, tvalue, compare, t>::rend() const
{
    return bstree_const_reverse_iterator(begin());
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator BS_tree<tkey, tvalue, compare, t>::crbegin() const
{
    return rbegin();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_reverse_iterator BS_tree<tkey, tvalue, compare, t>::crend() const
{
    return rend();
}

// endregion iterator begins implementation

// region lookup implementation

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t BS_tree<tkey, tvalue, compare, t>::size() const noexcept
{
    return _size;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::empty() const noexcept
{
    return _size == 0;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator BS_tree<tkey, tvalue, compare, t>::find(const tkey& key)
{
    if (!_root) return end();
    
    std::stack<std::pair<bstree_node**, size_t>> path;
    path.push({&_root, 0}); 
    
    bstree_node* current = _root;
    
    while (current)
    {
        size_t pos = find_key_position(current, key);
        
        path.top().second = pos;
        
        if (pos < current->_keys.size() && current->_keys[pos].first == key)
        {
            return bstree_iterator(path, pos);
        }
        
        if (current->_pointers.empty())
            break;
        
        bstree_node** next_node_ptr = &current->_pointers[pos];
        current = *next_node_ptr;
        
        path.push({next_node_ptr, 0});
    }
    
    return end();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator BS_tree<tkey, tvalue, compare, t>::find(const tkey& key) const
{
    return const_cast<BS_tree*>(this)->find(key);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator BS_tree<tkey, tvalue, compare, t>::lower_bound(const tkey& key)
{
    if (!_root) return end();

    auto it = begin();
    auto e = end();
    
    while (it != e && compare_keys(it->first, key))
    {
        ++it;
    }
    return it;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator BS_tree<tkey, tvalue, compare, t>::lower_bound(const tkey& key) const
{
    return const_cast<BS_tree*>(this)->lower_bound(key);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator BS_tree<tkey, tvalue, compare, t>::upper_bound(const tkey& key)
{
    auto it = lower_bound(key);
    while (it != end() && !compare_keys(key, it->first))
    {
        ++it;
    }
    return it;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_const_iterator BS_tree<tkey, tvalue, compare, t>::upper_bound(const tkey& key) const
{
    return const_cast<BS_tree*>(this)->upper_bound(key);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool BS_tree<tkey, tvalue, compare, t>::contains(const tkey& key) const
{
    return find(key) != end();
}

// endregion lookup implementation

// region modifiers implementation

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
void BS_tree<tkey, tvalue, compare, t>::clear() noexcept
{
    clear_subtree(_root);
    _root = nullptr;
    _size = 0;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
std::pair<typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator, bool>
BS_tree<tkey, tvalue, compare, t>::insert(const tree_data_type& data)
{
    if (!_root)
    {
        _root = create_node();
    }
    
    auto it_check = find(data.first);
    if (it_check != end())
    {
        return {it_check, false};
    }

    if (_root->_keys.size() == maximum_keys_in_node)
    {
        bstree_node* new_root = create_node();
        new_root->_pointers.push_back(_root);
        split_child(new_root, 0);
        _root = new_root;
    }
    
    insert_non_full(_root, data);
    
    return {find(data.first), true};
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
std::pair<typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator, bool>
BS_tree<tkey, tvalue, compare, t>::insert(tree_data_type&& data)
{
    return insert(data);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
template<typename... Args>
std::pair<typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator, bool>
BS_tree<tkey, tvalue, compare, t>::emplace(Args&&... args)
{
    return insert(tree_data_type(std::forward<Args>(args)...));
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator
BS_tree<tkey, tvalue, compare, t>::insert_or_assign(const tree_data_type& data)
{
    auto it = find(data.first);
    if (it != end())
    {
        const_cast<tvalue&>(it->second) = data.second;
        return it;
    }
    return insert(data).first;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator
BS_tree<tkey, tvalue, compare, t>::insert_or_assign(tree_data_type&& data)
{
    return insert_or_assign(data);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
template<typename... Args>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator
BS_tree<tkey, tvalue, compare, t>::emplace_or_assign(Args&&... args)
{
    return insert_or_assign(tree_data_type(std::forward<Args>(args)...));
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator
BS_tree<tkey, tvalue, compare, t>::erase(bstree_iterator pos)
{
    if (pos == end()) return end();
    auto next = pos;
    ++next;
    remove_key(pos->first);
    return next;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator
BS_tree<tkey, tvalue, compare, t>::erase(bstree_const_iterator pos)
{
    return erase(bstree_iterator(pos));
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator
BS_tree<tkey, tvalue, compare, t>::erase(bstree_iterator beg, bstree_iterator en)
{
    while (beg != en)
    {
        beg = erase(beg);
    }
    return beg;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator
BS_tree<tkey, tvalue, compare, t>::erase(bstree_const_iterator beg, bstree_const_iterator en)
{
    return erase(bstree_iterator(beg), bstree_iterator(en));
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename BS_tree<tkey, tvalue, compare, t>::bstree_iterator
BS_tree<tkey, tvalue, compare, t>::erase(const tkey& key)
{
    auto it = find(key);
    if (it == end())
    {
        return end();
    }
    
    auto next = it;
    ++next;
    remove_key(key);
    return next;
}

// endregion modifiers implementation

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool compare_pairs(const typename BS_tree<tkey, tvalue, compare, t>::tree_data_type &lhs,
                   const typename BS_tree<tkey, tvalue, compare, t>::tree_data_type &rhs)
{
    return compare_keys(lhs.first, rhs.first);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool compare_keys(const tkey &lhs, const tkey &rhs)
{
    return compare::operator()(lhs, rhs);
}


#endif