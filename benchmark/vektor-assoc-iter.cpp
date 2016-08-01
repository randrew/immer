
#include <nonius/nonius_single.h++>

#include <immu/vektor.hpp>
#include <immu/dvektor.hpp>
#include <vector>
#include <list>

extern "C" {
#define restrict __restrict__
#include <rrb.h>
#undef restrict
}

constexpr auto benchmark_size = 1000u;

NONIUS_BENCHMARK("std::vector", [] (nonius::chronometer meter)
{
    auto v = std::vector<unsigned>{};
    for (auto i = 0u; i < benchmark_size; ++i)
        v.push_back(i);
    auto all = std::vector<std::vector<unsigned>>(meter.runs(), v);

    meter.measure([&] (int iter) {
        auto& r = all[iter];
        for (auto i = 0u; i < benchmark_size; ++i)
            r[i] = benchmark_size - i;
        return r;
    });
})

NONIUS_BENCHMARK("librrb", [] (nonius::chronometer meter)
{
    auto v = rrb_create();
    for (auto i = 0u; i < benchmark_size; ++i)
        v = rrb_push(v, reinterpret_cast<void*>(i));

    meter.measure([&] {
        auto r = v;
        for (auto i = 0u; i < benchmark_size; ++i)
            r = rrb_update(r, i, reinterpret_cast<void*>(benchmark_size - i));
        return r;
    });
    return v;
})

NONIUS_BENCHMARK("immu::vektor", [] (nonius::chronometer meter)
{
    auto v = immu::vektor<unsigned>{};
    for (auto i = 0u; i < benchmark_size; ++i)
        v = v.push_back(i);

    meter.measure([&] {
        auto r = v;
        for (auto i = 0u; i < benchmark_size; ++i)
            r = v.assoc(i, benchmark_size - i);
        return r;
    });
})

NONIUS_BENCHMARK("immu::dvektor", [] (nonius::chronometer meter)
{
    auto v = immu::dvektor<unsigned>{};
    for (auto i = 0u; i < benchmark_size; ++i)
        v = v.push_back(i);

    meter.measure([&] {
        auto r = v;
        for (auto i = 0u; i < benchmark_size; ++i)
            r = v.assoc(i, benchmark_size - i);
        return r;
    });
})