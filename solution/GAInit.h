#pragma once

#include <vector>
#include <unordered_map>
#include <random>
#include "node.h"

// 基于优先级的拓扑排序并分配卡号（可继承父代卡）
// priority: 节点优先级，数值越小越优先；inherit_cards 可为空，表示不继承
std::vector<std::pair<int,int>> TopoByPriority(
    const std::unordered_map<int,int>& indeg0,
    const std::unordered_map<int,std::vector<int>>& adj,
    int card_num,
    std::mt19937& rng,
    const std::unordered_map<int,double>& priority,
    const std::unordered_map<int,int>* inherit_cards);

// 初始种群生成：随机优先级 + 拓扑排序 + 随机卡分配
std::vector<std::vector<std::pair<int,int>>> InitializePopulation(
    const std::vector<int>& node_ids,
    const std::unordered_map<int,int>& indeg0,
    const std::unordered_map<int,std::vector<int>>& adj,
    const std::unordered_map<int, const Node*>& id2node,
    int card_num,
    int pop_size,
    std::mt19937& rng);