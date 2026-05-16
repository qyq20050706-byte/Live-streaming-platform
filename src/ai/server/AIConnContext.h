#pragma once

#include "ai/http/HttpContext.h"
#include <memory>

namespace tmms
{
    namespace ai
    {
        struct AIConnContext
        {
            HttpContext http_ctx;
            std::shared_ptr<bool> sse_alive;
        };

    } // namespace ai
} // namespace tmms