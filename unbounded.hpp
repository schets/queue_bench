#include <stdint.h>
#include <stddef.h>
#include <atomic>

namespace unbounded {

template<class T>
class ProducerConsumerQueue {

  constexpr static size_t num_ptrs = 256;
  constexpr static size_t num_blocks = 4;
  constexpr static size_t cache_size = 64;

  struct queue_block {
    char b1[cache_size];
    std::atomic<queue_block *>next;
    T *ptrs[num_ptrs];
    char b2[cache_size];
  };


  //the implementation works
  //on both 32 and 64 bit,
  //since the actual indexing ins done mod 128
  //and unsigned integer math is done mod 2^(32, 64).
  //In 32 bit, you can only have 2^32 elements
  //in the queue, but at that point you have also
  //exhausted the address space
  typedef uint64_t index_type;

  typedef char buffer[cache_size];
  buffer b1; //block from sharing with other objects behind

  //head data
  index_type head;
  index_type tail_cache;
  queue_block *head_block;

  buffer b2;

  //tail data
  std::atomic<index_type> tail;
  std::atomic<queue_block *> tail_block;

  buffer b3;

  //shared_data

  //hold a few blocks to avoid a possibly locking malloc
  std::atomic<queue_block *> block_stack;
  std::atomic<uint32_t> stack_counter;

  queue_block *get_block() {

    //this adds an unneeded memory barrier on arm32 and powerpc
    //but according to the standard, it isn't correct otherwise
    //same with the consume barrier further down
    //x86 isn't affected at all, of course
    auto cstack = block_stack.load(std::memory_order_consume);
    queue_block *next;
    //really low contention here, so a cas loop is fine
    do {
      //pull off top - since this is only remover,
      //we avoid the aba problem
      if (cstack == nullptr) {
        return new queue_block;
      }

      next = cstack->next.load(std::memory_order_relaxed);
    } while (!block_stack.compare_exchange_weak(cstack,
                                                next,
                                                std::memory_order_release,
                                                std::memory_order_consume));

    stack_counter.fetch_sub(1, std::memory_order_relaxed);
    return cstack;
  }

  void return_block(queue_block *blk) {

    if (stack_counter.load(std::memory_order_relaxed) > num_blocks) {
      delete blk;
      return;
    }

    stack_counter.fetch_add(1, std::memory_order_relaxed);
    auto cstack = block_stack.load(std::memory_order_relaxed);

    do {
      blk->next.store(cstack, std::memory_order_relaxed);
    } while (!block_stack.compare_exchange_weak(cstack,
                                                blk,
                                                std::memory_order_release,
                                                std::memory_order_relaxed));
  }

  queue_block *add_tail(queue_block *tblock) {
    auto nblock = get_block();
    nblock->next.store(nullptr, std::memory_order_relaxed);
    tblock->next.store(nblock, std::memory_order_relaxed);
    tail_block.
    store(nblock, std::memory_order_relaxed);
    return nblock;
  }

  void remove_head() {
    auto nblock = head_block->next.load(std::memory_order_relaxed);
    return_block(head_block);
    head_block = nblock;
  }

public:

  bool PushBack(T *item) {
    auto ctail = tail.load(std::memory_order_relaxed);
    auto tblock = tail_block.load(std::memory_order_relaxed);
    auto cind = ctail & (num_ptrs - 1);
    if (cind == 0) {
      // we start off at the first element, so that
      //we avoid the condition where the & returns a new block
      //before starting anything
      tblock = add_tail(tblock);
    }
    tblock->ptrs[cind] = item;
    tail.store(ctail + 1, std::memory_order_release);
    return true;
  }

  T *PopFront() {

    if (head == tail_cache) {
      tail_cache = tail.load(std::memory_order_acquire);
      if (head == tail_cache) {
        return nullptr;
      }
    }

    auto hind = head & (num_ptrs - 1);
    head++;
    if (hind == 0) {
        remove_head();
    }

    return head_block->ptrs[hind];
  }

  ProducerConsumerQueue() {

    //set up interal memory
    block_stack.store(nullptr, std::memory_order_relaxed);
    stack_counter.store(0, std::memory_order_relaxed);

    //build queue
    head_block = get_block();
    tail_block.store(head_block, std::memory_order_relaxed);
    head_block->next.store(nullptr, std::memory_order_relaxed);

    //start at one to avoid new-block checks at beginning
    head = 1;
    tail_cache = 1;
    tail.store(head, std::memory_order_relaxed);

    //thread fence so all these writes are visible to other threads
    //that acquire the queue
    std::atomic_thread_fence(std::memory_order_release);
  }

  ~ProducerConsumerQueue() {

    //ensure that all reads/writes in here
    //occur after the fence, enforcing that
    //they are not visible before anything pre-destructor
    std::atomic_thread_fence(std::memory_order_acquire);

    T *todel;
    while ((todel = PopFront())) {
  //    delete todel;
    }

    auto shead = block_stack.load(std::memory_order_relaxed);
    while (shead) {
      auto todel = shead;
      shead = shead->next.load(std::memory_order_relaxed);
      delete todel;
    }

    while (head_block) {
      auto bdel = head_block;
      head_block = head_block->next.load(std::memory_order_relaxed);
      delete bdel;
    }
  }
};

}
