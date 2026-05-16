#pragma once

#include <map>
#include <string>

namespace tmms
{
    namespace ai
    {

        class HttpResponse
        {
        public:
            void SetStatusCode(int code);
            void SetStatusMessage(const std::string &msg);
            void SetHeader(const std::string &key, const std::string &value);
            void SetBody(const std::string &body);

            int StatusCode() const;
            const std::string &StatusMessage() const;
            const std::string &Body() const;

            void Reset();
            std::string ToString() const;

            bool HasHeader(const std::string &key) const;

        private:
            int status_code_ = 200;
            std::string status_message_ = "OK";
            std::map<std::string, std::string> headers_;
            std::string body_;
        };

    }
}