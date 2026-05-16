#pragma once

#include <string>

namespace tmms
{
    namespace base
    {
        /// 模块抽象接口
        /// 每个功能模块（AI、RAG、推流等）实现此接口
        class IModule
        {
        public:
            virtual ~IModule() = default;

            /// 模块名称（用于日志和配置查找）
            virtual const std::string &Name() const = 0;

            /// 初始化模块（加载配置、创建资源）
            /// @return true=初始化成功
            virtual bool Init() = 0;

            /// 启动模块（开始监听、注册回调等）
            virtual bool Start() = 0;

            /// 停止模块（优雅关闭）
            virtual void Stop() = 0;
        };

    } // namespace base
} // namespace tmms