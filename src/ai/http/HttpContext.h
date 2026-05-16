#pragma once

#include "HttpRequest.h"
#include "network/base/MsgBuffer.h"
#include <string>

namespace tmms
{
    namespace ai
    {

        class HttpContext
        {
        public:
            enum class ParseState
            {
                kExpectRequestLine,
                kExpectHeaders,
                kExpectBody,
                kGotAll
            };

            HttpContext();

            bool ParseRequest(network::MsgBuffer &buf);
            bool GotAll() const;
            const HttpRequest &Request() const;
            void Reset();

        private:
            bool ParseRequestLine(const std::string &line);
            bool ParseHeaderLine(const std::string &line);
            static std::string Trim(const std::string &s);

        private:
            ParseState state_;
            HttpRequest request_;
            int content_length_;
            std::string raw_;
        };

    }
}