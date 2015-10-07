#include <stdint.h>
#include <stddef.h>
#include <atomic>

namespace bounded {
template<uint32_t size, class T>
class ProducerConsumerQueue {
  public:
    // size must be >= 2.
    //
    // Also, note that the number of usable slots in the queue at any
    // given time is actually (size-1), so if you start with an empty queue,
    // PushBack will fail after size-1 insertions.
    ProducerConsumerQueue() : readIndex_(0), writeIndex_(0) {
      memset(&records_, 0, sizeof(records_[0]) * size);
    }

    // Returns false if the queue is full, cannot insert nullptr
    bool PushBack(T* item) {
      auto const currentWrite = writeIndex_.load(std::memory_order_relaxed);
      auto nextRecord = currentWrite + 1;
      if (nextRecord == size) {
        nextRecord = 0;
      }

      if (nextRecord != readIndex_.load(std::memory_order_acquire)) {
        records_[currentWrite] = item;
        writeIndex_.store(nextRecord, std::memory_order_release);
        return true;
      }

      // queue is full
      return false;
    }

    // Returns nullptr if the queue is empty.
    T* PopFront() {
      auto const currentRead = readIndex_.load(std::memory_order_relaxed);
      if (currentRead == writeIndex_.load(std::memory_order_acquire)) {
        // queue is empty
        return nullptr;
      }

      auto nextRecord = currentRead + 1;
      if (nextRecord == size) {
        nextRecord = 0;
      }
      T* ret = records_[currentRead];
      readIndex_.store(nextRecord, std::memory_order_release);
      return ret;
    }
  private:
    static const size_t kCacheLineLength = 128;
    typedef char padding[kCacheLineLength];
    padding padding1_;
    T* records_[size];
    padding padding2_;
    std::atomic<unsigned int> readIndex_;
    padding padding3_;
    std::atomic<unsigned int> writeIndex_;
    padding padding4_;
};

}
