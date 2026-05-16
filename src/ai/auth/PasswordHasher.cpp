#include "PasswordHasher.h"
#include "base/LogStream.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#include <openssl/params.h>

#include <vector>
#include <sstream>
#include <cstring>

namespace tmms
{
    namespace ai
    {
        // ============================================================
        // Base64 编码（使用 OpenSSL EVP_EncodeBlock）
        // ============================================================
        std::string PasswordHasher::Base64Encode(const unsigned char *data,
                                                  size_t len)
        {
            // EVP_EncodeBlock 输出标准 Base64，不加换行
            size_t out_len = 4 * ((len + 2) / 3);
            std::vector<unsigned char> buf(out_len + 1, 0);
            int real_len = EVP_EncodeBlock(buf.data(), data, (int)len);
            return std::string((char *)buf.data(), real_len);
        }

        // ============================================================
        // Base64 解码（使用 OpenSSL EVP_DecodeBlock）
        // ============================================================
        bool PasswordHasher::Base64Decode(const std::string &encoded,
                                           std::vector<unsigned char> &out)
        {
            // 补齐 '=' 到 4 的倍数
            std::string padded = encoded;
            while (padded.size() % 4 != 0)
                padded += '=';

            size_t out_max = padded.size() / 4 * 3;
            out.resize(out_max, 0);

            int real_len = EVP_DecodeBlock(out.data(),
                                           (const unsigned char *)padded.c_str(),
                                           (int)padded.size());
            if (real_len < 0)
                return false;

            // 处理末尾 '=' 填充导致的多余字节
            size_t pad_count = 0;
            for (int i = (int)padded.size() - 1;
                 i >= 0 && padded[i] == '=';
                 --i)
            {
                ++pad_count;
            }

            out.resize((size_t)real_len - pad_count);
            return true;
        }

        // ============================================================
        // 生成随机 salt（16 字节，转 Base64）
        // ============================================================
        std::string PasswordHasher::GenerateSalt()
        {
            unsigned char buf[16];
            RAND_bytes(buf, sizeof(buf));
            return Base64Encode(buf, sizeof(buf));
        }

        // ============================================================
        // PBKDF2（OpenSSL 3 EVP_KDF 接口）
        // ============================================================
        bool PasswordHasher::DoPBKDF2(const std::string &password,
                                       const unsigned char *salt,
                                       size_t salt_len,
                                       int iterations,
                                       unsigned char *out,
                                       size_t out_len)
        {
            EVP_KDF *kdf = EVP_KDF_fetch(nullptr, "PBKDF2", nullptr);
            if (!kdf)
            {
                LOG_ERROR << "PasswordHasher: EVP_KDF_fetch PBKDF2 failed";
                return false;
            }

            EVP_KDF_CTX *ctx = EVP_KDF_CTX_new(kdf);
            EVP_KDF_free(kdf);

            if (!ctx)
            {
                LOG_ERROR << "PasswordHasher: EVP_KDF_CTX_new failed";
                return false;
            }

            int iter_param = iterations;

            OSSL_PARAM params[] = {
                OSSL_PARAM_construct_octet_string(
                    OSSL_KDF_PARAM_PASSWORD,
                    (void *)password.c_str(),
                    password.size()),
                OSSL_PARAM_construct_octet_string(
                    OSSL_KDF_PARAM_SALT,
                    (void *)salt,
                    salt_len),
                OSSL_PARAM_construct_int(
                    OSSL_KDF_PARAM_ITER,
                    &iter_param),
                OSSL_PARAM_construct_utf8_string(
                    OSSL_KDF_PARAM_DIGEST,
                    (char *)"SHA256",
                    0),
                OSSL_PARAM_construct_end()
            };

            bool ok = (EVP_KDF_derive(ctx, out, out_len, params) == 1);
            EVP_KDF_CTX_free(ctx);

            if (!ok)
                LOG_ERROR << "PasswordHasher: EVP_KDF_derive failed";

            return ok;
        }

        // ============================================================
        // Hash：生成 PHC 格式
        // ============================================================
        std::string PasswordHasher::Hash(const std::string &password,
                                          int iterations)
        {
            std::string salt_b64 = GenerateSalt();

            std::vector<unsigned char> salt_bytes;
            if (!Base64Decode(salt_b64, salt_bytes))
            {
                LOG_ERROR << "PasswordHasher::Hash Base64Decode salt failed";
                return "";
            }

            unsigned char hash[32];
            if (!DoPBKDF2(password,
                          salt_bytes.data(), salt_bytes.size(),
                          iterations,
                          hash, sizeof(hash)))
            {
                return "";
            }

            std::string hash_b64 = Base64Encode(hash, sizeof(hash));

            std::ostringstream oss;
            oss << "$pbkdf2-sha256$i=" << iterations
                << "$" << salt_b64
                << "$" << hash_b64;

            return oss.str();
        }

        // ============================================================
        // Verify：验证密码
        // ============================================================
        bool PasswordHasher::Verify(const std::string &password,
                                     const std::string &phc_string)
        {
            if (phc_string.size() < 15 ||
                phc_string.substr(0, 15) != "$pbkdf2-sha256$")
            {
                LOG_WARN << "PasswordHasher::Verify: unsupported format";
                return false;
            }

            size_t p1 = phc_string.find('$', 1);
            size_t p2 = phc_string.find('$', p1 + 1);
            size_t p3 = phc_string.find('$', p2 + 1);

            if (p1 == std::string::npos ||
                p2 == std::string::npos ||
                p3 == std::string::npos)
            {
                LOG_WARN << "PasswordHasher::Verify: malformed PHC string";
                return false;
            }

            std::string params_str = phc_string.substr(p1 + 1, p2 - p1 - 1);
            int iterations = 100000;
            if (params_str.size() > 2 && params_str.substr(0, 2) == "i=")
            {
                try { iterations = std::stoi(params_str.substr(2)); }
                catch (...) { return false; }
            }

            std::string salt_b64 = phc_string.substr(p2 + 1, p3 - p2 - 1);
            std::string hash_b64 = phc_string.substr(p3 + 1);

            std::vector<unsigned char> salt_bytes;
            if (!Base64Decode(salt_b64, salt_bytes))
                return false;

            unsigned char computed[32];
            if (!DoPBKDF2(password,
                          salt_bytes.data(), salt_bytes.size(),
                          iterations,
                          computed, sizeof(computed)))
            {
                return false;
            }

            std::vector<unsigned char> stored_hash;
            if (!Base64Decode(hash_b64, stored_hash))
                return false;

            if (stored_hash.size() != sizeof(computed))
                return false;

            return (CRYPTO_memcmp(computed,
                                  stored_hash.data(),
                                  sizeof(computed)) == 0);
        }

    } // namespace ai
} // namespace tmms