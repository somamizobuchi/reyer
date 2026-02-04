#include "iplugin.hpp"
#include <reyer/core/queue.hpp>

namespace reyer::plugin
{
    class ISource : public IProducer<core::Data> {
    public:
        virtual ~ISource() = default;
    };

    template <typename TConfig>
    class SourcePluginBase : public PluginBase<TConfig>, public ISource
    {
    public:
        virtual ~SourcePluginBase() = default;

        virtual PluginType getType() const override final
        {
            return PluginType::SOURCE;
        }

        virtual bool pop(core::Data *data) override final
        {
            auto output = output_queue_.try_pop();
            if (!output)
            {
                return false;
            }
            *data = output.value();
            return true;
        }
    protected:
        void pushData(const core::Data &data)
        {
            output_queue_.push(data);
        }

    private:
        core::Queue<core::Data> output_queue_;
    };
} // namespace reyer::plugin