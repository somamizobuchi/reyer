#pragma once

#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace reyer::core {

template <typename Derived>
class Thread {
  public:
    Thread() = default;

    void Spawn() {
        if (thread_.joinable())
            return;

        thread_ = std::jthread([this](std::stop_token token) {
            threadFcn_(token);
        });
    }

    void Stop() {
        if (!thread_.joinable())
            return;
        thread_.request_stop();
        thread_.join();
    }

    // Queue a callable to run on this thread's loop, before the next Run()
    // iteration. Use this to execute work that must happen on the owning
    // thread (e.g. lifecycle calls) from another thread.
    void postTask(std::function<void()> task) {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        tasks_.push_back(std::move(task));
    }

    ~Thread() {};

  protected:
    std::stop_token get_stop_token() const {
        return thread_.get_stop_token();
    }

  private:
    std::jthread thread_;
    std::mutex tasks_mutex_;
    std::vector<std::function<void()>> tasks_;

    void init_() { static_cast<Derived *>(this)->Init(); }

    void run_() { static_cast<Derived *>(this)->Run(); }

    void shutdown_() { static_cast<Derived *>(this)->Shutdown(); }

    void drainTasks_() {
        std::vector<std::function<void()>> pending;
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            pending.swap(tasks_);
        }
        for (auto &task : pending)
            task();
    }

    void threadFcn_(std::stop_token stop_token) {
        init_();

        while (!stop_token.stop_requested()) {
            drainTasks_();
            run_();
        }

        shutdown_();
    }
};

} // namespace reyer::core
