#ifndef NPU_GACONFIG_H
#define NPU_GACONFIG_H

#include <string>

struct GAConfig {
    int pop_size = 40;
    int generations = 120;
    double mutation_rate = 0.25;
    int tournament_k = 3;
    int seed = -1; // <0 表示使用时间种子
};

GAConfig LoadGAConfig(const std::string& path);

#endif // NPU_GACONFIG_H