#include "HttpContext.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace tmms
{
    namespace ai
    {
        HttpContext::HttpContext()
            : state_(ParseState::kExpectRequestLine),
              content_length_(0)
        {
        }

        bool HttpContext::ParseRequestLine(const std::string &line)
        {
            std::istringstream iss(line);
            std::string method;
            std::string path;
            std::string version;

            if (!(iss >> method >> path >> version))
            {
                return false;
            }

            request_.SetMethod(method);
            request_.SetPath(path);
            request_.SetVersion(version);
            return true;
        }

        bool HttpContext::ParseHeaderLine(const std::string &line)
        {
            size_t colon = line.find(':');
            if (colon == std::string::npos)
            {
                return false;
            }

            std::string key = Trim(line.substr(0, colon));
            std::string value = Trim(line.substr(colon + 1));

            request_.AddHeader(key, value);

            if (key == "Content-Length")
            {
                content_length_ = std::atoi(value.c_str());
                if (content_length_ < 0)
                {
                    content_length_ = 0;
                }
            }

            return true;
        }

        std::string HttpContext::Trim(const std::string &s)
        {
            size_t start = 0;
            while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
            {
                ++start;
            }

            size_t end = s.size();
            while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
            {
                --end;
            }

            return s.substr(start, end - start);
        }

        bool HttpContext::ParseRequest(network::MsgBuffer &buf)
        {
            while (true)
            {
                if (state_ == ParseState::kExpectRequestLine)
                {
                    const char *begin = buf.peek();
                    const char *end = begin + buf.readableBytes();
                    const char *crlf = std::search(begin, end, "\r\n", "\r\n" + 2);

                    if (crlf == end)
                    {
                        return true;
                    }

                    std::string line(begin, crlf);
                    if (!ParseRequestLine(line))
                    {
                        return false;
                    }

                    buf.retrieve(static_cast<size_t>(crlf - begin) + 2);
                    state_ = ParseState::kExpectHeaders;
                }
                else if (state_ == ParseState::kExpectHeaders)
                {
                    const char *begin = buf.peek();
                    const char *end = begin + buf.readableBytes();
                    const char *crlf = std::search(begin, end, "\r\n", "\r\n" + 2);

                    if (crlf == end)
                    {
                        return true;
                    }

                    if (crlf == begin)
                    {
                        // 空行，headers 结束
                        buf.retrieve(2);
                        if (content_length_ > 0)
                        {
                            state_ = ParseState::kExpectBody;
                        }
                        else
                        {
                            state_ = ParseState::kGotAll;
                        }
                        continue;
                    }

                    std::string line(begin, crlf);
                    if (!ParseHeaderLine(line))
                    {
                        return false;
                    }

                    buf.retrieve(static_cast<size_t>(crlf - begin) + 2);
                }
                else if (state_ == ParseState::kExpectBody)
                {
                    if (buf.readableBytes() < static_cast<size_t>(content_length_))
                    {
                        return true;
                    }

                    std::string body(buf.peek(), static_cast<size_t>(content_length_));
                    request_.SetBody(body);
                    buf.retrieve(static_cast<size_t>(content_length_));
                    state_ = ParseState::kGotAll;
                    return true;
                }
                else
                {
                    return true;
                }
            }
        }

        bool HttpContext::GotAll() const
        {
            return state_ == ParseState::kGotAll;
        }

        const HttpRequest &HttpContext::Request() const
        {
            return request_;
        }

        void HttpContext::Reset()
        {
            state_ = ParseState::kExpectRequestLine;
            content_length_ = 0;
            request_.Reset();
        }

    }
}