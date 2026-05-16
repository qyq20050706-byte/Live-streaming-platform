#pragma once

#include "ai/http/HttpContext.h"
#include "ai/http/HttpRequest.h"
#include "ai/http/HttpResponse.h"
#include "ai/llm/DoubaoLLM.h"
#include "ai/server/AIConnContext.h"
#include "ai/embedding/DoubaoEmbedding.h"
#include "ai/vector/SQLiteVectorStore.h"
#include "ai/rag/RAGService.h"
#include "network/TcpServer.h"
#include "network/net/TcpConnection.h"
#include "network/net/EventLoop.h"
#include "base/AIConfig.h"
#include "base/ThreadPool.h"
#include <json/json.h>
#include <atomic>
#include <memory>
#include <string>
#include <mutex>
#include <filesystem>
#include "ai/storage/MySQLClient.h"
#include "ai/auth/AuthService.h"
#include "ai/storage/ConversationRepo.h"
#include "ai/storage/MessageRepo.h"

namespace tmms
{
    namespace ai
    {
        class AIServer
        {
        public:
            AIServer(network::EventLoop *loop,
                     const Json::Value &server_cfg,
                     const base::AIConfigInfoPtr &ai_cfg);

            ~AIServer();

            void Start();
            void Stop();

        private:
            // ---------------------------------------------------
            // TcpServer 回调
            // ---------------------------------------------------
            void OnConnection(const network::TcpConnectionPtr &conn);
            void OnConnectionDestroy(const network::TcpConnectionPtr &conn);
            void OnMessage(const network::TcpConnectionPtr &conn,
                           network::MsgBuffer &buf);

            // ---------------------------------------------------
            // 路由处理（原有）
            // ---------------------------------------------------
            void HandleOptions(const network::TcpConnectionPtr &conn);
            void HandleChat(const network::TcpConnectionPtr &conn,
                            const HttpRequest &req);
            void HandleStreamChat(const network::TcpConnectionPtr &conn,
                                  const HttpRequest &req);
            void HandleHealth(const network::TcpConnectionPtr &conn);
            void HandleEcho(const network::TcpConnectionPtr &conn,
                            const HttpRequest &req);
            void HandleMockStream(const network::TcpConnectionPtr &conn,
                                  const HttpRequest &req);

            // ---------------------------------------------------
            // 路由处理（RAG 新增）
            // ---------------------------------------------------
            void HandleRagAdd(const network::TcpConnectionPtr &conn,
                              const HttpRequest &req);
            void HandleRagChat(const network::TcpConnectionPtr &conn,
                               const HttpRequest &req);
            void HandleRagStreamChat(const network::TcpConnectionPtr &conn,
                                     const HttpRequest &req);
            void HandleRagStats(const network::TcpConnectionPtr &conn);

            // ---------------------------------------------------
            // 工具函数
            // ---------------------------------------------------
            bool ParseAuthToken(const HttpRequest &req,
                                uint64_t &user_id,
                                std::string &username,
                                std::string &err);

            void SendError(const network::TcpConnectionPtr &conn,
                           int code, const std::string &detail);
            void SendAndClose(const network::TcpConnectionPtr &conn,
                              const std::string &data);
            void ScheduleSSEHeartbeat(
                network::EventLoop *io_loop,
                std::weak_ptr<network::TcpConnection> weak_conn,
                std::weak_ptr<bool> weak_alive);
            void PrepareShortResponse(HttpResponse &resp);

            bool ParseJsonBody(const std::string &body,
                               Json::Value &out,
                               std::string &err);
            std::string BuildJsonResponse(int code,
                                          const std::string &message,
                                          const std::string &answer);
            static std::string StatusText(int code);
            static AIConnContext *EnsureConnCtx(
                const network::TcpConnectionPtr &conn);

            // RAG 初始化
            bool InitRAG(const std::string &embedding_cfg,
                         const std::string &rag_cfg,
                         std::string &err);

        private:
            // ---------------------------------------------------
            // 核心组件
            // ---------------------------------------------------
            network::EventLoop *loop_;
            network::TcpServer server_;
            DoubaoLLM llm_;
            base::ThreadPool thread_pool_;

            // RAG 组件
            DoubaoEmbedding embedder_;
            SQLiteVectorStore vector_store_;
            std::unique_ptr<RAGService> rag_service_;

            // ---------------------------------------------------
            // 配置
            // ---------------------------------------------------
            uint16_t port_;
            int32_t io_thread_num_;
            int32_t idle_timeout_s_;
            int32_t sse_heartbeat_interval_s_;
            int32_t max_connections_;

            // ---------------------------------------------------
            // 统计信息（原子变量，多线程安全）
            // ---------------------------------------------------
            std::atomic<int32_t> active_connections_{0};
            std::atomic<uint64_t> total_requests_{0};
            std::atomic<uint64_t> chat_requests_{0};
            std::atomic<uint64_t> stream_requests_{0};
            std::atomic<uint64_t> failed_requests_{0};
            std::atomic<uint64_t> rejected_requests_{0};
            std::atomic<uint64_t> llm_calls_{0};
            std::atomic<uint64_t> llm_success_{0};
            std::atomic<uint64_t> llm_failures_{0};

            std::mutex stats_mutex_;
            double total_llm_time_ms_{0.0};

            std::unique_ptr<ConversationRepo> conv_repo_;
            std::unique_ptr<MessageRepo> msg_repo_;

            // ---------------------------------------------------
            // 数据库 + 认证
            // ---------------------------------------------------
            MySQLClient db_;
            JWTConfig jwt_cfg_;
            std::unique_ptr<AuthService> auth_service_;

            // DB 和 Auth 初始化
            bool InitDB(const std::string &db_cfg_path,
                        const std::string &auth_cfg_path,
                        std::string &err);

            bool stopped_{false};

            void StartStatsTimer();
            void LogStats();
            void HandleUserRegister(const network::TcpConnectionPtr &conn,
                                    const HttpRequest &req);
            void HandleUserLogin(const network::TcpConnectionPtr &conn,
                                 const HttpRequest &req);
            void HandleConversationCreate(const network::TcpConnectionPtr &conn,
                                          const HttpRequest &req);
            void HandleConversationList(const network::TcpConnectionPtr &conn,
                                        const HttpRequest &req);
            void HandleMessageList(const network::TcpConnectionPtr &conn,
                                   const HttpRequest &req);
        };

    } // namespace ai
} // namespace tmms