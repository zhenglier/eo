#include "GAInit.h"

#include <limits>
#include <algorithm>

std::vector<std::pair<int,int>> TopoByPriority(
    const std::unordered_map<int,int>& indeg0,
    const std::unordered_map<int,std::vector<int>>& adj,
    int card_num,
    std::mt19937& rng,
    const std::unordered_map<int,double>& priority,
    const std::unordered_map<int,int>* inherit_cards)
{
    // 拷贝入度
    std::unordered_map<int,int> indeg = indeg0;
    std::vector<int> ready;
    ready.reserve(indeg.size());
    for (const auto& kv : indeg) if (kv.second == 0) ready.push_back(kv.first);

    std::vector<std::pair<int,int>> order;
    order.reserve(indeg.size());
    std::uniform_int_distribution<int> card_dist(0, std::max(0, card_num - 1));

    while (!ready.empty()) {
        int chosen = ready[0];
        double best_p = std::numeric_limits<double>::infinity();
        for (int nid : ready) {
            double p = priority.count(nid) ? priority.at(nid) : 0.0;
            if (p < best_p || (p == best_p && nid < chosen)) { chosen = nid; best_p = p; }
        }
        int card = (inherit_cards && inherit_cards->count(chosen))
                    ? inherit_cards->at(chosen)
                    : card_dist(rng);
        order.emplace_back(chosen, card);

        // 从 ready 移除
        for (auto it = ready.begin(); it != ready.end(); ++it) {
            if (*it == chosen) { ready.erase(it); break; }
        }
        // 更新后继入度
        auto it = adj.find(chosen);
        if (it != adj.end()) {
            for (int succ : it->second) {
                auto inIt = indeg.find(succ);
                if (inIt != indeg.end() && --(inIt->second) == 0) ready.push_back(succ);
            }
        }
    }
    if (order.size() != indeg.size()) return {}; // 有环或未覆盖
    return order;
}

std::vector<std::vector<std::pair<int,int>>> InitializePopulation(
    const std::vector<int>& node_ids,
    const std::unordered_map<int,int>& indeg0,
    const std::unordered_map<int,std::vector<int>>& adj,
    const std::unordered_map<int, const Node*>& id2node,
    int card_num,
    int pop_size,
    std::mt19937& rng)
{
    std::vector<std::vector<std::pair<int,int>>> population;
    population.reserve(pop_size);
    // 少量随机扰动
    std::uniform_real_distribution<double> noise(0.0, 0.1);
    for (int i = 0; i < pop_size; ++i) {
        std::unordered_map<int,double> prio;
        prio.reserve(node_ids.size());
        for (int nid : node_ids) {
            auto it = id2node.find(nid);
            if (it != id2node.end() && it->second) {
                const Node* n = it->second;
                // 启发式：优先大的执行时间与（部分）迁移时间的算子，采用较小的priority值
                // priority 越小优先级越高，这里取负值实现“大的时间靠前”
                double base = n->exec_time() + 0.5 * n->transfer_time();
                prio[nid] = -base + noise(rng);
            } else {
                // 缺失节点信息时退化为小噪声随机
                prio[nid] = noise(rng);
            }
        }
        auto indiv = TopoByPriority(indeg0, adj, card_num, rng, prio, nullptr);
        if (indiv.empty()) return {}; // 有环，无法调度
        population.push_back(std::move(indiv));
    }
    return population;
}