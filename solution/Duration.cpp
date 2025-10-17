#include "solution.h"

#include <unordered_map>
#include <algorithm>

double CalcTotalDuration(const std::vector<std::pair<int,int>>& order,
                         const std::vector<Node*>& all_nodes,
                         int card_num) {
    if (card_num <= 0) return 0.0;
    if (order.empty()) return 0.0;

    std::unordered_map<int, const Node*> id2node;
    id2node.reserve(all_nodes.size());
    for (const Node* n : all_nodes) {
        if (n) id2node[n->id()] = n;
    }

    std::unordered_map<int, int> assigned_card;
    assigned_card.reserve(order.size());
    for (const auto& p : order) {
        assigned_card[p.first] = p.second;
    }

    // 执行资源可用时间（每张卡上的算子执行串行）
    std::vector<double> card_avail(static_cast<size_t>(card_num), 0.0);
    // 入站迁移资源可用时间（每张卡同时只能接受一条迁移数据，串行）
    std::vector<double> inbound_avail(static_cast<size_t>(card_num), 0.0);
    std::unordered_map<int, double> finish_time;
    finish_time.reserve(order.size());

    // 消费者侧调度：在消费者准备执行前，按前驱完成时间排序安排跨卡迁移

    for (const auto& [nid, card] : order) {
        auto itNode = id2node.find(nid);
        if (itNode == id2node.end()) continue;
        const Node* n = itNode->second;

        // 1) 计算同卡输入的最晚完成时间
        double local_inputs_max_ft = 0.0;
        // 2) 收集跨卡输入：(生产者完成时间, 传输时间)
        std::vector<std::pair<double,double>> cross_inputs;
        cross_inputs.reserve(n->inputs().size());
        for (const Node* pred : n->inputs()) {
            if (!pred) continue;
            int pid = pred->id();
            double ft = 0.0;
            auto itF = finish_time.find(pid);
            if (itF != finish_time.end()) ft = itF->second;
            int pc = card;
            auto itC = assigned_card.find(pid);
            if (itC != assigned_card.end()) pc = itC->second;
            if (pc == card) {
                local_inputs_max_ft = std::max(local_inputs_max_ft, ft);
            } else {
                double pt = pred->transfer_time();
                cross_inputs.emplace_back(ft, pt);
            }
        }
        // 3) 跨卡输入按生产者完成时间升序排序
        std::sort(cross_inputs.begin(), cross_inputs.end(),
                  [](const auto& a, const auto& b){ return a.first < b.first; });
        // 4) 在目标卡按互斥资源顺序安排入站迁移
        double last_transfer_arrival = 0.0;
        for (const auto& ci : cross_inputs) {
            double start_transfer = std::max(ci.first, std::max(inbound_avail[card], card_avail[card]));
            double arrival = start_transfer + ci.second;
            inbound_avail[card] = arrival;
            card_avail[card] = arrival; // 接收与执行互斥，共用一条轨道
            last_transfer_arrival = arrival;
        }
        // 5) 消费者开始执行时间：等待本卡可用与所有输入到达
        double latest_input_arrival = std::max(local_inputs_max_ft, last_transfer_arrival);
        double start = std::max(card_avail[card], latest_input_arrival);
        double finish = start + n->exec_time();
        card_avail[card] = finish;
        finish_time[nid] = finish;
    }

    return *std::max_element(card_avail.begin(), card_avail.end());
}