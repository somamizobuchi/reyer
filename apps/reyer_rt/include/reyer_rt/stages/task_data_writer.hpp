#pragma once
#include "reyer_rt/threading/thread.hpp"
#include <reyer/core/core.hpp>
#include <reyer/core/h5.hpp>
#include <reyer/core/queue.hpp>
#include <reyer/plugin/interfaces.hpp>

namespace reyer_rt::stages {

// Owns every HDF5 write for one task's group. HDF5 is built without thread
// safety, so exactly one thread may touch the file — hence eye samples and task
// events both funnel through this thread rather than each getting their own.
//
// Timestamps arrive on the raw monotonic clock in microseconds (see
// reyer::core::now_us) and are rebased against the task's start as they are
// written, so t=0 in the file is the task start and the clock's arbitrary epoch
// never lands on disk.
class TaskDataWriter : public reyer::plugin::SinkBase<reyer::core::EyeData>,
                       public reyer::plugin::IRecorder,
                       public threading::Thread<TaskDataWriter> {
  public:
    TaskDataWriter(hid_t group_id, uint64_t epoch_us)
        : epochUs_(epoch_us), eyeDataset_(group_id, "eye_data"),
          eventDataset_(group_id, "events", kEventChunk) {}

    void Init() {}

    void Run() {
        reyer::core::EyeData data;
        if (eyeDataQueue_.wait_and_pop(data, get_stop_token())) {
            data.timestamp = rebase_(data.timestamp);
            eyeDataset_.write(data);
        }
        // Events piggyback on the sample loop rather than blocking on their own
        // wait, so they land within a sample period of being recorded. Any that
        // arrive after the last sample are caught by Shutdown().
        drainEvents_();
    }

    void Shutdown() {
        drainEvents_();
        eyeDataset_.flush();
        eventDataset_.flush();
    }

    // IRecorder — called from the task's render/consume threads. Only stamps
    // and enqueues; the HDF5 write happens on this writer's thread.
    void recordEvent(int event, uint64_t timestamp) override {
        eventQueue_.push(reyer::core::UserEvent{timestamp, event});
    }

  protected:
    void onConsume(const reyer::core::EyeData &data) override {
        eyeDataQueue_.push(data);
    }

  private:
    static constexpr size_t kEventChunk = 64;

    // Samples already in flight when the task starts can predate the epoch;
    // clamp so the unsigned subtraction cannot wrap.
    uint64_t rebase_(uint64_t timestamp) const {
        return timestamp > epochUs_ ? timestamp - epochUs_ : 0;
    }

    void drainEvents_() {
        bool wrote = false;
        while (auto event = eventQueue_.try_pop()) {
            event->timestamp = rebase_(event->timestamp);
            eventDataset_.write(*event);
            wrote = true;
        }
        // Events are rare and worth having on disk promptly, unlike samples
        // which are left to fill a chunk.
        if (wrote)
            eventDataset_.flush();
    }

    uint64_t epochUs_;
    reyer::h5::Dataset<reyer::core::EyeData> eyeDataset_;
    reyer::h5::Dataset<reyer::core::UserEvent> eventDataset_;
    reyer::core::Queue<reyer::core::EyeData> eyeDataQueue_;
    reyer::core::Queue<reyer::core::UserEvent> eventQueue_;
};

} // namespace reyer_rt::stages
