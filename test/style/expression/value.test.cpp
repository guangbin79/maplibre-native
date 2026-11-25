#include <gtest/gtest.h>
#include <mbgl/style/expression/value.hpp>

#include <future>
#include <thread>
#include <vector>

using namespace mbgl;
using namespace mbgl::style;
using namespace mbgl::style::expression;

// Regression test for the ThreadSanitizer-reported start-up race: tile worker
// threads hit the Formatted branch of ValueConverter<mbgl::Value>::
// fromExpressionValue concurrently at first use and raced the lazily
// initialized "format" operator string.
TEST(Expression, ConcurrentFormattedSerialization) {
    constexpr std::size_t threadCount = 8;
    constexpr int iterations = 100;

    const Formatted formatted(std::vector<FormattedSection>{
        FormattedSection("concurrent", 1.5, FontStack{"first", "second"}, std::nullopt),
        FormattedSection("serialization", std::nullopt, std::nullopt, std::nullopt),
    });

    std::promise<void> barrier;
    const std::shared_future<void> start = barrier.get_future().share();

    std::vector<mbgl::Value> firstResults(threadCount);
    std::vector<mbgl::Value> lastResults(threadCount);
    // Not vector<bool>: its bit packing makes distinct indices share a word,
    // so concurrent writes race (TSan-detectable).
    std::vector<int> consistent(threadCount, 0);
    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) {
        threads.emplace_back([start, &formatted, &firstResults, &lastResults, &consistent, i] {
            start.wait();
            // First use may happen concurrently from any of these threads.
            firstResults[i] = ValueConverter<mbgl::Value>::fromExpressionValue(expression::Value(formatted));
            bool same = true;
            for (int iteration = 0; iteration < iterations; ++iteration) {
                lastResults[i] = ValueConverter<mbgl::Value>::fromExpressionValue(expression::Value(formatted));
                same = same && lastResults[i] == firstResults[i];
            }
            consistent[i] = same;
        });
    }
    barrier.set_value();
    for (auto& thread : threads) {
        thread.join();
    }

    const mbgl::Value expected = ValueConverter<mbgl::Value>::fromExpressionValue(expression::Value(formatted));
    for (std::size_t i = 0; i < threadCount; ++i) {
        EXPECT_EQ(expected, firstResults[i]);
        EXPECT_EQ(expected, lastResults[i]);
        EXPECT_TRUE(consistent[i]);
    }
}
