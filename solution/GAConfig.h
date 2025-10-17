#ifndef NPU_GACONFIG_H
#define NPU_GACONFIG_H

#include <string>

struct GAConfig {
    int pop_size = 5;
    int generations = 120;
    double mutation_rate = 0.5;
    int tournament_k = 2;
    int seed = -1; // <0 表示使用时间种子
};

#endif // NPU_GACONFIG_H