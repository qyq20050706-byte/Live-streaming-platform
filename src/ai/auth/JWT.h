#pragma once
#include <string>
#include <cstdint>
#include <json/json.h>

namespace tmms
{
    namespace ai
    {
        struct JWTConfig
        {
            std::string secret;
            int64_t     expire_seconds{86400};
        };

        class JWT
        {
        public:
            // 加载配置
            static bool LoadConfig(const std::string &config_path,
                                   JWTConfig &cfg,
                                   std::string &err);

            // 签发 token
            // payload 里放 user_id、username
            static std::string Sign(uint64_t user_id,
                                    const std::string &username,
                                    const JWTConfig &cfg);

            // 验证 token，成功返回 true，并填充 user_id 和 username
            static bool Verify(const std::string &token,
                                const JWTConfig &cfg,
                                uint64_t &user_id,
                                std::string &username,
                                std::string &err);

        private:
            static std::string Base64UrlEncode(const unsigned char *data,
                                               size_t len);
            static bool Base64UrlDecode(const std::string &encoded,
                                        std::vector<unsigned char> &out);

            static std::string HmacSha256(const std::string &data,
                                          const std::string &secret);
        };

    } // namespace ai
} // namespace tmms