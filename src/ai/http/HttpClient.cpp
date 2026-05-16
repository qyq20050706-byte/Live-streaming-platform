#include "HttpClient.h"
#include <curl/curl.h>
#include <chrono>

using namespace tmms::ai;

struct ProgressContext;

namespace tmms
{
    namespace ai
    {

        struct StreamContext
        {
            HttpClient::StreamCallback *cb;
            ProgressContext *progress_ctx{nullptr};
        };

        static size_t WriteStreamCallback(void *contents, size_t size, size_t nmemb, void *userp)
        {
            StreamContext *ctx = static_cast<StreamContext *>(userp);
            if (!ctx || !ctx->cb)
            {
                return 0;
            }

            // 标记首包已到达
            if (ctx->progress_ctx)
            {
                ctx->progress_ctx->first_token_received = true;
            }

            std::string chunk(static_cast<char *>(contents), size * nmemb);
            (*ctx->cb)(chunk);
            return size * nmemb;
        }
    }
}

struct ProgressContext
{
    std::chrono::steady_clock::time_point start_time;
    long first_token_timeout_ms;
    bool first_token_received{false};
};

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
// ==========================================================
// 进度回调上下文（用于首包超时检测）
// ==========================================================

// CURLOPT_XFERINFOFUNCTION 回调
static int ProgressCallback(void *clientp,
                            curl_off_t dltotal,
                            curl_off_t dlnow,
                            curl_off_t ultotal,
                            curl_off_t ulnow)
{
    ProgressContext *ctx = static_cast<ProgressContext *>(clientp);
    if (!ctx)
        return 0;

    // 如果已经收到过数据，不检查首包超时
    if (ctx->first_token_received)
        return 0;

    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - ctx->start_time)
                          .count();
    if (elapsed_ms > ctx->first_token_timeout_ms)
    {
        // 返回非0让 curl 中止请求
        return 1;
    }
    return 0;
}

// 流式回调上下文扩展（携带进度上下文指针）
struct StreamContextEx : public tmms::ai::StreamContext
{
    ProgressContext *progress_ctx{nullptr};
};

// CURLOPT_XFERINFOFUNCTION 回调
static int ProgressCallback(void *clientp,
                            curl_off_t dltotal,
                            curl_off_t dlnow,
                            curl_off_t ultotal,
                            curl_off_t ulnow)
{
    ProgressContext *ctx = static_cast<ProgressContext *>(clientp);
    if (!ctx) return 0;

    if (ctx->first_token_received) return 0;

    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - ctx->start_time).count();
    if (elapsed_ms > ctx->first_token_timeout_ms)
    {
        return 1; // 中止请求
    }
    return 0;
}

bool tmms::ai::HttpClient::PostStream(
    const std::string &url,
    const std::string &body,
    const std::vector<std::string> &headers,
    long connect_timeout_ms,
    long first_token_timeout_ms,
    long idle_stream_timeout_ms,
    StreamCallback callback,
    long &http_code,
    std::string &err_msg)
{
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        err_msg = "curl init failed";
        return false;
    }

    ProgressContext prog_ctx;
    prog_ctx.start_time = std::chrono::steady_clock::now();
    prog_ctx.first_token_timeout_ms = first_token_timeout_ms;

    StreamContext ctx;
    ctx.cb = &callback;
    ctx.progress_ctx = &prog_ctx;

    struct curl_slist *header_list = nullptr;
    for (auto &h : headers)
    {
        header_list = curl_slist_append(header_list, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_NONE);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    // 三维超时设置
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, idle_stream_timeout_ms / 1000);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog_ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStreamCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

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