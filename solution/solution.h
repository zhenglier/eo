#ifndef NPU_SOLUTION_H
#define NPU_SOLUTION_H

#include <vector>
#include <utility>
#include "node.h"

// 接口：根据算子与卡数量产生执行序列 (node_id, card_id)
std::vector<std::pair<int,int>> ExecuteOrder(const std::vector<Node*>&& all_nodes, int card_num);

// 接口：根据给定的执行序列计算总时长（makespan）
double CalcTotalDuration(const std::vector<std::pair<int,int>>& order,
                         const std::vector<Node*>& all_nodes,
                         int card_num);

#endif // NPU_SOLUTION_H