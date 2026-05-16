#pragma once
#include <string>
#include <vector>
#include <json/json.h>

namespace tmms
{
    namespace ai
    {
        class PromptTemplate
        {
        public:
            static std::string BuildSystemPrompt(
                const std::vector<std::string> &contexts);

            static Json::Value BuildRAGMessages(
                const std::vector<std::string> &contexts,
                const std::string &user_query);
        };

    } // namespace ai
} // namespace tmms