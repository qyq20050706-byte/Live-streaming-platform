#include "ai/AIModule.h"
#include "network/net/EventLoop.h"
#include "base/Config.h"
#include "base/AIConfig.h"
#include "base/Logger.h"
#include "base/FileLog.h"
#include "base/LogStream.h"
#include "base/Singleton.h"
#include "base/ModuleMgr.h"

#include <iostream>
#include <signal.h>
#include <memory>
#include <string>

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

static tmms::network::EventLoop *g_main_loop = nullptr;

static void OnSignal(int /*sig*/)
{
    if (g_main_loop)
    {
        g_main_loop->Quit();
    }
}

int main(int /*argc*/, char * /*argv*/[])
{
    const std::string main_cfg_path =
        std::string(PROJECT_ROOT) + "/bin/config/config.json";
    const std::string doubao_cfg_path =
        std::string(PROJECT_ROOT) + "/bin/config/ai/doubao.json";
    const std::string log_dir_path =
        std::string(PROJECT_ROOT) + "/bin/log/";

    // ========== 1. 加载主配置 ==========
    if (!sConfigMgr->LoadConfig(main_cfg_path))
    {
        std::cerr << "[FATAL] Failed to load " << main_cfg_path << "\n";
        return -1;
    }

    auto config = sConfigMgr->GetConfig();
    if (!config)
    {
        std::cerr << "[FATAL] Config object is null.\n";
        return -1;
    }

    // ========== 2. 初始化日志 ==========
    auto log_info = config->GetLogInfo();
    if (!log_info)
    {
        std::cerr << "[FATAL] Log info is null in config.\n";
        return -1;
    }

    std::string log_name =
        log_info->name.empty() ? "ai_server.log" : log_info->name;
    std::string final_log_path = log_dir_path + log_name;

    auto file_log = std::make_shared<tmms::base::FileLog>();
    file_log->SetRotate(log_info->rotate_type);

    if (!file_log->Open(final_log_path))
    {
        std::cerr << "[FATAL] Failed to open log file: "
                  << final_log_path << "\n";
        return -1;
    }

    static tmms::base::Logger s_logger(file_log);
    s_logger.SetLogLevel(log_info->level);
    tmms::base::g_logger = &s_logger;

    LOG_INFO << "===== ai_server starting (modular) =====";
    LOG_INFO << "PROJECT_ROOT = " << PROJECT_ROOT;
    LOG_INFO << "Config file  = " << main_cfg_path;
    LOG_INFO << "AI config    = " << doubao_cfg_path;
    LOG_INFO << "Log file     = " << final_log_path;

    // ========== 3. 加载 AI 配置 ==========
    if (!sAIConfigMgr->LoadConfig(doubao_cfg_path))
    {
        LOG_ERROR << "Failed to load " << doubao_cfg_path;
        return -1;
    }

    auto ai_config = sAIConfigMgr->GetConfig();
    if (!ai_config || !ai_config->GetAIConfigInfo())
    {
        LOG_ERROR << "AI config is null or invalid.";
        return -1;
    }

    auto ai_cfg = ai_config->GetAIConfigInfo();
    LOG_INFO << "AI config loaded."
             << " model=" << ai_cfg->model
             << " timeout_ms=" << ai_cfg->timeout_ms;

    // ========== 4. 读取 ai_server 配置节点 ==========
    Json::Value raw_cfg;
    if (!tmms::base::Config::LoadFile(main_cfg_path, raw_cfg))
    {
        LOG_ERROR << "Failed to re-read " << main_cfg_path;
        return -1;
    }

    if (!raw_cfg.isMember("ai_server"))
    {
        LOG_ERROR << "Missing 'ai_server' section in config.json";
        return -1;
    }

    const Json::Value &server_cfg = raw_cfg["ai_server"];

    LOG_INFO << "Server config:"
             << " port=" << server_cfg["port"].asUInt()
             << " io_thread_num=" << server_cfg.get("io_thread_num", 0).asInt()
             << " worker_thread_num=" << server_cfg.get("worker_thread_num", 4).asInt()
             << " max_connections=" << server_cfg.get("max_connections", 2000).asInt()
             << " idle_timeout_s=" << server_cfg["idle_timeout_s"].asInt()
             << " sse_heartbeat_s=" << server_cfg["sse_heartbeat_interval_s"].asInt();

    // ========== 5. 注册信号 ==========
    signal(SIGINT, OnSignal);
    signal(SIGTERM, OnSignal);
    signal(SIGPIPE, SIG_IGN);

    // ========== 6. 创建 EventLoop ==========
    tmms::network::EventLoop main_loop;
    g_main_loop = &main_loop;

    // ========== 7. 注册模块 ==========
    auto ai_module = std::make_shared<tmms::ai::AIModule>(
        &main_loop, server_cfg, ai_cfg);

    sModuleMgr->Register(ai_module);

    // ========== 8. 初始化并启动所有模块 ==========
    if (!sModuleMgr->InitAll())
    {
        LOG_ERROR << "Module initialization failed.";
        return -1;
    }

    if (!sModuleMgr->StartAll())
    {
        LOG_ERROR << "Module start failed.";
        return -1;
    }

    // ========== 9. 进入事件循环 ==========
    LOG_INFO << "===== Event loop running =====";
    main_loop.Loop();

    // ========== 10. 优雅退出 ==========
    LOG_INFO << "===== Shutting down =====";
    sModuleMgr->StopAll();
    LOG_INFO << "===== ai_server stopped =====";

    return 0;
}