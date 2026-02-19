#include <reyer/plugin/plugin.hpp>
#include <ddpi-suite/client/ddpi_client.hpp>

namespace reyer::plugin::ddpi_ipc_source {

    struct Configuration {};

    class DDPIIPCSource : public SourcePluginBase<Configuration> {
        public:
        DDPIIPCSource() : client_() {}
        ~DDPIIPCSource() = default;

        protected:
            void onInit() override {
                client_ = std::make_unique<ddpi::suite::client::DdpiClient>("ipc:///tmp/ddpi-data.sock");
                client_->initialize();
            }

            void onShutdown() override {
                client_->shutdown();
            }

            void onPause() override {}
            void onResume() override {}
            void onReset() override {}

            bool onProduce(core::EyeData &out) override {
                ddpi::ddpi_data_t data;
                if (client_->receive(data, 10)) {
                    if (data.eye_left.p1.has_value()) {
                        out.left.dpi.p1.x = std::move(data.eye_left.p1.value().x);
                        out.left.dpi.p1.y = std::move(data.eye_left.p1.value().y);
                    }
                    if (data.eye_left.p4.has_value()) {
                        out.left.dpi.p4.x = std::move(data.eye_left.p4.value().x);
                        out.left.dpi.p4.y = std::move(data.eye_left.p4.value().y);
                    }
                    if (data.eye_right.p1.has_value()) {
                        out.right.dpi.p1.x = std::move(data.eye_right.p1.value().x);
                        out.right.dpi.p1.y = std::move(data.eye_right.p1.value().y);
                    }
                    if (data.eye_right.p4.has_value()) {
                        out.right.dpi.p4.x = std::move(data.eye_right.p4.value().x);
                        out.right.dpi.p4.y = std::move(data.eye_right.p4.value().y);
                    }
                    out.timestamp = std::move(data.timestamp);
                    return true;
                }
                return false;
            }
        private:
            std::unique_ptr<ddpi::suite::client::DdpiClient> client_;
    };

} // namespace reyer::plugin::ddpi_ipc_source

REYER_PLUGIN_ENTRY(reyer::plugin::ddpi_ipc_source::DDPIIPCSource, "DDPI IPC Source", "Soma Mizobuchi", "DDPI IPC Source", 1);