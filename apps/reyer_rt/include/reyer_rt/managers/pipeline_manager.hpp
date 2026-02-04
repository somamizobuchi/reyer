#pragma once

#include <reyer/plugin/defs.hpp>
#include <reyer/plugin/source.hpp>
#include "reyer_rt/threading/thread.hpp"
#include <memory>

namespace reyer_rt::managers
{
    class PipelineManager : public threading::Thread<PipelineManager>
    {
    public:
        explicit PipelineManager(
            reyer::plugin::Plugin source) : source_(source) {}

        ~PipelineManager() = default;

        void Init() {}

        void Run()
        {
            while (!get_stop_token().stop_requested())
            {
                if (source_)
                {
                    auto src = source_.as<reyer::plugin::ISource>();
                    reyer::core::Data data;
                    while (src->pop(&data))
                    {
                        if (render_)
                        {
                            auto render = render_.as<reyer::plugin::IRender>();
                            render->push(&data, 1);
                        }
                    }
                }
            }
        }

        void Shutdown() {}

    private:
        reyer::plugin::Plugin source_;
        reyer::plugin::Plugin render_;
    };
} // namespace reyer_rt::managers
