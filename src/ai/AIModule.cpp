#include "AIModule.h"
#include "base/LogStream.h"

namespace tmms
{
    namespace ai
    {
        AIModule::AIModule(network::EventLoop *loop,
                           const Json::Value &server_cfg,
                           const base::AIConfigInfoPtr &ai_cfg)
            : loop_(loop),
              server_cfg_(server_cfg),
              ai_cfg_(ai_cfg)
        {
        }

        bool AIModule::Init()
        {
            LOG_INFO << "AIModule::Init() creating AIServer...";

            try
            {
                server_ = std::make_unique<AIServer>(loop_, server_cfg_, ai_cfg_);
            }
            catch (const std::exception &e)
            {
                LOG_ERROR << "AIModule::Init() exception: " << e.what();
                return false;
            }

            LOG_INFO << "AIModule::Init() success.";
            return true;
        }

        bool AIModule::Start()
        {
            if (!server_)
            {
                LOG_ERROR << "AIModule::Start() server is null, call Init() first.";
                return false;
            }

            LOG_INFO << "AIModule::Start()";
            server_->Start();
            return true;
        }

        void AIModule::Stop()
        {
            if (server_)
            {
                LOG_INFO << "AIModule::Stop()";
                server_->Stop();
            }
        }

    } // namespace ai
} // namespace tmms