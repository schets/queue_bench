#include <thread>
#include <string>
#include <iostream>
#include <ctime>
#include <stdint.h>

#include "bounded.hpp"
#include "unbounded.hpp"
using namespace std;

int * const end_val = (int *)2;
int * const queue_val = (int *)1;

void spin() {
    for (volatile int i = 0; i < 30; i++) {}
}

template<class Q, bool backoff = false, bool work = false>
void pop_queue(Q& queue) {
    int *qval;
    while ((qval = queue.PopFront()) != end_val) {
        if (backoff) {
            if (qval == nullptr) {
                spin();
            }
        }
        if (work) {
            if (qval != nullptr) {
                spin();
            }
        }
    }
}

template<class Q, bool backoff = false, bool work = false>
void push_queue(Q& queue, size_t topush) {
    for (size_t i = 0; i < topush; i++) {
        bool rval;
        do {
           rval = queue.PushBack(queue_val);
           if (backoff) {
             if (!rval) {
                    spin();
                }
            }
        } while(!rval);
        if (work) {
            spin();
        }
    }
    queue.PushBack(end_val);
}

template<class Q>
void bench_queue(size_t topush, std::string name) {
    Q myqueue;
    auto ctime = clock();
    std::thread t1([&] () {
        push_queue<Q, false, true>(myqueue, topush);
    });
    std::thread t2([&]() {
        pop_queue<Q, false, true>(myqueue);
    });
    t1.join();
    t2.join();
    ctime = clock() - ctime;
    double dtime = ctime * 1.0 / CLOCKS_PER_SEC;
    cout << "It took " << name << " " << dtime << " seconds to push/pop " << topush << " elements" << endl;
    cout << name << " pushed/popped " << (topush/dtime) << " elements per second, " << (1e9 * dtime / topush) << " ns per element" << endl;
}

int main() {
    bench_queue<unbounded::ProducerConsumerQueue<int>>(3e8, "Unbounded Queue");
    bench_queue<bounded::ProducerConsumerQueue<4096, int>>(3e8, "Bounded Queue");
    return 0;
}
