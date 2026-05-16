#include "HttpClient.h"
#include <curl/curl.h>

using namespace tmms::ai;

namespace tmms
{
    namespace ai
    {

        struct StreamContext
        {
            HttpClient::StreamCallback *cb;
        };

        static size_t WriteStreamCallback(void *contents, size_t size, size_t nmemb, void *userp)
        {
            StreamContext *ctx = static_cast<StreamContext *>(userp);
            if (!ctx || !ctx->cb)
            {
                return 0;
            }

            std::string chunk(static_cast<char *>(contents), size * nmemb);
            (*ctx->cb)(chunk);
            return size * nmemb;
        }

    }
}

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string *)userp)->append((char *)contents, size * nmemb);
    return size * nmemb;
}

tmms::ai::HttpClient::HttpClient()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

tmms::ai::HttpClient::~HttpClient()
{
    curl_global_cleanup();
}

bool tmms::ai::HttpClient::Post(const std::string &url, const std::string &body, const std::vector<std::string> &headers, long timeout_ms, std::string &response, long &http_code, std::string &err_msg)
{
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        err_msg = "curl init failed";
        return false;
    }

    struct curl_slist *header_list = nullptr;
    for (auto &h : headers)
    {
        header_list = curl_slist_append(header_list, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_NONE);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK)
    {
        err_msg = curl_easy_strerror(res);
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    return true;
}

bool tmms::ai::HttpClient::PostStream(const std::string &url, const std::string &body, const std::vector<std::string> &headers, long timeout_ms, StreamCallback callback, long &http_code, std::string &err_msg)
{
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        err_msg = "curl init failed";
        return false;
    }

    StreamContext ctx;
    ctx.cb = &callback;
    struct curl_slist *header_list = nullptr;
    for (auto &h : headers)
    {
        header_list = curl_slist_append(header_list, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_NONE);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStreamCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        err_msg = "curl_easy_perform failed: " + std::string(curl_easy_strerror(res));
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    return true;
}
