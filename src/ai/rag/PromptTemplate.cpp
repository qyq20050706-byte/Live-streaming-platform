#include "PromptTemplate.h"
#include <sstream>

namespace tmms
{
    namespace ai
    {
        std::string PromptTemplate::BuildSystemPrompt(
            const std::vector<std::string> &contexts)
        {
            std::ostringstream oss;
            oss << "你是一个专业的智能助手。";
            oss << "请严格根据以下提供的知识片段回答用户问题。\n";
            oss << "如果知识片段中没有足够信息，请明确告知用户，不要编造答案。\n\n";
            oss << "知识片段：\n";

            for (size_t i = 0; i < contexts.size(); ++i)
            {
                oss << "---片段" << (i + 1) << "---\n";
                oss << contexts[i] << "\n";
            }

            oss << "---\n";
            oss << "请基于以上知识片段回答用户的问题。";
            return oss.str();
        }

        Json::Value PromptTemplate::BuildRAGMessages(
            const std::vector<std::string> &contexts,
            const std::string &user_query)
        {
            Json::Value messages(Json::arrayValue);

            Json::Value system_msg;
            system_msg["role"]    = "system";
            system_msg["content"] = BuildSystemPrompt(contexts);
            messages.append(system_msg);

            Json::Value user_msg;
            user_msg["role"]    = "user";
            user_msg["content"] = user_query;
            messages.append(user_msg);

            return messages;
        }

    } // namespace ai
} // namespace tmms