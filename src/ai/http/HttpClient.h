#pragma once
#include <string>
#include <vector>
#include <functional>

namespace tmms
{
    namespace ai
    {
        class HttpClient
        {
        public:
            using StreamCallback = std::function<void(const std::string &chunk)>;

            HttpClient();
            ~HttpClient();

            bool Post(const std::string &url,
                      const std::string &body,
                      const std::vector<std::string> &headers,
                      long timeout_ms,
                      std::string &response,
                      long &http_code,
                      std::string &err_msg);

            bool PostStream(const std::string &url,
                            const std::string &body,
                            const std::vector<std::string> &headers,
                            long timeout_ms,
                            StreamCallback callback,
                            long &http_code,
                            std::string &err_msg);

            bool PostStream(const std::string &url,
                            const std::string &body,
                            const std::vector<std::string> &headers,
                            long connect_timeout_ms,
                            long first_token_timeout_ms,
                            long idle_stream_timeout_ms,
                            StreamCallback callback,
                            long &http_code,
                            std::string &err_msg);
        };
    }
}