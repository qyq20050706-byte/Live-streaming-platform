#pragma once

#include <map>
#include <string>

namespace tmms
{
    namespace ai
    {

        class HttpRequest
        {
        public:
            void SetMethod(const std::string &method);
            void SetPath(const std::string &path);
            void SetVersion(const std::string &version);
            void AddHeader(const std::string &key, const std::string &value);
            void SetBody(const std::string &body);

            const std::string &Method() const;
            const std::string &Path() const;
            const std::string &Version() const;
            const std::string &Body() const;

            std::string GetHeader(const std::string &key) const;
            void Reset();

        private:
            std::string method_;
            std::string path_;
            std::string version_;
            std::map<std::string, std::string> headers_;
            std::string body_;
        };

    }
}