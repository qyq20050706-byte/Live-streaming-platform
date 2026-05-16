#pragma once

#include "base/IModule.h"
#include "ai/server/AIServer.h"
#include "network/net/EventLoop.h"
#include "base/AIConfig.h"
#include <json/json.h>
#include <memory>
#include <string>

namespace tmms
{
    namespace ai
    {
        /// AI 模块：封装 AIServer 的生命周期管理
        class AIModule : public base::IModule
        {
        public:
            /// @param loop       主事件循环（外部拥有）
            /// @param server_cfg config.json 里的 "ai_server" 节点
            /// @param ai_cfg     doubao.json 解析出的配置
            AIModule(network::EventLoop *loop,
                     const Json::Value &server_cfg,
                     const base::AIConfigInfoPtr &ai_cfg);

            ~AIModule() override = default;

            const std::string &Name() const override { return name_; }

            bool Init() override;
            bool Start() override;
            void Stop() override;

        private:
            std::string name_{"AIModule"};
            network::EventLoop *loop_;
            Json::Value server_cfg_;
            base::AIConfigInfoPtr ai_cfg_;
            std::unique_ptr<AIServer> server_;
        };

    } // namespace ai
} // namespace tmms