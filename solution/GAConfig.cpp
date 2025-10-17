#include "GAConfig.h"

// 返回内置默认配置；不进行任何本地文件读取
GAConfig LoadGAConfig(const std::string& /*path*/) {
    GAConfig cfg; // 使用 GAConfig 默认成员值
    return cfg;
}