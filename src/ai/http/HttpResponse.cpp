#include "HttpResponse.h"
#include <sstream>

namespace tmms
{
    namespace ai
    {

        void HttpResponse::SetStatusCode(int code)
        {
            status_code_ = code;
        }

        void HttpResponse::SetStatusMessage(const std::string &msg)
        {
            status_message_ = msg;
        }

        void HttpResponse::SetHeader(const std::string &key, const std::string &value)
        {
            headers_[key] = value;
        }

        void HttpResponse::SetBody(const std::string &body)
        {
            body_ = body;
        }

        int HttpResponse::StatusCode() const
        {
            return status_code_;
        }

        const std::string &HttpResponse::StatusMessage() const
        {
            return status_message_;
        }

        const std::string &HttpResponse::Body() const
        {
            return body_;
        }

        void HttpResponse::Reset()
        {
            status_code_ = 200;
            status_message_ = "OK";
            headers_.clear();
            body_.clear();
        }

        std::string HttpResponse::ToString() const
        {
            std::ostringstream oss;

            oss << "HTTP/1.1 " << status_code_ << " " << status_message_ << "\r\n";

            bool has_content_length = false;
            bool has_content_type = false;

            for (std::map<std::string, std::string>::const_iterator it = headers_.begin();
                 it != headers_.end(); ++it)
            {
                oss << it->first << ": " << it->second << "\r\n";
                if (it->first == "Content-Length")
                {
                    has_content_length = true;
                }
                if (it->first == "Content-Type")
                {
                    has_content_type = true;
                }
            }

            if (!has_content_type)
            {
                oss << "Content-Type: text/plain\r\n";
            }

            if (!has_content_length)
            {
                oss << "Content-Length: " << body_.size() << "\r\n";
            }

            oss << "\r\n";
            oss << body_;

            return oss.str();
        }

        bool HttpResponse::HasHeader(const std::string &key) const
        {
            return headers_.find(key) != headers_.end();
        }
    }
}