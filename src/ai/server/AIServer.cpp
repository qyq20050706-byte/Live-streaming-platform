#include "AIServer.h"
#include "network/base/InetAddress.h"
#include "network/net/Connection.h"
#include "base/LogStream.h"
#include "base/Config.h"
#include <sstream>
#include <chrono>
#include "ai/auth/JWT.h"

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

namespace
{
    // 按 UTF-8 字符数截断，避免切断多字节字符
    std::string TruncateUtf8(const std::string &s, size_t max_chars)
    {
        size_t i = 0;
        size_t chars = 0;

        while (i < s.size() && chars < max_chars)
        {
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t len = 1;

            if ((c & 0x80) == 0x00)
                len = 1; // 1-byte
            else if ((c & 0xE0) == 0xC0)
                len = 2; // 2-byte
            else if ((c & 0xF0) == 0xE0)
                len = 3; // 3-byte
            else if ((c & 0xF8) == 0xF0)
                len = 4; // 4-byte
            else
                break; // 非法 UTF-8，直接停止

            if (i + len > s.size())
                break; // 不完整 UTF-8，停止

            i += len;
            ++chars;
        }

        std::string out = s.substr(0, i);
        if (i < s.size())
            out += "...";
        return out;
    }
}

namespace tmms
{
    namespace ai
    {
        // =============================================================
        // 工具函数
        // =============================================================
        std::string AIServer::StatusText(int code)
        {
            switch (code)
            {
            case 200:
                return "OK";
            case 400:
                return "Bad Request";
            case 404:
                return "Not Found";
            case 405:
                return "Method Not Allowed";
            case 429:
                return "Too Many Requests";
            case 500:
                return "Internal Server Error";
            case 503:
                return "Service Unavailable";
            default:
                return "Unknown";
            }
        }

        AIConnContext *AIServer::EnsureConnCtx(
            const network::TcpConnectionPtr &conn)
        {
            auto ctx_ptr = conn->GetContext<AIConnContext>(
                network::kHttpContext);
            if (!ctx_ptr)
            {
                ctx_ptr = std::make_shared<AIConnContext>();
                conn->SetContext(network::kHttpContext,
                                 std::static_pointer_cast<void>(ctx_ptr));
            }
            return ctx_ptr.get();
        }

        void AIServer::PrepareShortResponse(HttpResponse &resp)
        {
            if (!resp.HasHeader("Access-Control-Allow-Origin"))
                resp.SetHeader("Access-Control-Allow-Origin", "*");
            if (!resp.HasHeader("Connection"))
                resp.SetHeader("Connection", "close");
        }

        // =============================================================
        // 构造 / 析构
        // =============================================================
        AIServer::AIServer(network::EventLoop *loop,
                           const Json::Value &server_cfg,
                           const base::AIConfigInfoPtr &ai_cfg)
            : loop_(loop),
              server_(loop,
                      network::InetAddress(
                          "0.0.0.0",
                          static_cast<uint16_t>(server_cfg["port"].asUInt()))),
              llm_(ai_cfg),
              port_(static_cast<uint16_t>(server_cfg["port"].asUInt())),
              io_thread_num_(server_cfg.get("io_thread_num", 0).asInt()),
              idle_timeout_s_(server_cfg["idle_timeout_s"].asInt()),
              sse_heartbeat_interval_s_(
                  server_cfg["sse_heartbeat_interval_s"].asInt()),
              max_connections_(
                  server_cfg.get("max_connections", 2000).asInt())
        {
            int worker_num = server_cfg.get("worker_thread_num", 4).asInt();
            if (worker_num <= 0)
                worker_num = 2;
            thread_pool_.Start(worker_num);
            server_.SetThreadNum(io_thread_num_);

            LOG_INFO << "AIServer constructed."
                     << " port=" << port_
                     << " io_threads=" << io_thread_num_
                     << " worker_threads=" << worker_num
                     << " max_connections=" << max_connections_
                     << " idle_timeout_s=" << idle_timeout_s_
                     << " sse_heartbeat_s=" << sse_heartbeat_interval_s_;

            // 初始化 RAG
            std::string embedding_cfg =
                std::string(PROJECT_ROOT) + "/bin/config/ai/embedding.json";
            std::string rag_cfg =
                std::string(PROJECT_ROOT) + "/bin/config/ai/rag.json";
            std::string rag_err;
            if (!InitRAG(embedding_cfg, rag_cfg, rag_err))
            {
                LOG_WARN << "RAG init failed: " << rag_err
                         << " (RAG routes disabled)";
            }
            else
            {
                LOG_INFO << "RAG initialized successfully.";
            }

            std::string db_cfg =
                std::string(PROJECT_ROOT) + "/bin/config/ai/db.json";
            std::string auth_cfg =
                std::string(PROJECT_ROOT) + "/bin/config/ai/auth.json";
            std::string db_err;
            if (!InitDB(db_cfg, auth_cfg, db_err))
            {
                LOG_WARN << "DB/Auth init failed: " << db_err
                         << " (user routes disabled)";
            }
            else
            {
                LOG_INFO << "DB and Auth initialized successfully.";
            }
        }

        AIServer::~AIServer()
        {
            Stop();
        }

        bool AIServer::InitRAG(const std::string &embedding_cfg,
                               const std::string &rag_cfg,
                               std::string &err)
        {
            // 1. 初始化 Embedding
            if (!embedder_.Init(embedding_cfg, err))
                return false;

            // 2. 加载 RAG 配置
            Json::Value rag_root;
            if (!tmms::base::Config::LoadFile(rag_cfg, rag_root))
            {
                err = "cannot load rag config: " + rag_cfg;
                return false;
            }

            std::string db_path =
                rag_root.get("db_path", "bin/data/rag.db").asString();

            // 如果是相对路径，则拼到 PROJECT_ROOT 下
            std::filesystem::path db_fs_path(db_path);
            if (db_fs_path.is_relative())
            {
                db_fs_path = std::filesystem::path(PROJECT_ROOT) / db_fs_path;
            }
            db_path = db_fs_path.string();

            // 确保父目录存在
            std::filesystem::create_directories(db_fs_path.parent_path());
            int top_k = rag_root.get("top_k", 3).asInt();
            float min_sim = rag_root.get("min_similarity", 0.3f).asFloat();
            int chunk_size = rag_root.get("chunk_size", 300).asInt();
            int chunk_overlap = rag_root.get("chunk_overlap", 50).asInt();

            // 3. 初始化向量存储
            if (!vector_store_.Init(db_path, embedder_.Dimension(), err))
                return false;

            // 4. 创建 RAGService
            RAGConfig cfg;
            cfg.db_path = db_path;
            cfg.chunk_size = chunk_size;
            cfg.chunk_overlap = chunk_overlap;
            cfg.top_k = top_k;
            cfg.min_similarity = min_sim;

            rag_service_ = std::make_unique<RAGService>(
                &embedder_, &vector_store_, &llm_);
            rag_service_->SetConfig(cfg);

            return true;
        }

        void AIServer::HandleInternalChat(const network::TcpConnectionPtr &conn,
                                          const HttpRequest &req)
        {
            Json::Value body;
            std::string parse_err;
            if (!ParseJsonBody(req.Body(), body, parse_err))
            {
                ++failed_requests_;
                SendError(conn, 400, "invalid json: " + parse_err);
                return;
            }

            if (!body.isMember("query") || !body["query"].isString())
            {
                ++failed_requests_;
                SendError(conn, 400, "missing or invalid 'query' field");
                return;
            }

            std::string query = body["query"].asString();

            // 查询长度限制
            if (query.size() > 16000)
            {
                ++failed_requests_;
                SendError(conn, 413, "query too large (max 16000 chars), please upload to knowledge base");
                return;
            }

            uint64_t conv_id = 0;
            uint64_t user_id = 0;
            std::string username;
            bool has_auth = false;

            std::string token_err;
            if (ParseAuthToken(req, user_id, username, token_err))
            {
                has_auth = true;
                if (body.isMember("conversation_id") && body["conversation_id"].isUInt64())
                {
                    conv_id = body["conversation_id"].asUInt64();
                }
            }

            LOG_INFO << "InternalChat query_len=" << query.size()
                     << " has_auth=" << has_auth;

            std::weak_ptr<network::TcpConnection> weak_conn = conn;
            network::EventLoop *io_loop = conn->GetLoop();

            thread_pool_.AddTask(
                [this, weak_conn, io_loop, query, has_auth, user_id, conv_id]()
                {
                    auto start = std::chrono::steady_clock::now();

                    uint64_t actual_conv_id = conv_id;
                    std::string full_query_title = TruncateUtf8(query, 20);

                    if (has_auth)
                    {
                        std::string err;

                        if (actual_conv_id == 0 && conv_repo_)
                        {
                            actual_conv_id = conv_repo_->Create(
                                user_id, full_query_title, "chat", err);
                            if (actual_conv_id == 0)
                            {
                                LOG_WARN << "InternalChat: create conversation failed: " << err;
                                io_loop->RunInLoop([this, weak_conn, err]()
                                                   {
                        auto sp = weak_conn.lock(); if (!sp) return;
                        SendError(sp, 500, "create conversation failed: " + err); });
                                return;
                            }
                        }
                        else if (actual_conv_id != 0 && conv_repo_)
                        {
                            ConversationRecord conv;
                            if (!conv_repo_->FindById(actual_conv_id, conv, err) ||
                                conv.user_id != user_id)
                            {
                                LOG_WARN << "InternalChat: conversation access denied";
                                io_loop->RunInLoop([this, weak_conn]()
                                                   {
                        auto sp = weak_conn.lock(); if (!sp) return;
                        SendError(sp, 403, "conversation not found or access denied"); });
                                return;
                            }
                        }

                        if (actual_conv_id != 0 && msg_repo_)
                        {
                            msg_repo_->Insert(actual_conv_id, "user", query, err);
                        }
                    }

                    // 构建 messages
                    Json::Value messages(Json::arrayValue);

                    if (has_auth && actual_conv_id != 0 && msg_repo_)
                    {
                        std::string err;
                        std::vector<MessageRecord> history;
                        msg_repo_->ListRecent(actual_conv_id, 12, history, err);
                        for (auto &h : history)
                        {
                            Json::Value m;
                            m["role"] = h.role;
                            m["content"] = h.content;
                            messages.append(m);
                        }
                    }

                    Json::Value current_msg;
                    current_msg["role"] = "user";
                    current_msg["content"] = query;
                    messages.append(current_msg);

                    // 流式上游，服务端聚合
                    std::string answer;
                    std::string err_msg;
                    bool ok = llm_.ChatStreamWithMessages(
                        messages,
                        [&answer](const std::string &delta)
                        { answer += delta; },
                        err_msg);

                    double elapsed_ms = std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - start)
                                            .count();

                    if (ok && has_auth && actual_conv_id != 0 && msg_repo_)
                    {
                        std::string err;
                        msg_repo_->Insert(actual_conv_id, "assistant", answer, err);
                        if (conv_repo_)
                            conv_repo_->Touch(actual_conv_id, err);
                    }

                    io_loop->RunInLoop(
                        [this, weak_conn, ok, answer, err_msg, elapsed_ms, actual_conv_id]()
                        {
                            ++llm_calls_;
                            if (ok)
                            {
                                ++llm_success_;
                                std::lock_guard<std::mutex> lk(stats_mutex_);
                                total_llm_time_ms_ += elapsed_ms;
                            }
                            else
                            {
                                ++llm_failures_;
                            }

                            auto sp = weak_conn.lock();
                            if (!sp)
                                return;

                            HttpResponse resp;
                            if (ok)
                            {
                                Json::Value out;
                                out["code"] = 0;
                                out["message"] = "ok";
                                out["answer"] = answer;
                                if (actual_conv_id != 0)
                                    out["conversation_id"] = static_cast<Json::UInt64>(actual_conv_id);

                                Json::StreamWriterBuilder writer;
                                writer["indentation"] = "";
                                writer["emitUTF8"] = true;

                                resp.SetStatusCode(200);
                                resp.SetStatusMessage("OK");
                                resp.SetHeader("Content-Type", "application/json");
                                resp.SetBody(Json::writeString(writer, out));

                                LOG_INFO << "InternalChat success llm_ms=" << elapsed_ms;
                            }
                            else
                            {
                                resp.SetStatusCode(500);
                                resp.SetStatusMessage("Internal Server Error");
                                resp.SetHeader("Content-Type", "application/json");
                                resp.SetBody(BuildJsonResponse(500, "Internal Server Error", err_msg));
                                LOG_ERROR << "InternalChat failed: " << err_msg;
                            }

                            PrepareShortResponse(resp);
                            SendAndClose(sp, resp.ToString());
                        });
                });
        }

        // =============================================================
        // Start / Stop
        // =============================================================
        void AIServer::Start()
        {
            server_.SetNewConnectionCallback(
                [this](const network::TcpConnectionPtr &conn)
                { OnConnection(conn); });

            server_.SetDestoryConnectionCallback(
                [this](const network::TcpConnectionPtr &conn)
                { OnConnectionDestroy(conn); });

            server_.SetMessageCallback(
                [this](const network::TcpConnectionPtr &conn,
                       network::MsgBuffer &buf)
                { OnMessage(conn, buf); });

            server_.Start();
            StartStatsTimer();
            LOG_INFO << "AIServer started, listening on port " << port_;
        }

        void AIServer::Stop()
        {
            if (stopped_)
                return;
            stopped_ = true;
            thread_pool_.Stop();
            LOG_INFO << "AIServer stopped."
                     << " total_requests=" << total_requests_.load()
                     << " llm_calls=" << llm_calls_.load()
                     << " llm_success=" << llm_success_.load()
                     << " llm_failures=" << llm_failures_.load();
        }

        bool AIServer::InitDB(const std::string &db_cfg_path,
                              const std::string &auth_cfg_path,
                              std::string &err)
        {
            // 1. 加载数据库配置并连接
            if (!db_.LoadConfig(db_cfg_path, err))
                return false;

            if (!db_.Connect(err))
                return false;

            // 2. 加载 JWT 配置
            if (!JWT::LoadConfig(auth_cfg_path, jwt_cfg_, err))
                return false;

            // 3. 读取 pbkdf2 迭代次数
            Json::Value auth_root;
            int pbkdf2_iter = 100000;
            if (tmms::base::Config::LoadFile(auth_cfg_path, auth_root))
            {
                pbkdf2_iter = auth_root.get("pbkdf2_iterations", 100000).asInt();
            }

            // 4. 创建 AuthService
            auth_service_ = std::make_unique<AuthService>(
                &db_, jwt_cfg_, pbkdf2_iter);

            conv_repo_ = std::make_unique<ConversationRepo>(&db_);
            msg_repo_ = std::make_unique<MessageRepo>(&db_);
            project_repo_ = std::make_unique<ProjectRepo>(&db_);

            LOG_INFO << "ConversationRepo and MessageRepo initialized.";
            LOG_INFO << "ProjectRepo initialized.";

            return true;
        }

        // =============================================================
        // 统计日志
        // =============================================================
        void AIServer::StartStatsTimer()
        {
            loop_->RunEvery(60.0, [this]()
                            { LogStats(); });
            LOG_INFO << "Stats timer started (every 60s).";
        }

        void AIServer::LogStats()
        {
            double avg_llm_ms = 0.0;
            {
                std::lock_guard<std::mutex> lk(stats_mutex_);
                if (llm_success_.load() > 0)
                    avg_llm_ms = total_llm_time_ms_ / static_cast<double>(llm_success_.load());
            }
            double rate = 0.0;
            if (llm_calls_.load() > 0)
                rate = 100.0 * static_cast<double>(llm_success_.load()) / static_cast<double>(llm_calls_.load());

            int rag_count = rag_service_ ? rag_service_->Count() : -1;

            LOG_INFO << "[STATS]"
                     << " active_conn=" << active_connections_.load()
                     << " total_req=" << total_requests_.load()
                     << " chat=" << chat_requests_.load()
                     << " stream=" << stream_requests_.load()
                     << " failed=" << failed_requests_.load()
                     << " rejected=" << rejected_requests_.load()
                     << " pending_tasks=" << thread_pool_.PendingTasks()
                     << " llm_calls=" << llm_calls_.load()
                     << " llm_ok=" << llm_success_.load()
                     << " llm_fail=" << llm_failures_.load()
                     << " llm_success_rate=" << rate << "%"
                     << " avg_llm_ms=" << avg_llm_ms
                     << " rag_chunks=" << rag_count;
        }

        // =============================================================
        // /user/register
        // =============================================================
        void AIServer::HandleUserRegister(const network::TcpConnectionPtr &conn,
                                          const HttpRequest &req)
        {
            if (!auth_service_)
            {
                SendError(conn, 503, "auth service not initialized");
                return;
            }

            Json::Value body;
            std::string parse_err;
            if (!ParseJsonBody(req.Body(), body, parse_err))
            {
                ++failed_requests_;
                SendError(conn, 400, "invalid json: " + parse_err);
                return;
            }

            if (!body.isMember("username") || !body["username"].isString() ||
                !body.isMember("password") || !body["password"].isString())
            {
                ++failed_requests_;
                SendError(conn, 400, "missing username or password");
                return;
            }

            std::string username = body["username"].asString();
            std::string password = body["password"].asString();

            LOG_INFO << "HandleUserRegister username=" << username;

            // 注册是 IO 密集（需要做 PBKDF2 哈希 + 数据库写入），放到线程池
            std::weak_ptr<network::TcpConnection> weak_conn = conn;
            network::EventLoop *io_loop = conn->GetLoop();

            thread_pool_.AddTask([this, weak_conn, io_loop, username, password]()
                                 {
        auto result = auth_service_->Register(username, password);

        io_loop->RunInLoop([this, weak_conn, result]()
        {
            auto sp = weak_conn.lock();
            if (!sp) return;

            HttpResponse resp;
            if (result.ok)
            {
                Json::Value body_out;
                body_out["code"]    = 0;
                body_out["message"] = "ok";
                body_out["user_id"] = static_cast<Json::UInt64>(result.user_id);

                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                writer["emitUTF8"]    = true;

                resp.SetStatusCode(200);
                resp.SetStatusMessage("OK");
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(Json::writeString(writer, body_out));

                LOG_INFO << "Register success user_id=" << result.user_id;
            }
            else
            {
                int http_code = (result.code == 400) ? 400 : 500;
                resp.SetStatusCode(http_code);
                resp.SetStatusMessage(StatusText(http_code));
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(BuildJsonResponse(
                    result.code, result.message, ""));

                LOG_WARN << "Register failed: " << result.message;
            }

            PrepareShortResponse(resp);
            SendAndClose(sp, resp.ToString());
        }); });
        }

        // =============================================================
        // /user/login
        // =============================================================
        void AIServer::HandleUserLogin(const network::TcpConnectionPtr &conn,
                                       const HttpRequest &req)
        {
            if (!auth_service_)
            {
                SendError(conn, 503, "auth service not initialized");
                return;
            }

            Json::Value body;
            std::string parse_err;
            if (!ParseJsonBody(req.Body(), body, parse_err))
            {
                ++failed_requests_;
                SendError(conn, 400, "invalid json: " + parse_err);
                return;
            }

            if (!body.isMember("username") || !body["username"].isString() ||
                !body.isMember("password") || !body["password"].isString())
            {
                ++failed_requests_;
                SendError(conn, 400, "missing username or password");
                return;
            }

            std::string username = body["username"].asString();
            std::string password = body["password"].asString();

            LOG_INFO << "HandleUserLogin username=" << username;

            std::weak_ptr<network::TcpConnection> weak_conn = conn;
            network::EventLoop *io_loop = conn->GetLoop();

            thread_pool_.AddTask([this, weak_conn, io_loop, username, password]()
                                 {
        auto result = auth_service_->Login(username, password);

        io_loop->RunInLoop([this, weak_conn, result]()
        {
            auto sp = weak_conn.lock();
            if (!sp) return;

            HttpResponse resp;
            if (result.ok)
            {
                Json::Value body_out;
                body_out["code"]     = 0;
                body_out["message"]  = "ok";
                body_out["token"]    = result.token;
                body_out["user_id"]  = static_cast<Json::UInt64>(result.user_id);
                body_out["username"] = result.username;

                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                writer["emitUTF8"]    = true;

                resp.SetStatusCode(200);
                resp.SetStatusMessage("OK");
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(Json::writeString(writer, body_out));

                LOG_INFO << "Login success user_id=" << result.user_id;
            }
            else
            {
                int http_code = 400;
                if (result.code == 401) http_code = 401;
                if (result.code == 403) http_code = 403;
                if (result.code == 500) http_code = 500;

                resp.SetStatusCode(http_code);
                resp.SetStatusMessage(StatusText(http_code));
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(BuildJsonResponse(
                    result.code, result.message, ""));

                LOG_WARN << "Login failed: " << result.message;
            }

            PrepareShortResponse(resp);
            SendAndClose(sp, resp.ToString());
        }); });
        }

        // =============================================================
        // 连接回调
        // =============================================================
        void AIServer::OnConnection(const network::TcpConnectionPtr &conn)
        {
            if (!conn || !conn->IsConnected())
                return;

            int curr = ++active_connections_;
            if (curr > max_connections_)
            {
                --active_connections_;
                ++rejected_requests_;
                LOG_WARN << "Max connections reached active=" << curr;
                conn->ForceClose();
                return;
            }

            EnsureConnCtx(conn);

            LOG_INFO << "New connection"
                     << " peer=" << conn->PeerAddr().ToIpPort()
                     << " active=" << active_connections_.load();

            conn->EnableCheckIdleTimeout(idle_timeout_s_);
        }

        void AIServer::OnConnectionDestroy(
            const network::TcpConnectionPtr &conn)
        {
            if (active_connections_.load() > 0)
                --active_connections_;

            auto ctx = conn->GetContext<AIConnContext>(network::kHttpContext);
            if (ctx && ctx->sse_alive)
                *(ctx->sse_alive) = false;

            LOG_INFO << "Connection destroyed"
                     << " active=" << active_connections_.load();
        }

        // =============================================================
        // SendAndClose / ScheduleSSEHeartbeat
        // =============================================================
        void AIServer::SendAndClose(const network::TcpConnectionPtr &conn,
                                    const std::string &data)
        {
            conn->Send(data.c_str(), data.size());
            conn->CloseAfterWrite();
        }

        void AIServer::ScheduleSSEHeartbeat(
            network::EventLoop *io_loop,
            std::weak_ptr<network::TcpConnection> weak_conn,
            std::weak_ptr<bool> weak_alive)
        {
            io_loop->RunAfter(
                static_cast<double>(sse_heartbeat_interval_s_),
                [this, io_loop, weak_conn, weak_alive]()
                {
                    auto flag = weak_alive.lock();
                    if (!flag || !(*flag))
                        return;
                    auto sp = weak_conn.lock();
                    if (!sp)
                        return;
                    sp->Send(":heartbeat\n\n", 12);
                    ScheduleSSEHeartbeat(io_loop, weak_conn, weak_alive);
                });
        }

        // =============================================================
        // 消息回调
        // =============================================================
        void AIServer::OnMessage(const network::TcpConnectionPtr &conn,
                                 network::MsgBuffer &buf)
        {
            if (!conn)
                return;

            AIConnContext *ctx_ptr = EnsureConnCtx(conn);
            if (!ctx_ptr)
            {
                LOG_ERROR << "OnMessage: failed to ensure AIConnContext";
                conn->ForceClose();
                return;
            }

            HttpContext &http_ctx = ctx_ptr->http_ctx;

            if (!http_ctx.ParseRequest(buf))
            {
                LOG_WARN << "HTTP parse failed";
                ++failed_requests_;
                http_ctx.Reset();
                SendError(conn, 400, "parse http request failed");
                return;
            }

            if (!http_ctx.GotAll())
                return;

            HttpRequest req = http_ctx.Request();
            http_ctx.Reset();
            ++total_requests_;

            LOG_INFO << req.Method() << " " << req.Path()
                     << " from " << conn->PeerAddr().ToIpPort();

            if (req.Method() == "OPTIONS")
            {
                HandleOptions(conn);
                return;
            }

            if (req.Path() == "/healthz")
            {
                HandleHealth(conn);
                return;
            }

            // 其余接口只允许 POST
            if (req.Method() != "POST")
            {
                ++failed_requests_;
                SendError(conn, 405, "only POST and OPTIONS are supported");
                return;
            }

            if (req.Path() == "/user/register")
            {
                HandleUserRegister(conn, req);
                return;
            }

            if (req.Path() == "/user/login")
            {
                HandleUserLogin(conn, req);
                return;
            }

            if (req.Path() == "/conversation/create")
            {
                HandleConversationCreate(conn, req);
                return;
            }

            if (req.Path() == "/conversation/list")
            {
                HandleConversationList(conn, req);
                return;
            }

            if (req.Path() == "/project/create")
            {
                HandleProjectCreate(conn, req);
                return;
            }

            if (req.Path() == "/project/list")
            {
                HandleProjectList(conn, req);
                return;
            }

            if (req.Path() == "/project/delete")
            {
                HandleProjectDelete(conn, req);
                return;
            }

            if (req.Path() == "/message/list")
            {
                HandleMessageList(conn, req);
                return;
            }

            if (req.Path() == "/internal/chat")
            {
                HandleInternalChat(conn, req);
                return;
            }

            if (req.Path() == "/echo")
            {
                HandleEcho(conn, req);
                return;
            }

            if (req.Path() == "/mock_stream")
            {
                HandleMockStream(conn, req);
                return;
            }

            if (req.Path() == "/stream_chat")
            {
                ++stream_requests_;
                HandleStreamChat(conn, req);
                return;
            }

            if (req.Path() == "/rag/add")
            {
                HandleRagAdd(conn, req);
                return;
            }

            if (req.Path() == "/rag/stream_chat")
            {
                HandleRagStreamChat(conn, req);
                return;
            }

            if (req.Path() == "/rag/stats")
            {
                HandleRagStats(conn);
                return;
            }

            ++failed_requests_;
            SendError(conn, 404, "path not found: " + req.Path());
        }

        // =============================================================
        // OPTIONS
        // =============================================================
        void AIServer::HandleOptions(const network::TcpConnectionPtr &conn)
        {
            HttpResponse resp;
            resp.SetStatusCode(200);
            resp.SetStatusMessage("OK");
            resp.SetHeader("Access-Control-Allow-Origin", "*");
            resp.SetHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
            resp.SetHeader("Access-Control-Allow-Headers",
                           "Content-Type, Authorization");
            resp.SetHeader("Access-Control-Max-Age", "86400");
            resp.SetHeader("Content-Length", "0");
            resp.SetHeader("Vary", "Origin");

            PrepareShortResponse(resp);
            SendAndClose(conn, resp.ToString());
        }

        // =============================================================
        // /healthz
        // =============================================================
        void AIServer::HandleHealth(const network::TcpConnectionPtr &conn)
        {
            HttpResponse resp;
            resp.SetStatusCode(200);
            resp.SetStatusMessage("OK");
            resp.SetHeader("Content-Type", "application/json");
            resp.SetBody(BuildJsonResponse(0, "ok", "pong"));
            PrepareShortResponse(resp);
            SendAndClose(conn, resp.ToString());
        }

        // =============================================================
        // /echo
        // =============================================================
        void AIServer::HandleEcho(const network::TcpConnectionPtr &conn,
                                  const HttpRequest &req)
        {
            Json::Value body_json;
            std::string parse_err;
            if (!ParseJsonBody(req.Body(), body_json, parse_err))
            {
                ++failed_requests_;
                SendError(conn, 400, "invalid json: " + parse_err);
                return;
            }
            if (!body_json.isMember("query") || !body_json["query"].isString())
            {
                ++failed_requests_;
                SendError(conn, 400, "missing or invalid 'query' field");
                return;
            }
            std::string query = body_json["query"].asString();
            HttpResponse resp;
            resp.SetStatusCode(200);
            resp.SetStatusMessage("OK");
            resp.SetHeader("Content-Type", "application/json");
            resp.SetBody(BuildJsonResponse(0, "ok", query));
            PrepareShortResponse(resp);
            SendAndClose(conn, resp.ToString());
        }

        // =============================================================
        // /mock_stream
        // =============================================================
        void AIServer::HandleMockStream(const network::TcpConnectionPtr &conn,
                                        const HttpRequest &req)
        {
            Json::Value body_json;
            std::string parse_err;
            if (!ParseJsonBody(req.Body(), body_json, parse_err))
            {
                ++failed_requests_;
                SendError(conn, 400, "invalid json: " + parse_err);
                return;
            }
            if (!body_json.isMember("query") || !body_json["query"].isString())
            {
                ++failed_requests_;
                SendError(conn, 400, "missing or invalid 'query' field");
                return;
            }

            const std::string sse_header =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/event-stream\r\n"
                "Cache-Control: no-cache\r\n"
                "Connection: keep-alive\r\n"
                "X-Accel-Buffering: no\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n";
            conn->Send(sse_header.c_str(), sse_header.size());

            AIConnContext *ctx_ptr = EnsureConnCtx(conn);
            if (!ctx_ptr)
            {
                conn->ForceClose();
                return;
            }

            auto alive = std::make_shared<bool>(true);
            ctx_ptr->sse_alive = alive;

            std::weak_ptr<bool> weak_alive = alive;
            std::weak_ptr<network::TcpConnection> weak_conn = conn;
            network::EventLoop *io_loop = conn->GetLoop();

            auto tokens = std::make_shared<std::vector<std::string>>();
            *tokens = {"你", "好", "，", "这", "是", "本地", "mock", "流式", "输出", "。"};

            ScheduleSSEHeartbeat(io_loop, weak_conn, weak_alive);

            auto sender = std::make_shared<std::function<void(size_t)>>();
            *sender = [io_loop, weak_conn, weak_alive, tokens, sender](size_t idx)
            {
                io_loop->RunAfter(0.05,
                                  [io_loop, weak_conn, weak_alive, tokens, sender, idx]()
                                  {
                                      auto flag = weak_alive.lock();
                                      if (!flag || !(*flag))
                                          return;
                                      auto sp = weak_conn.lock();
                                      if (!sp)
                                          return;

                                      if (idx >= tokens->size())
                                      {
                                          sp->Send("data: [DONE]\n\n", 14);
                                          sp->CloseAfterWrite();
                                          return;
                                      }
                                      std::string data = "data: " + (*tokens)[idx] + "\n\n";
                                      sp->Send(data.c_str(), data.size());
                                      (*sender)(idx + 1);
                                  });
            };
            (*sender)(0);
        }

        // =============================================================
        // /stream_chat
        // =============================================================
        void AIServer::HandleStreamChat(const network::TcpConnectionPtr &conn,
                                        const HttpRequest &req)
        {
            Json::Value body;
            std::string parse_err;
            if (!ParseJsonBody(req.Body(), body, parse_err))
            {
                ++failed_requests_;
                SendError(conn, 400, "invalid json: " + parse_err);
                return;
            }
            if (!body.isMember("query") || !body["query"].isString())
            {
                ++failed_requests_;
                SendError(conn, 400, "missing or invalid 'query' field");
                return;
            }

            std::string query = body["query"].asString();

            if (query.size() > 16000)
            {
                ++failed_requests_;
                SendError(conn, 413, "query too large (max 16000 chars), please upload to knowledge base");
                return;
            }

            uint64_t conv_id = 0;
            uint64_t user_id = 0;
            std::string username;
            bool has_auth = false;

            std::string token_err;
            if (ParseAuthToken(req, user_id, username, token_err))
            {
                has_auth = true;
                if (body.isMember("conversation_id") &&
                    body["conversation_id"].isUInt64())
                {
                    conv_id = body["conversation_id"].asUInt64();
                }
            }

            LOG_INFO << "StreamChat query_len=" << query.size()
                     << " has_auth=" << has_auth
                     << " user_id=" << user_id
                     << " conv_id=" << conv_id;

            // 发送 SSE 响应头
            const std::string sse_header =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/event-stream\r\n"
                "Cache-Control: no-cache\r\n"
                "Connection: keep-alive\r\n"
                "X-Accel-Buffering: no\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n";
            conn->Send(sse_header.c_str(), sse_header.size());

            auto alive = std::make_shared<bool>(true);
            AIConnContext *ctx_ptr = EnsureConnCtx(conn);
            if (ctx_ptr)
                ctx_ptr->sse_alive = alive;

            std::weak_ptr<bool> weak_alive = alive;
            std::weak_ptr<network::TcpConnection> weak_conn = conn;
            network::EventLoop *io_loop = conn->GetLoop();

            ScheduleSSEHeartbeat(io_loop, weak_conn, weak_alive);

            thread_pool_.AddTask(
                [this, weak_conn, weak_alive, io_loop,
                 query, has_auth, user_id, conv_id]()
                {
                    auto start = std::chrono::steady_clock::now();

                    // ---- 1. 处理会话 ----
                    uint64_t actual_conv_id = conv_id;
                    std::string full_query_title = TruncateUtf8(query, 20);

                    if (has_auth)
                    {
                        std::string err;

                        if (actual_conv_id == 0 && conv_repo_)
                        {
                            actual_conv_id = conv_repo_->Create(
                                user_id, full_query_title, "chat", err);
                            if (actual_conv_id == 0)
                            {
                                LOG_WARN << "StreamChat: create conversation failed: " << err;
                                io_loop->RunInLoop([this, weak_conn, weak_alive, err]()
                                                   {
                        auto flag = weak_alive.lock(); if (flag) *flag = false;
                        auto sp = weak_conn.lock(); if (!sp) return;
                        std::string e = "data: [ERROR] create conversation failed: " + err + "\n\n";
                        sp->Send(e.c_str(), e.size());
                        sp->Send("data: [DONE]\n\n", 14); sp->CloseAfterWrite(); });
                                return;
                            }
                        }
                        else if (actual_conv_id != 0 && conv_repo_)
                        {
                            ConversationRecord conv;
                            if (!conv_repo_->FindById(actual_conv_id, conv, err) ||
                                conv.user_id != user_id)
                            {
                                LOG_WARN << "StreamChat: conversation access denied"
                                         << " conv_id=" << actual_conv_id
                                         << " user_id=" << user_id;
                                io_loop->RunInLoop([this, weak_conn, weak_alive]()
                                                   {
                        auto flag = weak_alive.lock(); if (flag) *flag = false;
                        auto sp = weak_conn.lock(); if (!sp) return;
                        const char *e = "data: [ERROR] conversation not found or access denied\n\n";
                        sp->Send(e, strlen(e));
                        sp->Send("data: [DONE]\n\n", 14); sp->CloseAfterWrite(); });
                                return;
                            }
                        }

                        if (actual_conv_id != 0 && msg_repo_)
                        {
                            if (msg_repo_->Insert(actual_conv_id, "user", query, err) == 0)
                            {
                                LOG_WARN << "StreamChat: insert user message failed: " << err;
                            }
                        }
                    }

                    // ---- 2. 构建 messages 数组 ----
                    Json::Value messages(Json::arrayValue);

                    if (has_auth && actual_conv_id != 0 && msg_repo_)
                    {
                        std::string err;
                        std::vector<MessageRecord> history;
                        msg_repo_->ListRecent(actual_conv_id, 12, history, err);

                        if (!err.empty())
                            LOG_WARN << "StreamChat: ListRecent failed: " << err;

                        for (auto &h : history)
                        {
                            Json::Value m;
                            m["role"] = h.role;
                            m["content"] = h.content;
                            messages.append(m);
                        }
                    }

                    // ---- 1.5 如果有会话 id，先通知前端 ----
                    if (actual_conv_id != 0)
                    {
                        io_loop->RunInLoop([weak_conn, weak_alive, actual_conv_id]()
                                           {
        auto flag = weak_alive.lock();
        if (!flag || !(*flag)) return;
        auto sp = weak_conn.lock();
        if (!sp) return;

        std::string conv_event = "data: [CONV:" +
            std::to_string(actual_conv_id) + "]\n\n";
        sp->Send(conv_event.c_str(), conv_event.size()); });
                    }

                    Json::Value current_msg;
                    current_msg["role"] = "user";
                    current_msg["content"] = query;
                    messages.append(current_msg);

                    // ---- 3. 流式调用豆包 ----
                    std::string err_msg;
                    std::string full_answer; // 收集完整回答用于存库

                    auto stream_cb =
                        [this, weak_conn, weak_alive, io_loop,
                         &full_answer](const std::string &delta)
                    {
                        full_answer += delta;

                        auto flag = weak_alive.lock();
                        if (!flag || !(*flag))
                            return;

                        io_loop->RunInLoop([weak_conn, weak_alive, delta]()
                                           {
                auto f = weak_alive.lock();
                if (!f || !(*f)) return;
                auto sp = weak_conn.lock();
                if (!sp) return;
                std::string data = "data: " + delta + "\n\n";
                sp->Send(data.c_str(), data.size()); });
                    };

                    bool ok = llm_.ChatStreamWithMessages(messages, stream_cb, err_msg);

                    double elapsed_ms = std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - start)
                                            .count();

                    // ---- 4. 保存 assistant 回答 ----
                    if (ok && has_auth && actual_conv_id != 0 && msg_repo_)
                    {
                        std::string err;
                        msg_repo_->Insert(actual_conv_id, "assistant", full_answer, err);
                        if (conv_repo_)
                            conv_repo_->Touch(actual_conv_id, err);
                    }

                    // ---- 5. 收尾 ----
                    io_loop->RunInLoop(
                        [this, weak_conn, weak_alive, ok, err_msg,
                         elapsed_ms, actual_conv_id]()
                        {
                            ++llm_calls_;
                            if (ok)
                            {
                                ++llm_success_;
                                std::lock_guard<std::mutex> lk(stats_mutex_);
                                total_llm_time_ms_ += elapsed_ms;
                            }
                            else
                            {
                                ++llm_failures_;
                            }

                            auto flag = weak_alive.lock();
                            if (flag)
                                *flag = false;

                            auto sp = weak_conn.lock();
                            if (!sp)
                            {
                                LOG_INFO << "StreamChat done but conn closed";
                                return;
                            }

                            if (!ok)
                            {
                                std::string e = "data: [ERROR] " + err_msg + "\n\n";
                                sp->Send(e.c_str(), e.size());
                                LOG_ERROR << "StreamChat failed llm_ms=" << elapsed_ms;
                            }
                            else
                            {
                                LOG_INFO << "StreamChat success llm_ms=" << elapsed_ms
                                         << " conv_id=" << actual_conv_id;
                            }

                            sp->Send("data: [DONE]\n\n", 14);
                            sp->CloseAfterWrite();
                        });
                });
        }

        // =============================================================
        // /rag/add
        // =============================================================
        void AIServer::HandleRagAdd(const network::TcpConnectionPtr &conn,
                                    const HttpRequest &req)
        {
            if (!rag_service_)
            {
                SendError(conn, 503, "RAG service not initialized");
                return;
            }

            Json::Value body;
            std::string parse_err;
            if (!ParseJsonBody(req.Body(), body, parse_err))
            {
                ++failed_requests_;
                SendError(conn, 400, "invalid json: " + parse_err);
                return;
            }
            if (!body.isMember("text") || !body["text"].isString() || body["text"].asString().empty())
            {
                ++failed_requests_;
                SendError(conn, 400, "missing or empty 'text' field");
                return;
            }

            std::string text = body["text"].asString();
            std::string source = body.get("source", "").asString();

            LOG_INFO << "RAG /rag/add text_len=" << text.size()
                     << " source=" << source;

            std::weak_ptr<network::TcpConnection> weak_conn = conn;
            network::EventLoop *io_loop = conn->GetLoop();

            thread_pool_.AddTask([this, weak_conn, io_loop, text, source]()
                                 {
                std::string err;
                int chunks = rag_service_->AddKnowledge(text, source, err);

                io_loop->RunInLoop([this, weak_conn, chunks, err]()
                {
                    auto sp = weak_conn.lock();
                    if (!sp) return;

                    HttpResponse resp;
                    if (chunks > 0)
                    {
                        Json::Value result;
                        result["code"]    = 0;
                        result["message"] = "ok";
                        result["chunks"]  = chunks;

                        Json::StreamWriterBuilder writer;
                        writer["indentation"] = "";
                        writer["emitUTF8"]    = true;

                        resp.SetStatusCode(200);
                        resp.SetStatusMessage("OK");
                        resp.SetHeader("Content-Type", "application/json");
                        resp.SetBody(Json::writeString(writer, result));
                    }
                    else
                    {
                        resp.SetStatusCode(500);
                        resp.SetStatusMessage("Internal Server Error");
                        resp.SetHeader("Content-Type", "application/json");
                        resp.SetBody(BuildJsonResponse(500,
                            "Internal Server Error", err));
                    }
                    PrepareShortResponse(resp);
                    SendAndClose(sp, resp.ToString());
                }); });
        }

        // =============================================================
        // /rag/stream_chat
        // =============================================================
        void AIServer::HandleRagStreamChat(
            const network::TcpConnectionPtr &conn,
            const HttpRequest &req)
        {
            if (!rag_service_)
            {
                SendError(conn, 503, "RAG service not initialized");
                return;
            }

            Json::Value body;
            std::string parse_err;
            if (!ParseJsonBody(req.Body(), body, parse_err))
            {
                ++failed_requests_;
                SendError(conn, 400, "invalid json: " + parse_err);
                return;
            }
            if (!body.isMember("query") || !body["query"].isString())
            {
                ++failed_requests_;
                SendError(conn, 400, "missing 'query' field");
                return;
            }

            std::string query = body["query"].asString();
            if (query.size() > 16000)
            {
                ++failed_requests_;
                SendError(conn, 413, "query too large (max 16000 chars), please upload to knowledge base");
                return;
            }
            LOG_INFO << "RAG /rag/stream_chat query_len=" << query.size();

            // 1. 发送 SSE 响应头
            const std::string sse_header =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/event-stream\r\n"
                "Cache-Control: no-cache\r\n"
                "Connection: keep-alive\r\n"
                "X-Accel-Buffering: no\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n";
            conn->Send(sse_header.c_str(), sse_header.size());

            auto alive = std::make_shared<bool>(true);
            AIConnContext *ctx_ptr = EnsureConnCtx(conn);
            if (ctx_ptr)
                ctx_ptr->sse_alive = alive;

            std::weak_ptr<bool> weak_alive = alive;
            std::weak_ptr<network::TcpConnection> weak_conn = conn;
            network::EventLoop *io_loop = conn->GetLoop();

            ScheduleSSEHeartbeat(io_loop, weak_conn, weak_alive);

            thread_pool_.AddTask(
                [this, weak_conn, weak_alive, io_loop, query]()
                {
                    auto start = std::chrono::steady_clock::now();
                    std::string err_msg;

                    auto stream_cb = [this, weak_conn, weak_alive, io_loop](
                                         const std::string &delta)
                    {
                        auto flag = weak_alive.lock();
                        if (!flag || !(*flag))
                            return;

                        io_loop->RunInLoop([weak_conn, weak_alive, delta]()
                                           {
                auto f = weak_alive.lock();
                if (!f || !(*f)) return;
                auto sp = weak_conn.lock();
                if (!sp) return;
                std::string data = "data: " + delta + "\n\n";
                sp->Send(data.c_str(), data.size()); });
                    };

                    bool ok = rag_service_->ChatStream(query, stream_cb, err_msg);
                    double elapsed_ms = std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - start)
                                            .count();

                    io_loop->RunInLoop(
                        [this, weak_conn, weak_alive, ok, err_msg, elapsed_ms]()
                        {
                            ++llm_calls_;
                            if (ok)
                            {
                                ++llm_success_;
                                std::lock_guard<std::mutex> lk(stats_mutex_);
                                total_llm_time_ms_ += elapsed_ms;
                            }
                            else
                            {
                                ++llm_failures_;
                            }

                            auto flag = weak_alive.lock();
                            if (flag)
                                *flag = false;

                            auto sp = weak_conn.lock();
                            if (!sp)
                                return;

                            if (!ok)
                            {
                                std::string e = "data: [ERROR] " + err_msg + "\n\n";
                                sp->Send(e.c_str(), e.size());
                                LOG_ERROR << "RAG StreamChat failed llm_ms=" << elapsed_ms;
                            }
                            else
                            {
                                LOG_INFO << "RAG StreamChat success llm_ms=" << elapsed_ms;
                            }

                            sp->Send("data: [DONE]\n\n", 14);
                            sp->CloseAfterWrite();
                        });
                });
        }

        // =============================================================
        // /rag/stats
        // =============================================================
        void AIServer::HandleRagStats(const network::TcpConnectionPtr &conn)
        {
            int count = rag_service_ ? rag_service_->Count() : -1;

            Json::Value result;
            result["code"] = 0;
            result["message"] = "ok";
            result["count"] = count;

            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            writer["emitUTF8"] = true;

            HttpResponse resp;
            resp.SetStatusCode(200);
            resp.SetStatusMessage("OK");
            resp.SetHeader("Content-Type", "application/json");
            resp.SetBody(Json::writeString(writer, result));
            PrepareShortResponse(resp);
            SendAndClose(conn, resp.ToString());
        }

        bool AIServer::ParseAuthToken(const HttpRequest &req,
                                      uint64_t &user_id,
                                      std::string &username,
                                      std::string &err)
        {
            if (!auth_service_)
            {
                err = "auth service not initialized";
                return false;
            }

            std::string auth_header = req.GetHeader("Authorization");
            if (auth_header.size() <= 7 ||
                auth_header.substr(0, 7) != "Bearer ")
            {
                err = "missing or invalid Authorization header";
                return false;
            }

            std::string token = auth_header.substr(7);
            if (!auth_service_->VerifyToken(token, user_id, username, err))
            {
                return false;
            }

            return true;
        }

        // =============================================================
        // 统一错误响应
        // =============================================================
        void AIServer::SendError(const network::TcpConnectionPtr &conn,
                                 int code, const std::string &detail)
        {
            LOG_WARN << "SendError code=" << code << " detail=" << detail;
            HttpResponse resp;
            resp.SetStatusCode(code);
            resp.SetStatusMessage(StatusText(code));
            resp.SetHeader("Content-Type", "application/json");
            resp.SetBody(BuildJsonResponse(code, StatusText(code), detail));
            PrepareShortResponse(resp);
            SendAndClose(conn, resp.ToString());
        }

        // =============================================================
        // JSON 工具
        // =============================================================
        bool AIServer::ParseJsonBody(const std::string &body,
                                     Json::Value &out,
                                     std::string &err)
        {
            Json::CharReaderBuilder builder;
            std::istringstream iss(body);
            return Json::parseFromStream(builder, iss, &out, &err);
        }

        std::string AIServer::BuildJsonResponse(int code,
                                                const std::string &message,
                                                const std::string &answer)
        {
            Json::Value root;
            root["code"] = code;
            root["message"] = message;
            root["answer"] = answer;

            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            writer["emitUTF8"] = true;
            return Json::writeString(writer, root);
        }

        void AIServer::HandleConversationCreate(
            const network::TcpConnectionPtr &conn,
            const HttpRequest &req)
        {
            if (!auth_service_ || !conv_repo_)
            {
                SendError(conn, 503, "service not initialized");
                return;
            }

            // 验证 token
            uint64_t user_id = 0;
            std::string username, token_err;
            if (!ParseAuthToken(req, user_id, username, token_err))
            {
                SendError(conn, 401, token_err);
                return;
            }

            Json::Value body;
            std::string parse_err;
            if (!ParseJsonBody(req.Body(), body, parse_err))
            {
                SendError(conn, 400, "invalid json: " + parse_err);
                return;
            }

            std::string title = body.get("title", "").asString();
            std::string mode = body.get("mode", "chat").asString();

            if (title.empty())
                title = "新对话";

            std::weak_ptr<network::TcpConnection> weak_conn = conn;
            network::EventLoop *io_loop = conn->GetLoop();

            thread_pool_.AddTask([this, weak_conn, io_loop, user_id, title, mode]()
                                 {
        std::string err;
        uint64_t conv_id = conv_repo_->Create(user_id, title, mode, err);

        io_loop->RunInLoop([this, weak_conn, conv_id, err]()
        {
            auto sp = weak_conn.lock();
            if (!sp) return;

            HttpResponse resp;
            if (conv_id > 0)
            {
                Json::Value out;
                out["code"]            = 0;
                out["message"]         = "ok";
                out["conversation_id"] = static_cast<Json::UInt64>(conv_id);

                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                writer["emitUTF8"]    = true;

                resp.SetStatusCode(200);
                resp.SetStatusMessage("OK");
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(Json::writeString(writer, out));
            }
            else
            {
                resp.SetStatusCode(500);
                resp.SetStatusMessage("Internal Server Error");
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(BuildJsonResponse(500, "Internal Server Error", err));
            }
            PrepareShortResponse(resp);
            SendAndClose(sp, resp.ToString());
        }); });
        }

        void AIServer::HandleConversationList(
            const network::TcpConnectionPtr &conn,
            const HttpRequest &req)
        {
            if (!auth_service_ || !conv_repo_)
            {
                SendError(conn, 503, "service not initialized");
                return;
            }

            uint64_t user_id = 0;
            std::string username, token_err;
            if (!ParseAuthToken(req, user_id, username, token_err))
            {
                SendError(conn, 401, token_err);
                return;
            }

            std::weak_ptr<network::TcpConnection> weak_conn = conn;
            network::EventLoop *io_loop = conn->GetLoop();

            thread_pool_.AddTask([this, weak_conn, io_loop, user_id]()
                                 {
        std::string err;
        std::vector<ConversationRecord> records;
        bool ok = conv_repo_->ListByUser(user_id, 50, records, err);

        io_loop->RunInLoop([this, weak_conn, ok, records, err]()
        {
            auto sp = weak_conn.lock();
            if (!sp) return;

            HttpResponse resp;
            if (ok)
            {
                Json::Value out;
                out["code"]    = 0;
                out["message"] = "ok";
                out["conversations"] = Json::Value(Json::arrayValue);

                for (auto &r : records)
                {
                    Json::Value item;
                    item["id"]         = static_cast<Json::UInt64>(r.id);
                    item["title"]      = r.title;
                    item["mode"]       = r.mode;
                    item["updated_at"] = r.updated_at;
                    out["conversations"].append(item);
                }

                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                writer["emitUTF8"]    = true;

                resp.SetStatusCode(200);
                resp.SetStatusMessage("OK");
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(Json::writeString(writer, out));
            }
            else
            {
                resp.SetStatusCode(500);
                resp.SetStatusMessage("Internal Server Error");
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(BuildJsonResponse(500, "Internal Server Error", err));
            }
            PrepareShortResponse(resp);
            SendAndClose(sp, resp.ToString());
        }); });
        }

        void AIServer::HandleMessageList(
            const network::TcpConnectionPtr &conn,
            const HttpRequest &req)
        {
            if (!auth_service_ || !msg_repo_ || !conv_repo_)
            {
                SendError(conn, 503, "service not initialized");
                return;
            }

            uint64_t user_id = 0;
            std::string username, token_err;
            if (!ParseAuthToken(req, user_id, username, token_err))
            {
                SendError(conn, 401, token_err);
                return;
            }

            Json::Value body;
            std::string parse_err;
            if (!ParseJsonBody(req.Body(), body, parse_err))
            {
                SendError(conn, 400, "invalid json: " + parse_err);
                return;
            }

            if (!body.isMember("conversation_id"))
            {
                SendError(conn, 400, "missing conversation_id");
                return;
            }

            uint64_t conv_id = body["conversation_id"].asUInt64();
            int limit = body.get("limit", 20).asInt();
            if (limit <= 0 || limit > 100)
                limit = 20;

            std::weak_ptr<network::TcpConnection> weak_conn = conn;
            network::EventLoop *io_loop = conn->GetLoop();

            thread_pool_.AddTask(
                [this, weak_conn, io_loop, user_id, conv_id, limit]()
                {
                    // 先验证会话归属
                    std::string err;
                    ConversationRecord conv;
                    if (!conv_repo_->FindById(conv_id, conv, err) ||
                        conv.user_id != user_id)
                    {
                        io_loop->RunInLoop([this, weak_conn]()
                                           {
                auto sp = weak_conn.lock();
                if (!sp) return;
                SendError(sp, 403, "conversation not found or access denied"); });
                        return;
                    }

                    std::vector<MessageRecord> messages;
                    bool ok = msg_repo_->ListRecent(conv_id, limit, messages, err);

                    io_loop->RunInLoop([this, weak_conn, ok, messages, err]()
                                       {
            auto sp = weak_conn.lock();
            if (!sp) return;

            HttpResponse resp;
            if (ok)
            {
                Json::Value out;
                out["code"]     = 0;
                out["message"]  = "ok";
                out["messages"] = Json::Value(Json::arrayValue);

                for (auto &m : messages)
                {
                    Json::Value item;
                    item["id"]         = static_cast<Json::UInt64>(m.id);
                    item["role"]       = m.role;
                    item["content"]    = m.content;
                    item["created_at"] = m.created_at;
                    out["messages"].append(item);
                }

                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                writer["emitUTF8"]    = true;

                resp.SetStatusCode(200);
                resp.SetStatusMessage("OK");
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(Json::writeString(writer, out));
            }
            else
            {
                resp.SetStatusCode(500);
                resp.SetStatusMessage("Internal Server Error");
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(BuildJsonResponse(500, "Internal Server Error", err));
            }
            PrepareShortResponse(resp);
            SendAndClose(sp, resp.ToString()); });
                });
        }

// =============================================================
// /project/create
// =============================================================
void AIServer::HandleProjectCreate(const network::TcpConnectionPtr &conn,
                                   const HttpRequest &req)
{
    if (!project_repo_)
    {
        SendError(conn, 503, "project service not initialized");
        return;
    }

    uint64_t user_id = 0;
    std::string username, token_err;
    if (!ParseAuthToken(req, user_id, username, token_err))
    {
        SendError(conn, 401, token_err);
        return;
    }

    Json::Value body;
    std::string parse_err;
    if (!ParseJsonBody(req.Body(), body, parse_err))
    {
        SendError(conn, 400, "invalid json: " + parse_err);
        return;
    }

    if (!body.isMember("name") || !body["name"].isString() ||
        body["name"].asString().empty())
    {
        SendError(conn, 400, "missing or empty 'name' field");
        return;
    }

    std::string name = body["name"].asString();
    std::string description = body.get("description", "").asString();

    LOG_INFO << "HandleProjectCreate user_id=" << user_id
             << " name=" << name;

    std::weak_ptr<network::TcpConnection> weak_conn = conn;
    network::EventLoop *io_loop = conn->GetLoop();

    thread_pool_.AddTask([this, weak_conn, io_loop, user_id, name, description]()
    {
        std::string err;
        uint64_t project_id = project_repo_->Create(user_id, name, description, err);

        io_loop->RunInLoop([this, weak_conn, project_id, err]()
        {
            auto sp = weak_conn.lock();
            if (!sp) return;

            HttpResponse resp;
            if (project_id > 0)
            {
                Json::Value out;
                out["code"]       = 0;
                out["message"]    = "ok";
                out["project_id"] = static_cast<Json::UInt64>(project_id);

                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                writer["emitUTF8"]    = true;

                resp.SetStatusCode(200);
                resp.SetStatusMessage("OK");
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(Json::writeString(writer, out));

                LOG_INFO << "Project created id=" << project_id;
            }
            else
            {
                resp.SetStatusCode(500);
                resp.SetStatusMessage("Internal Server Error");
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(BuildJsonResponse(500, "Internal Server Error", err));

                LOG_ERROR << "Project create failed: " << err;
            }

            PrepareShortResponse(resp);
            SendAndClose(sp, resp.ToString());
        });
    });
}

// =============================================================
// /project/list
// =============================================================
void AIServer::HandleProjectList(const network::TcpConnectionPtr &conn,
                                 const HttpRequest &req)
{
    if (!project_repo_)
    {
        SendError(conn, 503, "project service not initialized");
        return;
    }

    uint64_t user_id = 0;
    std::string username, token_err;
    if (!ParseAuthToken(req, user_id, username, token_err))
    {
        SendError(conn, 401, token_err);
        return;
    }

    LOG_INFO << "HandleProjectList user_id=" << user_id;

    std::weak_ptr<network::TcpConnection> weak_conn = conn;
    network::EventLoop *io_loop = conn->GetLoop();

    thread_pool_.AddTask([this, weak_conn, io_loop, user_id]()
    {
        std::string err;
        std::vector<ProjectRecord> records;
        bool ok = project_repo_->ListByUser(user_id, records, err);

        io_loop->RunInLoop([this, weak_conn, ok, records, err]()
        {
            auto sp = weak_conn.lock();
            if (!sp) return;

            HttpResponse resp;
            if (ok)
            {
                Json::Value out;
                out["code"]     = 0;
                out["message"]  = "ok";
                out["projects"] = Json::Value(Json::arrayValue);

                for (auto &r : records)
                {
                    Json::Value item;
                    item["id"]          = static_cast<Json::UInt64>(r.id);
                    item["name"]        = r.name;
                    item["description"] = r.description;
                    item["created_at"]  = r.created_at;
                    item["updated_at"]  = r.updated_at;
                    out["projects"].append(item);
                }

                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                writer["emitUTF8"]    = true;

                resp.SetStatusCode(200);
                resp.SetStatusMessage("OK");
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(Json::writeString(writer, out));
            }
            else
            {
                resp.SetStatusCode(500);
                resp.SetStatusMessage("Internal Server Error");
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(BuildJsonResponse(500, "Internal Server Error", err));
            }

            PrepareShortResponse(resp);
            SendAndClose(sp, resp.ToString());
        });
    });
}

// =============================================================
// /project/delete
// =============================================================
void AIServer::HandleProjectDelete(const network::TcpConnectionPtr &conn,
                                   const HttpRequest &req)
{
    if (!project_repo_)
    {
        SendError(conn, 503, "project service not initialized");
        return;
    }

    uint64_t user_id = 0;
    std::string username, token_err;
    if (!ParseAuthToken(req, user_id, username, token_err))
    {
        SendError(conn, 401, token_err);
        return;
    }

    Json::Value body;
    std::string parse_err;
    if (!ParseJsonBody(req.Body(), body, parse_err))
    {
        SendError(conn, 400, "invalid json: " + parse_err);
        return;
    }

    if (!body.isMember("project_id") || !body["project_id"].isUInt64())
    {
        SendError(conn, 400, "missing or invalid 'project_id'");
        return;
    }

    uint64_t project_id = body["project_id"].asUInt64();

    LOG_INFO << "HandleProjectDelete user_id=" << user_id
             << " project_id=" << project_id;

    std::weak_ptr<network::TcpConnection> weak_conn = conn;
    network::EventLoop *io_loop = conn->GetLoop();

    thread_pool_.AddTask(
        [this, weak_conn, io_loop, user_id, project_id]()
    {
        std::string err;

        // 1. 校验项目归属
        if (!project_repo_->Exists(project_id, user_id, err))
        {
            io_loop->RunInLoop([this, weak_conn]()
            {
                auto sp = weak_conn.lock();
                if (!sp) return;
                SendError(sp, 404, "project not found or not owned by you");
            });
            return;
        }

        // 2. 删除该项目下所有知识片段（SQLite）
        std::string del_err;
        vector_store_.DeleteByScope(user_id, "project", (int64_t)project_id, del_err);
        if (!del_err.empty())
        {
            LOG_WARN << "ProjectDelete: delete chunks failed: " << del_err;
        }

        // 3. 解绑所有关联该项目的会话
        if (conv_repo_)
        {
            std::string unbind_err;
            conv_repo_->ClearProjectBinding(user_id, project_id, unbind_err);
            if (!unbind_err.empty())
            {
                LOG_WARN << "ProjectDelete: clear binding failed: " << unbind_err;
            }
        }

        // 4. 删除项目本身（MySQL）
        bool ok = project_repo_->Delete(project_id, user_id, err);

        io_loop->RunInLoop([this, weak_conn, ok, err, project_id]()
        {
            auto sp = weak_conn.lock();
            if (!sp) return;

            HttpResponse resp;
            if (ok)
            {
                Json::Value out;
                out["code"]    = 0;
                out["message"] = "ok";

                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                writer["emitUTF8"]    = true;

                resp.SetStatusCode(200);
                resp.SetStatusMessage("OK");
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(Json::writeString(writer, out));

                LOG_INFO << "Project deleted id=" << project_id;
            }
            else
            {
                resp.SetStatusCode(500);
                resp.SetStatusMessage("Internal Server Error");
                resp.SetHeader("Content-Type", "application/json");
                resp.SetBody(BuildJsonResponse(500, "Internal Server Error", err));

                LOG_ERROR << "Project delete failed: " << err;
            }

            PrepareShortResponse(resp);
            SendAndClose(sp, resp.ToString());
        });
    });
}

    } // namespace ai
} // namespace tmms