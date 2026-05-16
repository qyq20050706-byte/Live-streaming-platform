#include "HttpRequest.h"

namespace tmms
{
    namespace ai
    {

        void HttpRequest::SetMethod(const std::string &method)
        {
            method_ = method;
        }

        void HttpRequest::SetPath(const std::string &path)
        {
            path_ = path;
        }

        void HttpRequest::SetVersion(const std::string &version)
        {
            version_ = version;
        }

        void HttpRequest::AddHeader(const std::string &key, const std::string &value)
        {
            headers_[key] = value;
        }

        void HttpRequest::SetBody(const std::string &body)
        {
            body_ = body;
        }

        const std::string &HttpRequest::Method() const
        {
            return method_;
        }

        const std::string &HttpRequest::Path() const
        {
            return path_;
        }

        const std::string &HttpRequest::Version() const
        {
            return version_;
        }

        const std::string &HttpRequest::Body() const
        {
            return body_;
        }

        std::string HttpRequest::GetHeader(const std::string &key) const
        {
            std::map<std::string, std::string>::const_iterator it = headers_.find(key);
            if (it == headers_.end())
            {
                return "";
            }
            return it->second;
        }

        void HttpRequest::Reset()
        {
            method_.clear();
            path_.clear();
            version_.clear();
            headers_.clear();
            body_.clear();
        }

    }
}