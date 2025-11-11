#include "../threadPoolV2.h"

#include <iostream>

bool is_prime(int n) {
    if (n <= 1) return false;
    if (n <= 3) return true;
    if (n % 2 == 0 || n % 3 == 0) return false;
    
    for (int i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0) return false;
    }
    return true;
}

long long fibonacci_iterative(int n) {
    if (n <= 1) return n;
    
    long long a = 0, b = 1;
    for (int i = 2; i <= n; ++i) {
        long long temp = a + b;
        a = b;
        b = temp;
    }
    return b;
}

int main() {
    ThreadPoolV2 tpV2{};

    srand(time(NULL));

    std::cout << tpV2.size() << std::endl;

    std::cout << "==========================" << std::endl;

    for (int i = 0; i < 1e3; ++i) {
        auto anyFuture1 = tpV2.submit(is_prime, rand() % 1000);
        std::cout << "ActiveThreads : " << tpV2.getActiveThreads() << std::endl;
        auto anyFuture2 = tpV2.submit(AnyReturnPriTask::Priority::HIGH, fibonacci_iterative, rand() % 1000);
        std::cout << anyFuture1.get() << std::endl;
        std::cout << anyFuture2.get() << std::endl;
    }

    return 0;
}