#include "GAInit.h"

#include <limits>
#include <algorithm>
#include <tuple>

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

// 基于当前图与节点属性的贪心构造：
// 每次在 ready 集合中选择一个 (node, card) 使得结束时间最早，
// 迁移在目的卡 inbound 上串行，避免并发冲突；迁移完成后标记数据驻留卡，避免重复迁移。
static std::vector<std::pair<int,int>> BuildGreedyIndividual(
    const std::unordered_map<int,int>& indeg0,
    const std::unordered_map<int,std::vector<int>>& adj,
    const std::unordered_map<int, const Node*>& id2node,
    int card_num,
    std::mt19937& rng,
    bool randomized)
{
    if (card_num <= 0) return {};
    // 计算最大 id 以便开数组（输入文件保证 id 连续递增）
    int max_id = -1;
    for (const auto& kv : id2node) if (kv.second) max_id = std::max(max_id, kv.first);
    if (max_id < 0) return {};

    std::vector<long long> card_ready(card_num, 0);
    std::vector<long long> inbound_ready(card_num, 0);
    std::vector<int> data_card(max_id + 1, -1);     // 数据当前所在卡
    std::vector<long long> finish_time(max_id + 1, -1); // 节点完成时间

    // 初始化 ready 集合
    std::unordered_map<int,int> indeg = indeg0;
    std::vector<int> ready;
    ready.reserve(indeg.size());
    for (const auto& kv : indeg) if (kv.second == 0) ready.push_back(kv.first);

    std::vector<std::pair<int,int>> order;
    order.reserve(indeg.size());

    while (!ready.empty()) {
        int best_nid = ready[0];
        int best_card = 0;
        long long best_end = std::numeric_limits<long long>::max();
        const double epsilon = 0.2; // 20% 概率在前 k 个候选中随机挑选
        std::vector<std::tuple<long long,int,int>> candidates; // (end, nid, card)
        candidates.reserve(ready.size() * std::max(1, card_num));
        // 对完成时间加入与原值相关的相对随机扰动，增强探索
        std::uniform_real_distribution<double> jitter(0.0, 1.0);
        std::uniform_real_distribution<double> prob(0.0, 1.0);
        const double noise_frac = 0.05; // 5% 幅度的相对噪声

        for (int nid : ready) {
            auto itN = id2node.find(nid);
            if (itN == id2node.end() || !(itN->second)) continue;
            const Node* node = itN->second;
            for (int c = 0; c < card_num; ++c) {
                long long start = card_ready[c];
                long long local_max = 0;
                long long inbound_avail = inbound_ready[c];
                const auto& inputs = node->inputs();
                std::vector<std::pair<long long,long long>> cross; // (finish, transfer)
                for (const Node* pred : inputs) {
                    if (!pred) continue;
                    int pid = pred->id();
                    long long ft = (pid >= 0 && pid <= max_id) ? finish_time[pid] : -1;
                    if (ft < 0) { local_max = std::numeric_limits<long long>::max()/4; cross.clear(); break; }
                    if (data_card[pid] == c) {
                        local_max = std::max(local_max, ft);
                    } else {
                        cross.emplace_back(ft, pred->transfer_time());
                    }
                }
                if (local_max >= std::numeric_limits<long long>::max()/8) {
                    continue; // 输入尚未完成，不可选（理论上不会发生）
                }
                if (!cross.empty()) {
                    std::sort(cross.begin(), cross.end(),
                        [](const auto& x, const auto& y){ return x.first < y.first; });
                    long long tmp_inbound = inbound_avail;
                    for (const auto& rec : cross) {
                        tmp_inbound = std::max(tmp_inbound, rec.first) + rec.second;
                    }
                    inbound_avail = tmp_inbound;
                }
                long long ready_at = std::max(start, std::max(local_max, inbound_avail));
                long long end = ready_at + node->exec_time();
                long long score = randomized ? (end + static_cast<long long>(noise_frac * end * jitter(rng))) : end;
                candidates.emplace_back(score, nid, c);
                // 直接更新最优候选的逻辑已移除，改为在外层使用候选集排序 + epsilon 随机选择
            }
        }
        
        // 根据候选的带噪完成时间进行 epsilon-贪心选择
        std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b){
            if (std::get<0>(a) != std::get<0>(b)) return std::get<0>(a) < std::get<0>(b);
            if (std::get<1>(a) != std::get<1>(b)) return std::get<1>(a) < std::get<1>(b);
            return std::get<2>(a) < std::get<2>(b);
        });
        int k = randomized ? std::min(3, static_cast<int>(candidates.size())) : 1;
        if (!candidates.empty()) {
            int pick_index = 0;
            if (randomized && prob(rng) < epsilon && k > 0) {
                std::uniform_int_distribution<int> pick(0, k - 1);
                pick_index = pick(rng);
            } else {
                pick_index = 0;
            }
            best_end = std::get<0>(candidates[pick_index]);
            best_nid = std::get<1>(candidates[pick_index]);
            best_card = std::get<2>(candidates[pick_index]);
        }
        
        // 提交 best_nid 在 best_card 的调度
        const Node* node = id2node.at(best_nid);
        long long inbound_avail = inbound_ready[best_card];
        long long local_max = 0;
        for (const Node* pred : node->inputs()) {
            if (!pred) continue;
            int pid = pred->id();
            long long ft = (pid >= 0 && pid <= max_id) ? finish_time[pid] : -1;
            if (ft < 0) continue;
            if (data_card[pid] == best_card) {
                local_max = std::max(local_max, ft);
            } else {
                inbound_avail = std::max(inbound_avail, ft) + pred->transfer_time();
                data_card[pid] = best_card; // 标记数据已在目的卡，后续消费者无需重复迁移
            }
        }
        long long ready_at = std::max(card_ready[best_card], std::max(local_max, inbound_avail));
        long long end = ready_at + node->exec_time();
        card_ready[best_card] = end;
        inbound_ready[best_card] = inbound_avail;
        finish_time[best_nid] = end;
        order.emplace_back(best_nid, best_card);

        // 更新 ready 集合
        for (auto it = ready.begin(); it != ready.end(); ++it) { if (*it == best_nid) { ready.erase(it); break; } }
        auto itAdj = adj.find(best_nid);
        if (itAdj != adj.end()) {
            for (int succ : itAdj->second) {
                auto inIt = indeg.find(succ);
                if (inIt != indeg.end() && --(inIt->second) == 0) ready.push_back(succ);
            }
        }
    }

    if (order.size() != indeg.size()) return {};
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

    // 先加入一个贪心解，作为种群的强种子
    auto greedy = BuildGreedyIndividual(indeg0, adj, id2node, card_num, rng, false);
    if (!greedy.empty()) population.push_back(std::move(greedy));

    // 其余用启发式 + 随机噪声生成
    std::uniform_real_distribution<double> noise(0.0, 0.1);
    for (int i = static_cast<int>(population.size()); i < pop_size; ++i) {
        std::unordered_map<int,double> prio;
        prio.reserve(node_ids.size());
        for (int nid : node_ids) {
            auto it = id2node.find(nid);
            if (it != id2node.end() && it->second) {
                const Node* n = it->second;
                double base = n->exec_time() + 0.5 * n->transfer_time();
                prio[nid] = -base + noise(rng);
            } else {
                prio[nid] = noise(rng);
            }
        }
        auto indiv = TopoByPriority(indeg0, adj, card_num, rng, prio, nullptr);
        if (indiv.empty()) return {}; // 有环，无法调度
        population.push_back(std::move(indiv));
    }
    return population;
}