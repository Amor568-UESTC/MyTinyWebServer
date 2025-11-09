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

    for (int i = 0; i < 1e3; ++i) {
        auto anyFuture1 = tpV2.submit(i, is_prime, rand() % i);
        std::cout<< std::any_cast<bool>(anyFuture1)
        auto anyFuture2 = tpV2.submit(i + 1, fibonacci_iterative, rand() % i);
    }


    return 0;
}