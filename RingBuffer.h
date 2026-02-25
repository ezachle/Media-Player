#pragma once
#include <memory>
#include <iostream>
#include <mutex>
#include <condition_variable>

template <typename T>
class RingBuffer {
    public:
        explicit RingBuffer(size_t size) : buf(std::make_unique<T[]>(size)), max_size(size){}
        void reset() {
            std::lock_guard lock(rb_mutex);
            head = tail;
            is_full = false;
        }

        bool empty() { return (!is_full && head == tail); }
        bool full() { return is_full; }
        size_t capacity() { return max_size; }
        size_t size() const {
            if(!is_full) {
                if(head >= tail) {
                    return head - tail;
                } else {
                    return max_size + (head - tail);
                }
            }
        }
        
        // Overwrite the stalest object
        void put(T item) {
            std::lock_guard lock(rb_mutex);
            buf[head] = item;
            if( is_full ) {
                tail = (tail + 1) % max_size;
            }

            head = (head + 1) % max_size;
            is_full = (head == tail);
            n_elems++;

            cv.notify_one();
        }
        
        T get() {
            std::unique_lock lock(rb_mutex);
            cv.wait(lock, [this]{ return n_elems > (size_t)1; });
            if(empty()) return T(); // conflict with C functions?

            auto val = buf[tail];
            is_full = false;
            tail = (tail + 1) % max_size;
            n_elems--;
            return val;
        }

        void print() {
            for(size_t i = 0; i < n_elems; i++ ) {
                std::cout << buf[i] << " ";
            }
            std::cout << std::endl;
        }
    private:
        std::mutex rb_mutex;
        std::condition_variable cv;
        std::unique_ptr<T[]> buf;
        size_t head = 0;               // Incremented when adding data
        size_t tail = 0;               // Incremented when removing data
        size_t n_elems = 0;
        const size_t max_size;
        bool is_full = 0;
};
