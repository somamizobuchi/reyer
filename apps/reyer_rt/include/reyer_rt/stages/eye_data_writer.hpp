#pragma once
#include "reyer_rt/threading/thread.hpp"
#include <reyer/core/h5.hpp>
#include <reyer/core/queue.hpp>
#include <reyer/plugin/interfaces.hpp>

namespace reyer_rt::stages {

class EyeDataWriter : public reyer::plugin::SinkBase<reyer::core::EyeData>,
                      public threading::Thread<EyeDataWriter> {
  public:
    explicit EyeDataWriter(hid_t group_id)
        : dataset_(group_id, "eye_data") {}

    void Init() {}

    void Run() {
        reyer::core::EyeData data;
        if (dataQueue_.wait_and_pop(data, get_stop_token())) {
            dataset_.write(data);
        }
    }

    void Shutdown() { dataset_.flush(); }

  protected:
    void onConsume(const reyer::core::EyeData &data) override {
        dataQueue_.push(data);
    }

  private:
    reyer::h5::Dataset<reyer::core::EyeData> dataset_;
    reyer::core::Queue<reyer::core::EyeData> dataQueue_;
};

} // namespace reyer_rt::stages
