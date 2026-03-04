#pragma once
extern "C" {
#include <ffmpeg/libavutil/frame.h>
}
#include <memory>
#include <condition_variable>
#include <mutex>
#include <queue>

/*
 * Uses the C++ STL Queue to handle how frames are
 * moved from the decode packets thread to the respective
 * AV thread
 * 
 * Both push() and pop() utilize the not_full and not_empty
 * condition_variable to manage when to push and pop off the
 * queue.
 *
 */

using Frame = std::unique_ptr<AVFrame>;
class FrameQueue {
    public:
        FrameQueue(size_t size) : max_size(size){}
        void push(AVFrame *frame) {
            if(!frame) return;
            std::unique_lock<std::mutex> lock(mtx);

            // Don't queue any frames if already full
            not_full.wait(lock, [this]() { return quit_flag || queue.size() < max_size; });
            if(quit_flag) return;

            queue.push(Frame(frame));
            not_empty.notify_one();
        }

        AVFrame* pop() {
            std::unique_lock<std::mutex> lock(mtx);
            // Wait to receive something
            not_empty.wait(lock, [this](){ return quit_flag || queue.size() > 0; });
            if(quit_flag) return nullptr;

            Frame f = std::move(queue.front());
            queue.pop();

            not_full.notify_one();
            return f.release();
        }

        void quit() { 
            quit_flag = true;
            not_empty.notify_one();
            not_full.notify_one();
        }

        size_t get_size() { return queue.size(); }

        ~FrameQueue() {
            while(!queue.empty()) {
                queue.pop();
            }
        }
    private:
        std::queue<Frame> queue;
        std::mutex mtx;
        std::condition_variable not_full;   // Disallows enqueuing frames when the size exceeds the max
        std::condition_variable not_empty;  // Disallows dequeuing frames when there are 0 frames
        bool quit_flag = false;
        size_t max_size;
};
