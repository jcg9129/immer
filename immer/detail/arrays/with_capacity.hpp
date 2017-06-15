//
// immer - immutable data structures for C++
// Copyright (C) 2016, 2017 Juan Pedro Bolivar Puente
//
// This file is part of immer.
//
// immer is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// immer is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with immer.  If not, see <http://www.gnu.org/licenses/>.
//

#pragma once

#include <immer/detail/arrays/no_capacity.hpp>

namespace immer {
namespace detail {
namespace arrays {

template <typename T, typename MemoryPolicy>
struct with_capacity
{
    using no_capacity_t = no_capacity<T, MemoryPolicy>;

    using node_t      = node<T, MemoryPolicy>;
    using edit_t      = typename MemoryPolicy::transience_t::edit;
    using size_t      = std::size_t;

    node_t* ptr;
    size_t  size;
    size_t  capacity;

    static const with_capacity empty;

    with_capacity(node_t* p, size_t s, size_t c)
        : ptr{p}, size{s}, capacity{c}
    {}

    with_capacity(const with_capacity& other)
        : with_capacity{other.ptr, other.size, other.capacity}
    {
        inc();
    }

    with_capacity(const no_capacity_t& other)
        : with_capacity{other.ptr, other.size, other.size}
    {
        inc();
    }

    with_capacity(with_capacity&& other)
        : with_capacity{empty}
    {
        swap(*this, other);
    }

    with_capacity& operator=(const with_capacity& other)
    {
        auto next = other;
        swap(*this, next);
        return *this;
    }

    with_capacity& operator=(with_capacity&& other)
    {
        swap(*this, other);
        return *this;
    }

    friend void swap(with_capacity& x, with_capacity& y)
    {
        using std::swap;
        swap(x.ptr, y.ptr);
        swap(x.size, y.size);
        swap(x.capacity, y.capacity);
    }

    ~with_capacity()
    {
        dec();
    }

    void inc()
    {
        using immer::detail::get;
        ptr->refs().inc();
    }

    void dec()
    {
        using immer::detail::get;
        if (ptr->refs().dec())
            node_t::delete_n(ptr, size, capacity);
    }

    const T* data() const { return ptr->data(); }
    T* data()             { return ptr->data(); }

    operator no_capacity_t() const
    {
        if (size == capacity) {
            ptr->refs().inc();
            return { ptr, size };
        } else {
            return { node_t::copy_n(size, ptr, size), size };
        }
    }

    template <typename Iter>
    static with_capacity from_range(Iter first, Iter last)
    {
        auto count = static_cast<size_t>(std::distance(first, last));
        return {
            node_t::copy_n(count, first, last),
            count,
            count
        };
    }

    template <typename U>
    static with_capacity from_initializer_list(std::initializer_list<U> values)
    {
        using namespace std;
        return from_range(begin(values), end(values));
    }

    template <typename Fn>
    void for_each_chunk(Fn&& fn) const
    {
        std::forward<Fn>(fn)(data(), data() + size);
    }

    const T& get(std::size_t index) const
    {
        return data()[index];
    }

    bool equals(const with_capacity& other) const
    {
        return ptr == other.ptr ||
            (size == other.size &&
             std::equal(data(), data() + size, other.data()));
    }

    static size_t recommend_up(size_t sz, size_t cap)
    {
        auto max = std::numeric_limits<size_t>::max();
        return
            sz <= cap       ? cap :
            cap >= max / 2  ? max
            /* otherwise */ : std::max(2 * cap, sz);
    }

    static size_t recommend_down(size_t sz, size_t cap)
    {
        return sz == 0      ? 1 :
            sz < cap / 2    ? sz * 2 :
            /* otherwise */   cap;
    }

    with_capacity push_back(T value) const
    {
        auto cap = recommend_up(size + 1, capacity);
        auto p = node_t::copy_n(cap, ptr, size);
        try {
            new (p->data() + size) T{std::move(value)};
            return { p, size + 1, cap };
        } catch (...) {
            node_t::delete_n(p, size, cap);
            throw;
        }
    }

    void push_back_mut(edit_t e, T value)
    {
        if (ptr->can_mutate(e) && capacity > size) {
            new (data() + size) T{std::move(value)};
            ++size;
        } else {
            auto cap = recommend_up(size + 1, capacity);
            auto p = node_t::copy_e(e, cap, ptr, size);
            try {
                new (p->data() + size) T{std::move(value)};
                *this = { p, size + 1, cap };
            } catch (...) {
                node_t::delete_n(p, size, cap);
                throw;
            }
        }
    }

    with_capacity assoc(std::size_t idx, T value) const
    {
        auto p = node_t::copy_n(capacity, ptr, size);
        try {
            p->data()[idx] = std::move(value);
            return { p, size, capacity };
        } catch (...) {
            node_t::delete_n(p, size, capacity);
            throw;
        }
    }

    void assoc_mut(edit_t e, std::size_t idx, T value)
    {
        if (ptr->can_mutate(e)) {
            data()[idx] = std::move(value);
        } else {
            auto p = node_t::copy_n(capacity, ptr, size);
            try {
                p->data()[idx] = std::move(value);
                *this = { p, size, capacity };
            } catch (...) {
                node_t::delete_n(p, size, capacity);
                throw;
            }
        }
    }

    template <typename Fn>
    with_capacity update(std::size_t idx, Fn&& op) const
    {
        auto p = node_t::copy_n(capacity, ptr, size);
        try {
            auto& elem = p->data()[idx];
            elem = std::forward<Fn>(op)(std::move(elem));
            return { p, size, capacity };
        } catch (...) {
            node_t::delete_n(p, size, capacity);
            throw;
        }
    }

    template <typename Fn>
    void update_mut(edit_t e, std::size_t idx, Fn&& op)
    {
        if (ptr->can_mutate(e)) {
            auto& elem = data()[idx];
            elem = std::forward<Fn>(op)(std::move(elem));
        } else {
            auto p = node_t::copy_e(e, capacity, ptr, size);
            try {
                auto& elem = p->data()[idx];
                elem = std::forward<Fn>(op)(std::move(elem));
                *this = { p, size, capacity };
            } catch (...) {
                node_t::delete_n(p, size, capacity);
                throw;
            }
        }
    }

    with_capacity take(std::size_t sz) const
    {
        auto cap = recommend_down(sz, capacity);
        auto p = node_t::copy_n(cap, ptr, sz);
        return { p, sz, cap };
    }

    void take_mut(edit_t e, std::size_t sz)
    {
        if (ptr->can_mutate(e)) {
            destroy_n(data() + size, size - sz);
            size = sz;
        } else {
            auto cap = recommend_down(sz, capacity);
            auto p = node_t::copy_e(e, cap, ptr, sz);
            *this = { p, sz, cap };
        }
    }
};

template <typename T, typename MP>
const with_capacity<T, MP> with_capacity<T, MP>::empty = {
    node_t::make_n(1),
    0,
    1,
};

} // namespace arrays
} // namespace detail
} // namespace immer