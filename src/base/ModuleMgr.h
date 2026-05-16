#pragma once

#include "IModule.h"
#include "NonCopyable.h"
#include "Singleton.h"
#include "LogStream.h"
#include <vector>
#include <memory>
#include <string>

namespace tmms
{
    namespace base
    {
        class ModuleMgr : public NonCopyable
        {
        public:
            ModuleMgr() = default;
            ~ModuleMgr() = default;

            /// 注册一个模块
            void Register(std::shared_ptr<IModule> mod)
            {
                LOG_INFO << "ModuleMgr: registered module '" << mod->Name() << "'";
                modules_.push_back(std::move(mod));
            }

            /// 初始化所有模块（按注册顺序）
            bool InitAll()
            {
                for (auto &m : modules_)
                {
                    LOG_INFO << "ModuleMgr: initializing '" << m->Name() << "'";
                    if (!m->Init())
                    {
                        LOG_ERROR << "ModuleMgr: failed to init '" << m->Name() << "'";
                        return false;
                    }
                }
                return true;
            }

            /// 启动所有模块
            bool StartAll()
            {
                for (auto &m : modules_)
                {
                    LOG_INFO << "ModuleMgr: starting '" << m->Name() << "'";
                    if (!m->Start())
                    {
                        LOG_ERROR << "ModuleMgr: failed to start '" << m->Name() << "'";
                        return false;
                    }
                }
                return true;
            }

            /// 停止所有模块（逆序停止）
            void StopAll()
            {
                for (auto it = modules_.rbegin(); it != modules_.rend(); ++it)
                {
                    LOG_INFO << "ModuleMgr: stopping '" << (*it)->Name() << "'";
                    (*it)->Stop();
                }
            }

        private:
            std::vector<std::shared_ptr<IModule>> modules_;
        };

#define sModuleMgr tmms::base::Singleton<tmms::base::ModuleMgr>::Instance()

    } // namespace base
} // namespace tmms