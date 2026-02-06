#pragma once

#include "reyer_rt/managers/graphics_manager.hpp"
#include "reyer_rt/managers/pipeline_manager.hpp"
#include "reyer_rt/managers/plugin_manager.hpp"
#include "reyer_rt/net/message_types.hpp"
#include "reyer_rt/net/reply_socket.hpp"
#include "reyer_rt/threading/thread.hpp"
#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <system_error>
#include <variant>

namespace reyer_rt::managers {

class MessageManager : public threading::Thread<MessageManager> {
    friend struct MessageVisitor;

    using MessageVariant =
        std::variant<net::message::Ping, net::message::GraphicsSettingsRequest,
                     net::message::ProtocolRequest,
                     net::message::PipelineConfigRequest,
                     net::message::ResourceRequest,
                     net::message::CommandRequest>;

  public:
    explicit MessageManager(std::shared_ptr<GraphicsManager> &graphics_manager,
                            std::shared_ptr<PluginManager> &plugin_manager,
                            std::shared_ptr<PipelineManager> &pipeline_manager);

    void Init();
    void Shutdown();
    void Run();

    ~MessageManager() = default;

  protected:
    std::error_code SendErrorResponse(const std::error_code &ec,
                                      std::string_view payload);

    std::error_code SendResponse(net::message::Response &response);

    // Helper methods for unified response pattern
    template <typename T>
    std::expected<std::shared_ptr<T>, std::error_code>
    LockManager(std::weak_ptr<T> &weak_ptr);

    net::message::Response CreateSuccessResponse(std::string payload = "");

    net::message::Response CreateErrorResponse(std::error_code ec,
                                               std::string_view context = "");

    template <typename T>
    std::expected<std::string, std::error_code> SerializePayload(const T &data);

    std::expected<net::message::Response, std::error_code>
    BuildPluginInfoResponse(const std::vector<std::string> &plugin_names,
                            std::shared_ptr<PluginManager> &plugin_manager);

  private:
    net::ReplySocket rep_;
    std::string send_buffer_;
    std::string recv_buffer_;

    std::weak_ptr<GraphicsManager> graphics_manager_;
    std::weak_ptr<PluginManager> plugin_manager_;
    std::weak_ptr<PipelineManager> pipeline_manager_;

    struct MessageVisitor {
        MessageManager &manager;

        std::expected<net::message::Response, std::error_code>
        operator()(const net::message::Ping &ping);

        std::expected<net::message::Response, std::error_code>
        operator()(const net::message::GraphicsSettingsRequest &request);

        std::expected<net::message::Response, std::error_code>
        operator()(const net::message::ProtocolRequest &request);

        std::expected<net::message::Response, std::error_code>
        operator()(const net::message::ResourceRequest &request);

        std::expected<net::message::Response, std::error_code>
        operator()(const net::message::PipelineConfigRequest &request);

        std::expected<net::message::Response, std::error_code>
        operator()(const net::message::CommandRequest &request);
    };

    MessageVisitor message_visitor_;
};

} // namespace reyer_rt::managers
