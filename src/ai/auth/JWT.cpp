#include "JWT.h"
#include "base/Config.h"
#include "base/LogStream.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/core_names.h>
#include <openssl/params.h>

#include <vector>
#include <sstream>
#include <chrono>
#include <cstring>

namespace tmms
{
    namespace ai
    {
        // ============================================================
        // Base64URL 编码（JWT 标准）
        // ============================================================
        std::string JWT::Base64UrlEncode(const unsigned char *data, size_t len)
        {
            static const char table[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
            std::string out;
            out.reserve(((len + 2) / 3) * 4);

            for (size_t i = 0; i < len; i += 3)
            {
                unsigned char b0 = data[i];
                unsigned char b1 = (i + 1 < len) ? data[i + 1] : 0;
                unsigned char b2 = (i + 2 < len) ? data[i + 2] : 0;

                out += table[(b0 >> 2) & 0x3F];
                out += table[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)];
                if (i + 1 < len)
                    out += table[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)];
                if (i + 2 < len)
                    out += table[b2 & 0x3F];
            }
            // JWT 不加 '=' 填充
            return out;
        }

        // ============================================================
        // Base64URL 解码
        // ============================================================
        bool JWT::Base64UrlDecode(const std::string &encoded,
                                   std::vector<unsigned char> &out)
        {
            auto decode_char = [](char c) -> int {
                if (c >= 'A' && c <= 'Z') return c - 'A';
                if (c >= 'a' && c <= 'z') return c - 'a' + 26;
                if (c >= '0' && c <= '9') return c - '0' + 52;
                if (c == '-') return 62;
                if (c == '_') return 63;
                return -1;
            };

            out.clear();
            // 补齐 '='
            std::string padded = encoded;
            while (padded.size() % 4 != 0) padded += '=';

            for (size_t i = 0; i < padded.size(); i += 4)
            {
                int b0 = decode_char(padded[i]);
                int b1 = (i + 1 < padded.size()) ? decode_char(padded[i + 1]) : 0;
                int b2 = (i + 2 < padded.size() && padded[i + 2] != '=')
                             ? decode_char(padded[i + 2]) : 0;
                int b3 = (i + 3 < padded.size() && padded[i + 3] != '=')
                             ? decode_char(padded[i + 3]) : 0;

                if (b0 < 0 || b1 < 0) return false;
                out.push_back((unsigned char)((b0 << 2) | (b1 >> 4)));
                if (i + 2 < padded.size() && padded[i + 2] != '=')
                    out.push_back((unsigned char)(((b1 & 0x0F) << 4) | (b2 >> 2)));
                if (i + 3 < padded.size() && padded[i + 3] != '=')
                    out.push_back((unsigned char)(((b2 & 0x03) << 6) | b3));
            }
            return true;
        }

        // ============================================================
        // HMAC-SHA256（OpenSSL 3 EVP_MAC 接口）
        // ============================================================
        std::string JWT::HmacSha256(const std::string &data,
                                     const std::string &secret)
        {
            EVP_MAC *mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
            if (!mac) return "";

            EVP_MAC_CTX *ctx = EVP_MAC_CTX_new(mac);
            EVP_MAC_free(mac);
            if (!ctx) return "";

            OSSL_PARAM params[] = {
                OSSL_PARAM_construct_utf8_string(
                    OSSL_MAC_PARAM_DIGEST,
                    (char *)"SHA256",
                    0),
                OSSL_PARAM_construct_end()
            };

            if (EVP_MAC_init(ctx,
                             (const unsigned char *)secret.c_str(),
                             secret.size(),
                             params) != 1)
            {
                EVP_MAC_CTX_free(ctx);
                return "";
            }

            if (EVP_MAC_update(ctx,
                               (const unsigned char *)data.c_str(),
                               data.size()) != 1)
            {
                EVP_MAC_CTX_free(ctx);
                return "";
            }

            unsigned char out[32];
            size_t out_len = sizeof(out);
            if (EVP_MAC_final(ctx, out, &out_len, sizeof(out)) != 1)
            {
                EVP_MAC_CTX_free(ctx);
                return "";
            }

            EVP_MAC_CTX_free(ctx);
            return std::string((char *)out, out_len);
        }

        // ============================================================
        // LoadConfig
        // ============================================================
        bool JWT::LoadConfig(const std::string &config_path,
                              JWTConfig &cfg,
                              std::string &err)
        {
            Json::Value root;
            if (!tmms::base::Config::LoadFile(config_path, root))
            {
                err = "failed to load auth config: " + config_path;
                return false;
            }

            cfg.secret         = root.get("jwt_secret", "").asString();
            cfg.expire_seconds = root.get("jwt_expire_seconds", 86400).asInt64();

            if (cfg.secret.empty())
            {
                err = "jwt_secret is empty";
                return false;
            }

            LOG_INFO << "JWT config loaded. expire_seconds=" << cfg.expire_seconds;
            return true;
        }

        // ============================================================
        // Sign
        // ============================================================
        std::string JWT::Sign(uint64_t user_id,
                               const std::string &username,
                               const JWTConfig &cfg)
        {
            // Header
            std::string header_json = R"({"alg":"HS256","typ":"JWT"})";
            std::string header_b64 = Base64UrlEncode(
                (const unsigned char *)header_json.c_str(),
                header_json.size());

            // Payload
            int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            int64_t exp = now + cfg.expire_seconds;

            Json::Value payload;
            payload["user_id"]  = static_cast<Json::UInt64>(user_id);
            payload["username"] = username;
            payload["iat"]      = static_cast<Json::Int64>(now);
            payload["exp"]      = static_cast<Json::Int64>(exp);

            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            std::string payload_json = Json::writeString(writer, payload);

            std::string payload_b64 = Base64UrlEncode(
                (const unsigned char *)payload_json.c_str(),
                payload_json.size());

            // Signing input
            std::string signing_input = header_b64 + "." + payload_b64;

            // Signature
            std::string sig_raw = HmacSha256(signing_input, cfg.secret);
            if (sig_raw.empty()) return "";

            std::string sig_b64 = Base64UrlEncode(
                (const unsigned char *)sig_raw.c_str(),
                sig_raw.size());

            return signing_input + "." + sig_b64;
        }

        // ============================================================
        // Verify
        // ============================================================
        bool JWT::Verify(const std::string &token,
                          const JWTConfig &cfg,
                          uint64_t &user_id,
                          std::string &username,
                          std::string &err)
        {
            // 分割三段
            size_t p1 = token.find('.');
            size_t p2 = token.find('.', p1 + 1);

            if (p1 == std::string::npos || p2 == std::string::npos)
            {
                err = "invalid token format";
                return false;
            }

            std::string header_b64  = token.substr(0, p1);
            std::string payload_b64 = token.substr(p1 + 1, p2 - p1 - 1);
            std::string sig_b64     = token.substr(p2 + 1);

            // 验签
            std::string signing_input = header_b64 + "." + payload_b64;
            std::string expected_sig  = HmacSha256(signing_input, cfg.secret);
            if (expected_sig.empty())
            {
                err = "HMAC compute failed";
                return false;
            }

            std::string expected_b64 = Base64UrlEncode(
                (const unsigned char *)expected_sig.c_str(),
                expected_sig.size());

            if (sig_b64 != expected_b64)
            {
                err = "signature mismatch";
                return false;
            }

            // 解码 payload
            std::vector<unsigned char> payload_bytes;
            if (!Base64UrlDecode(payload_b64, payload_bytes))
            {
                err = "payload decode failed";
                return false;
            }

            std::string payload_json(
                (char *)payload_bytes.data(), payload_bytes.size());

            Json::Value payload;
            Json::CharReaderBuilder rb;
            std::string parse_err;
            std::istringstream iss(payload_json);
            if (!Json::parseFromStream(rb, iss, &payload, &parse_err))
            {
                err = "payload parse failed: " + parse_err;
                return false;
            }

            // 验证过期时间
            int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            int64_t exp = payload.get("exp", 0).asInt64();
            if (now > exp)
            {
                err = "token expired";
                return false;
            }

            user_id  = payload.get("user_id", 0).asUInt64();
            username = payload.get("username", "").asString();
            return true;
        }

    } // namespace ai
} // namespace tmms