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

#include <immer/config.hpp>
#include <immer/detail/hamts/cnode.hpp>

namespace immer {
namespace detail {
namespace hamts {

template <typename T,
          typename Hash,
          typename Equal,
          typename MemoryPolicy,
          bits_t B>
struct champ
{
    static_assert(branches<B> <= sizeof(bitmap_t) * 8, "");

    static constexpr auto bits = B;

    using node_t = cnode<T, Hash, Equal, MemoryPolicy, B>;

    node_t* root;
    size_t  size;

    static const champ empty;

    champ(node_t* r, size_t sz)
        : root{r}, size{sz}
    {
    }

    champ(const champ& other)
        : champ{other.root, other.size}
    {
        inc();
    }

    champ(champ&& other)
        : champ{empty}
    {
        swap(*this, other);
    }

    champ& operator=(const champ& other)
    {
        auto next = other;
        swap(*this, next);
        return *this;
    }

    champ& operator=(champ&& other)
    {
        swap(*this, other);
        return *this;
    }

    friend void swap(champ& x, champ& y)
    {
        using std::swap;
        swap(x.root, y.root);
        swap(x.size, y.size);
    }

    ~champ()
    {
        dec();
    }

    void inc() const
    {
        root->inc();
    }

    void dec() const
    {
        dec_traversal(root, 0);
    }

    void dec_traversal(node_t* node, count_t depth) const
    {
        if (node->dec()) {
            if (depth < max_depth<B>) {
                auto fst = node->children();
                auto lst = fst + popcount(node->nodemap());
                for (; fst != lst; ++fst)
                    dec_traversal(*fst, depth + 1);
                node_t::delete_inner(node);
            } else {
                node_t::delete_collision(node);
            }
        }
    }

    template <typename K>
    const T* get(const K& k) const
    {
        auto node = root;
        auto hash = Hash{}(k);
        for (auto i = count_t{}; i < max_depth<B>; ++i) {
            auto bit = 1 << (hash & mask<B>);
            if (node->nodemap() & bit) {
                auto offset = popcount(node->nodemap() & (bit - 1));
                node = node->children() [offset];
                hash = hash >> B;
            } else if (node->datamap() & bit) {
                auto offset = popcount(node->datamap() & (bit - 1));
                auto val    = node->values() + offset;
                if (Equal{}(*val, k))
                    return val;
                else
                    return nullptr;
            } else {
                return nullptr;
            }
        }
        auto fst = node->collisions();
        auto lst = fst + node->collision_count();
        for (; fst != lst; ++fst)
            if (Equal{}(*fst, k))
                return fst;
        return nullptr;
    }

    std::pair<node_t*, bool>
    do_add(node_t* node, T v, hash_t hash, shift_t shift) const
    {
        if (shift == max_shift<B>) {
            auto fst = node->collisions();
            auto lst = fst + node->collision_count();
            for (; fst != lst; ++fst)
                if (Equal{}(*fst, v))
                    return {
                        node_t::copy_collision_replace(node, fst, std::move(v)),
                        false
                    };
            return {
                node_t::copy_collision_insert(node, std::move(v)),
                true
            };
        } else {
            auto idx = (hash & (mask<B> << shift)) >> shift;
            auto bit = 1 << idx;
            if (node->nodemap() & bit) {
                auto offset = popcount(node->nodemap() & (bit - 1));
                auto result = do_add(node->children() [offset],
                                     std::move(v), hash,
                                     shift + B);
                result.first = node_t::copy_inner_replace(
                    node, offset, result.first);
                return result;
            } else if (node->datamap() & bit) {
                auto offset = popcount(node->datamap() & (bit - 1));
                auto val    = node->values() + offset;
                if (Equal{}(*val, v))
                    return {
                        node_t::copy_inner_replace_value(
                            node, offset, std::move(v)),
                        false
                    };
                else {
                    auto child = node_t::make_merged(shift + B,
                                                    std::move(v), hash,
                                                    *val, Hash{}(*val));
                    return {
                        node_t::copy_inner_replace_merged(
                            node, bit, offset, child),
                        true
                    };
                }
            } else {
                return {
                    node_t::copy_inner_insert_value(node, bit, std::move(v)),
                    true
                };
            }
        }
    }

    champ add(T v) const
    {
        auto hash = Hash{}(v);
        auto res = do_add(root, std::move(v), hash, 0);
        auto new_size = size + (res.second ? 1 : 0);
        return { res.first, new_size };
    }

    // basically:
    //      variant<monostate_t, T*, node_t*>
    // boo bad we are not using... C++17 :'(
    struct sub_result
    {
        enum kind_t
        {
            nothing,
            singleton,
            tree
        };

        union data_t
        {
            T*      singleton;
            node_t* tree;
        };

        kind_t kind;
        data_t data;

        sub_result()          : kind{nothing}   {};
        sub_result(T* x)      : kind{singleton} { data.singleton = x; };
        sub_result(node_t* x) : kind{tree}      { data.tree = x; };
    };

    template <typename K>
    sub_result do_sub(node_t* node, const K& k, hash_t hash, shift_t shift) const
    {
        if (shift == max_shift<B>) {
            auto fst = node->collisions();
            auto lst = fst + node->collision_count();
            for (auto cur = fst; cur != lst; ++cur)
                if (Equal{}(*cur, k))
                    return node->collision_count() > 2
                        ? node_t::copy_collision_remove(node, cur)
                        : sub_result{fst + (cur == fst)};
            return {};
        } else {
            auto idx = (hash & (mask<B> << shift)) >> shift;
            auto bit = 1 << idx;
            if (node->nodemap() & bit) {
                auto offset = popcount(node->nodemap() & (bit - 1));
                auto result = do_sub(node->children() [offset],
                                     k, hash, shift + B);
                switch (result.kind) {
                case sub_result::nothing:
                    return {};
                case sub_result::singleton:
                    return popcount(node->datamap()) == 0 &&
                           popcount(node->nodemap()) == 1
                        ? result
                        : node_t::copy_inner_replace_inline(
                            node, bit, offset, *result.data.singleton);
                case sub_result::tree:
                    return node_t::copy_inner_replace(node, offset,
                                                     result.data.tree);
                }
            } else if (node->datamap() & bit) {
                auto offset = popcount(node->datamap() & (bit - 1));
                auto val    = node->values() + offset;
                if (Equal{}(*val, k)) {
                    auto nv = popcount(node->datamap());
                    if (node->nodemap() || nv > 2)
                        return node_t::copy_inner_remove_value(node, bit, offset);
                    else if (nv == 2) {
                        return shift > 0
                            ? sub_result{node->values() + !offset}
                            : node_t::make_inner_n(0,
                                                  node->datamap() & ~bit,
                                                  node->values()[!offset]);
                    } else {
                        assert(shift == 0);
                        return empty.root->inc();
                    }
                }
            }
            return {};
        }
    }

    template <typename K>
    champ sub(const K& k) const
    {
        auto hash = Hash{}(k);
        auto res = do_sub(root, k, hash, 0);
        switch (res.kind) {
        case sub_result::nothing:
            return *this;
        case sub_result::tree:
            return {
                res.data.tree,
                size - 1
            };
        default:
            IMMER_UNREACHABLE;
        }
    }
};

template <typename T, typename H, typename Eq, typename MP, bits_t B>
const champ<T, H, Eq, MP, B> champ<T, H, Eq, MP, B>::empty = {
    node_t::make_inner_n(0),
    0,
};

} // namespace hamts
} // namespace detail
} // namespace immer
