#include <vector>
namespace reyer_rt::pipeline {

template <typename T> class IStage {
  public:
    virtual void process(T &data) = 0;
    virtual ~IStage() = default;
};

template <typename T> class Pipeline {
  public:
    Pipeline() = default;

    void addStage(IStage<T>&& stage) {
        stages_.push_back(stage);
    }

    void process(T& data) {
        for (auto& stage: stages_) {
            stage->process(data);
        }
    }

    ~Pipeline() = default;

  private:
    std::vector<std::shared_ptr<IStage<T>>> stages_;
};

} // namespace reyer_rt::pipeline
