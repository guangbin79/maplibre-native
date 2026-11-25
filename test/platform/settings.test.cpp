#include <gtest/gtest.h>
#include <mbgl/platform/settings.hpp>

#include <future>
#include <string>
#include <thread>
#include <vector>

using namespace mbgl;
using namespace mbgl::platform;

TEST(Settings, SetAndGet) {
    using Value = mapbox::base::Value;
    using Object = mapbox::base::ValueObject;

    auto& settings = Settings::getInstance();
    auto value = settings.get(EXPERIMENTAL_THREAD_PRIORITY_WORKER);
    EXPECT_TRUE(value.is<mapbox::base::NullValue>());

    Value lowPrioValue{19.0};
    settings.set(EXPERIMENTAL_THREAD_PRIORITY_WORKER, lowPrioValue);
    auto threadPriority = settings.get(EXPERIMENTAL_THREAD_PRIORITY_WORKER);
    EXPECT_EQ(lowPrioValue, threadPriority);
    EXPECT_EQ(lowPrioValue, settings.get("mapbox_thread_priority_worker"));

    Value networkPrioValue{-20.0};
    Value databasePrioValue{-10.0};
    Object multipleValues{{EXPERIMENTAL_THREAD_PRIORITY_DATABASE, databasePrioValue},
                          {EXPERIMENTAL_THREAD_PRIORITY_NETWORK, networkPrioValue}};
    settings.set(std::move(multipleValues));

    std::vector<std::string> keys{EXPERIMENTAL_THREAD_PRIORITY_WORKER,
                                  EXPERIMENTAL_THREAD_PRIORITY_DATABASE,
                                  EXPERIMENTAL_THREAD_PRIORITY_NETWORK,
                                  EXPERIMENTAL_THREAD_PRIORITY_FILE};

    auto object = settings.get(keys);

    Object values{{EXPERIMENTAL_THREAD_PRIORITY_WORKER, lowPrioValue},
                  {EXPERIMENTAL_THREAD_PRIORITY_DATABASE, databasePrioValue},
                  {EXPERIMENTAL_THREAD_PRIORITY_NETWORK, networkPrioValue},
                  {EXPERIMENTAL_THREAD_PRIORITY_FILE, Value{}}};

    EXPECT_EQ(values, object);

    values = Object{{EXPERIMENTAL_THREAD_PRIORITY_WORKER, Value{}},
                    {EXPERIMENTAL_THREAD_PRIORITY_DATABASE, Value{}},
                    {EXPERIMENTAL_THREAD_PRIORITY_NETWORK, Value{}},
                    {EXPERIMENTAL_THREAD_PRIORITY_FILE, Value{}}};

    settings.set(values);
    EXPECT_EQ(values, settings.get(keys));
}

// Regression test for the ThreadSanitizer-reported start-up race: worker
// threads (see util/thread_pool.cpp) hit the singleton accessor concurrently
// at first use and raced its lazy initialization.
TEST(Settings, ConcurrentInstanceAccess) {
    constexpr std::size_t threadCount = 8;
    constexpr int iterations = 100;

    std::promise<void> barrier;
    const std::shared_future<void> start = barrier.get_future().share();

    std::vector<mapbox::base::Value> firstReads(threadCount);
    // Not vector<bool>: its bit packing makes distinct indices share a word,
    // so concurrent writes race (TSan-detectable).
    std::vector<int> roundtrips(threadCount, 0);
    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) {
        threads.emplace_back([start, &firstReads, &roundtrips, i] {
            start.wait();
            // First use may happen concurrently from any of these threads.
            auto& settings = Settings::getInstance();
            firstReads[i] = settings.get(EXPERIMENTAL_THREAD_PRIORITY_WORKER);

            const std::string key = "concurrent_settings_test_key_" + std::to_string(i);
            const mapbox::base::Value value{static_cast<double>(i)};
            bool roundtrip = true;
            for (int iteration = 0; iteration < iterations; ++iteration) {
                settings.set(key, value);
                roundtrip = roundtrip && settings.get(key) == value;
            }
            roundtrips[i] = roundtrip;
        });
    }
    barrier.set_value();
    for (auto& thread : threads) {
        thread.join();
    }

    // All threads must have observed the same value on first read.
    for (std::size_t i = 1; i < threadCount; ++i) {
        EXPECT_EQ(firstReads.front(), firstReads[i]);
    }
    for (std::size_t i = 0; i < threadCount; ++i) {
        EXPECT_TRUE(roundtrips[i]);
        EXPECT_EQ(mapbox::base::Value{static_cast<double>(i)},
                  Settings::getInstance().get("concurrent_settings_test_key_" + std::to_string(i)));
    }
}
