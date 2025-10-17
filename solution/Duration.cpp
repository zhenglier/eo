#include "Solution.h"

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

    for (const auto& [nid, card] : order) {
        auto itNode = id2node.find(nid);
        if (itNode == id2node.end()) continue;
        const Node* n = itNode->second;

        // 计算输入到达时间：
        // - 同卡输入直接取其完成时间
        // - 跨卡输入需要在目标卡的入站资源上串行迁移
        double latest_input_arrival = 0.0;
        double local_inputs_max_ft = 0.0;
        std::vector<std::pair<double,double>> cross_inputs; // (producer_finish_time, transfer_time)
        cross_inputs.reserve(n->inputs().size());
        for (const Node* pred : n->inputs()) {
            double ft = 0.0;
            int prod_card = card;
            int in = pred ? pred->id() : -1;
            auto itFinish = finish_time.find(in);
            if (itFinish != finish_time.end()) {
                ft = itFinish->second;
                auto itProdCard = assigned_card.find(in);
                if (itProdCard != assigned_card.end()) prod_card = itProdCard->second;
            }
            if (prod_card == card) {
                local_inputs_max_ft = std::max(local_inputs_max_ft, ft);
            } else {
                double transfer = 0.0;
                transfer = pred ? pred->transfer_time() : 0.0;
                cross_inputs.emplace_back(ft, transfer);
            }
        }
        // 将跨卡输入按生产完成时间排序，尽量减少入站等待
        std::sort(cross_inputs.begin(), cross_inputs.end(),
                  [](const auto& a, const auto& b){ return a.first < b.first; });
        double last_transfer_arrival = 0.0;
        for (const auto& ci : cross_inputs) {
            double start_transfer = std::max(ci.first, inbound_avail[card]);
            double arrival = start_transfer + ci.second;
            inbound_avail[card] = arrival; // 串行占用入站资源
            last_transfer_arrival = arrival;
        }
        latest_input_arrival = std::max(local_inputs_max_ft, last_transfer_arrival);

        double start = std::max(card_avail[card], latest_input_arrival);
        double finish = start + n->exec_time();

        card_avail[card] = finish;
        finish_time[nid] = finish;
    }

    return *std::max_element(card_avail.begin(), card_avail.end());
}