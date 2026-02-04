#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>

namespace reyer_rt::threading
{
    template <typename Derived>
    class Thread
    {
    public:
        Thread() = default;

        void Spawn()
        {
            if (thread_.joinable())
                return;

            thread_ = std::jthread([this](std::stop_token token)
                                   { threadFcn_(token); });
        }

        void Stop()
        {
            if (!thread_.joinable())
                return;
            thread_.request_stop();
            thread_.join();
        }

        void Pause()
        {
            if (!thread_.joinable())
                return;
            pause_requested_.store(true, std::memory_order_release);
            pause_cv_.notify_one();
        }

        void Resume()
        {
            if (!thread_.joinable())
                return;
            pause_requested_.store(false, std::memory_order_release);
            pause_cv_.notify_one();
        }

        ~Thread() {};

    protected:
        std::stop_token get_stop_token() const { return thread_.get_stop_token(); }

    private:
        std::jthread thread_;
        std::condition_variable_any pause_cv_;
        std::mutex pause_mtx_;
        std::atomic<bool> pause_requested_{false};

        void init_() { static_cast<Derived *>(this)->Init(); }

        void run_() { static_cast<Derived *>(this)->Run(); }

        void shutdown_() { static_cast<Derived *>(this)->Shutdown(); }

        void threadFcn_(std::stop_token stop_token)
        {
            init_();

            while (!stop_token.stop_requested())
            {
                // Check for pause request
                if (pause_requested_.load(std::memory_order_acquire))
                {
                    std::unique_lock<std::mutex> mtx(pause_mtx_);
                    bool resume = pause_cv_.wait_for(
                        mtx, stop_token, std::chrono::milliseconds(10),
                        [&]
                        { return !pause_requested_; });
                    // Shutdown request
                    if (stop_token.stop_requested())
                        break;
                    // Timeout
                    if (!resume)
                        continue;
                }
                // Run thread operation
                run_();
            }

            shutdown_();
        }
    };
} // namespace reyer_rt::threading
