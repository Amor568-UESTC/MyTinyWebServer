#include <iostream>
#include <chrono>
#include <atomic>
#include <sstream>
#include <cassert>

#include "../threadPoolV2.h"

class ThreadPoolV2Test {
private:
    std::atomic<int> _testCounter{0};
    
public:
    void runAllTests() {
        std::cout << "=== ThreadPoolV2 Test Suite ===\n" << std::endl;
        
        testBasicFunctionality();
        testPriorityOrder();
        testReturnValues();
        testExceptionHandling();
        testThreadSafety();
        testPerformance();
        
        std::cout << "\n=== All Tests Completed ===" << std::endl;
    }

private:
    void testBasicFunctionality() {
        std::cout << "1. Testing Basic Functionality..." << std::endl;
        _testCounter = 0;
        
        {
            ThreadPoolV2 pool(2);
            
            auto future1 = pool.submit([this]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                return ++_testCounter;
            });
            
            auto future2 = pool.submit([this]() {
                return ++_testCounter;
            });
            
            assert(future1.get() + future2.get() == 3);
            std::cout << "   ✓ Basic task execution passed" << std::endl;
        }
        
        std::cout << "   ✓ Pool destruction and cleanup passed" << std::endl;
    }

    void testPriorityOrder() {
        std::cout << "2. Testing Priority Order..." << std::endl;
        
        ThreadPoolV2 pool(1);
        std::atomic<int> executionOrder{0};
        std::promise<void> blockPromise;
        auto blockFuture = blockPromise.get_future();
        
        // first submit a blocking task to occupy the only worker thread
        pool.submit([&]() {
            blockFuture.get();  // wait for "set_value()"
            return 0;
        });
        
        // from LOW to URGENT submit
        pool.submit(AnyReturnPriTask::Priority::LOW, [&]() {
            executionOrder = executionOrder * 10 + 1;
            return 1;
        });
        
        pool.submit(AnyReturnPriTask::Priority::HIGH, [&]() {
            executionOrder = executionOrder * 10 + 2; 
            return 2;
        });
        
        pool.submit(AnyReturnPriTask::Priority::URGENT, [&]() {
            executionOrder = executionOrder * 10 + 3;
            return 3;
        });
        
        // release blocked task
        blockPromise.set_value();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // should be : URGENT(3) → HIGH(2) → LOW(1)
        assert(executionOrder == 321);
        std::cout << "   Execution order: " << executionOrder << std::endl;
        std::cout << "   ✓ Priority ordering passed" << std::endl;
    }
    
    void testReturnValues() {
        std::cout << "3. Testing Return Values..." << std::endl;
        
        ThreadPoolV2 pool(2);
        
        // Test int return
        auto intFuture = pool.submit([]() { return 42; });
        assert(intFuture.get() == 42);
        std::cout << "   ✓ Integer return values passed" << std::endl;
        
        // Test string return
        auto stringFuture = pool.submit([]() { return std::string("hello"); });
        assert(stringFuture.get() == "hello");
        std::cout << "   ✓ String return values passed" << std::endl;
        
        // Test void return
        bool voidExecuted = false;
        auto voidFuture = pool.submit([&voidExecuted]() { voidExecuted = true; });
        voidFuture.get();
        assert(voidExecuted);
        std::cout << "   ✓ Void return values passed" << std::endl;
        
        // Test with arguments
        auto argFuture = pool.submit([](int a, int b) { return a + b; }, 10, 20);
        assert(argFuture.get() == 30);
        std::cout << "   ✓ Function with arguments passed" << std::endl;
    }

    void testExceptionHandling() {
        std::cout << "4. Testing Exception Handling..." << std::endl;
        
        ThreadPoolV2 pool(2);
        
        auto future = pool.submit([]() {
            throw std::runtime_error("Test exception");
            return 1;
        });
        
        try {
            future.get();
            assert(false && "Should have thrown exception");
        } catch (const std::exception& e) {
            assert(std::string(e.what()) == "Test exception");
            std::cout << "   ✓ Exception propagation passed" << std::endl;
        }
    }

    void testThreadSafety() {
        std::cout << "5. Testing Thread Safety..." << std::endl;
        
        ThreadPoolV2 pool(4);
        const int TASK_COUNT = 100;
        std::atomic<int> completedTasks{0};
        std::vector<std::future<int>> futures;
        
        for (int i = 0; i < TASK_COUNT; ++i) {
            auto future = pool.submit([i, &completedTasks]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                completedTasks++;
                return i * 2;
            });
            futures.push_back(std::move(future));
        }
        
        // Verify all results
        for (int i = 0; i < TASK_COUNT; ++i) {
            assert(futures[i].get() == i * 2);
        }
        
        assert(completedTasks == TASK_COUNT);
        std::cout << "   ✓ Thread safety with " << TASK_COUNT << " tasks passed" << std::endl;
    }

    void testPerformance() {
        std::cout << "6. Testing Performance..." << std::endl;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        ThreadPoolV2 pool;
        const int TASK_COUNT = 1000;
        std::vector<std::future<int>> futures;
        
        for (int i = 0; i < TASK_COUNT; ++i) {
            futures.push_back(pool.submit([i]() { return i; }));
        }
        
        for (int i = 0; i < TASK_COUNT; ++i) {
            assert(futures[i].get() == i);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "   ✓ Processed " << TASK_COUNT << " tasks in " 
                  << duration.count() << "ms" << std::endl;
    }
};

int main() {
    ThreadPoolV2Test tester;
    tester.runAllTests();
    
    // Additional demo
    std::cout << "\n=== Demonstration ===" << std::endl;
    {
        ThreadPoolV2 pool;
        
        std::cout << "Thread pool size: " << pool.size() << std::endl;
        
        // Mixed priorities demonstration
        auto lowTask = pool.submit(AnyReturnPriTask::Priority::LOW, []() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return "Low priority task";
        });
        
        auto highTask = pool.submit(AnyReturnPriTask::Priority::HIGH, []() {
            return "High priority task";
        });
        
        std::cout << "High priority result: " << highTask.get() << std::endl;
        std::cout << "Low priority result: " << lowTask.get() << std::endl;
        
        std::cout << "Active threads: " << pool.getActiveThreads() << std::endl;
    }
    
    return 0;
}