#pragma once
#include "ILLMProvider.h"
#include "ai/http/HttpClient.h"
#include "base/AIConfig.h"
#include <json/json.h>
#include <string>

namespace tmms
{
    namespace ai
    {
        class DoubaoLLM : public ILLMProvider
        {
        public:
            explicit DoubaoLLM(const base::AIConfigInfoPtr &cfg);
            ~DoubaoLLM() override = default;

            // 原有接口
            bool Chat(const std::string &prompt,
                      std::string &answer) override;

            bool ChatStream(const std::string &prompt,
                            StreamCallback callback,
                            std::string &err_msg) override;

            // 新增：多轮对话接口
            bool ChatWithMessages(const Json::Value &messages,
                                  std::string &answer) override;

            bool ChatStreamWithMessages(const Json::Value &messages,
                                        StreamCallback callback,
                                        std::string &err_msg) override;

        private:
            std::string base_url_;
            std::string api_key_;
            std::string model_;
            int timeout_ms_{60000};
            HttpClient client_;

            // 内部公共方法：发起 HTTP 请求
            bool DoChat(const Json::Value &messages,
                        bool stream,
                        StreamCallback callback,
                        std::string &answer,
                        std::string &err);
        };

    } // namespace ai
} // namespace tmms