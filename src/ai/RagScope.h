#pragma once
#include <string>

namespace tmms
{
    namespace ai
    {
        inline constexpr const char *SCOPE_GLOBAL       = "global";
        inline constexpr const char *SCOPE_PROJECT      = "project";
        inline constexpr const char *SCOPE_CONVERSATION = "conversation";

        inline bool IsValidScopeType(const std::string &scope_type)
        {
            return scope_type == SCOPE_GLOBAL
                || scope_type == SCOPE_PROJECT
                || scope_type == SCOPE_CONVERSATION;
        }

    } // namespace ai
} // namespace tmms