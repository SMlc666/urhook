#include "ur/thread.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <unistd.h>
#include <vector>
#include <algorithm>

TEST(ThreadTest, GetAllThreads)
{
    std::atomic<bool> running = true;
    std::vector<pid_t> tids;
    tids.push_back(gettid());

    std::vector<std::thread> threads;
    std::atomic<int> ready_threads = 0;
    const int num_threads = 3;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            tids.push_back(gettid());
            ready_threads++;
            while (running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }

    while (ready_threads < num_threads) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto all_tids = ur::thread::get_all_threads();
    
    // Sort both vectors to compare them
    std::sort(tids.begin(), tids.end());
    std::sort(all_tids.begin(), all_tids.end());

    // Check if our manually collected tids are a subset of the ones found by the function
    ASSERT_TRUE(std::includes(all_tids.begin(), all_tids.end(), tids.begin(), tids.end()));

    running = false;
    for (auto& t : threads) {
        t.join();
    }
}

TEST(ThreadTest, GetCurrentTid)
{
    ASSERT_EQ(ur::thread::get_current_tid(), gettid());
    std::thread t([]() {
        ASSERT_EQ(ur::thread::get_current_tid(), gettid());
    });
    t.join();
}
