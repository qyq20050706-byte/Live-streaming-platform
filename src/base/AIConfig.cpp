#include "AIConfig.h"
#include <fstream>
#include "json/json.h"

using namespace tmms::base;

bool AIConfig::LoadConfig(const std::string &file)
{
  std::ifstream ifs(file);
  if (!ifs.is_open())
  {
    return false;
  }

  Json::Value root;
  Json::CharReaderBuilder reader;
  std::string errs;
  if (!Json::parseFromStream(reader, ifs, &root, &errs))
  {
    return false;
  }

  ai_config_info_ = std::make_shared<AIConfigInfo>();
  ai_config_info_->base_url = root["base_url"].asString();
  ai_config_info_->api_key = root["api_key"].asString();
  ai_config_info_->model = root["model"].asString();
  ai_config_info_->timeout_ms = root["timeout_ms"].asInt();
  ai_config_info_->connect_timeout_ms = root.get("connect_timeout_ms", 10000).asInt();
  ai_config_info_->first_token_timeout_ms = root.get("first_token_timeout_ms", 60000).asInt();
  ai_config_info_->idle_stream_timeout_ms = root.get("idle_stream_timeout_ms", 30000).asInt();

  return true;
}