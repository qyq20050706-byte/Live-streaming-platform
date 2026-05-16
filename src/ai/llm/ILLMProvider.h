#pragma once
#include <string>
#include <vector>
#include <functional>
#include <json/json.h>

namespace tmms
{
    namespace ai
    {
        using StreamCallback = std::function<void(const std::string &delta)>;

        class ILLMProvider
        {
        public:
            virtual ~ILLMProvider() = default;

            // 单轮问答（原有，RAG 继续用这个）
            virtual bool Chat(const std::string &prompt,
                              std::string &answer) = 0;

            virtual bool ChatStream(const std::string &prompt,
                                    StreamCallback callback,
                                    std::string &err_msg) = 0;

            // 多轮问答（新增，多轮对话用这个）
            // messages 格式：[{"role":"user","content":"..."}, ...]
            virtual bool ChatWithMessages(const Json::Value &messages,
                                          std::string &answer) = 0;

            virtual bool ChatStreamWithMessages(const Json::Value &messages,
                                                StreamCallback callback,
                                                std::string &err_msg) = 0;
        };
    }
}