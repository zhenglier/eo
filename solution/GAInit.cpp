#include "GAInit.h"

#include <limits>
#include <algorithm>
#include <tuple>
#include <queue>

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
    // 使用按优先级的最小堆选择就绪节点，避免每次线性扫描
    using Key = std::pair<double,int>;
    struct PrioCmp {
        bool operator()(const Key& a, const Key& b) const {
            if (a.first != b.first) return a.first > b.first; // 较小优先级先出堆
            return a.second > b.second; // id 更小优先
        }
    };
    std::priority_queue<Key, std::vector<Key>, PrioCmp> ready;
    for (const auto& kv : indeg) {
        if (kv.second == 0) {
            double p = 0.0;
            auto itp = priority.find(kv.first);
            if (itp != priority.end()) p = itp->second;
            ready.push({p, kv.first});
        }
    }

    std::vector<std::pair<int,int>> order;
    order.reserve(indeg.size());
    std::uniform_int_distribution<int> card_dist(0, std::max(0, card_num - 1));

    while (!ready.empty()) {
        auto cur = ready.top(); ready.pop();
        int chosen = cur.second;
        int card = (inherit_cards && inherit_cards->count(chosen))
                   ? inherit_cards->at(chosen)
                   : card_dist(rng);
        order.emplace_back(chosen, card);

        // 更新后继入度并将新的就绪节点入堆
        auto it = adj.find(chosen);
        if (it != adj.end()) {
            for (int succ : it->second) {
                auto inIt = indeg.find(succ);
                if (inIt != indeg.end() && --(inIt->second) == 0) {
                    double p = 0.0;
                    auto itp = priority.find(succ);
                    if (itp != priority.end()) p = itp->second;
                    ready.push({p, succ});
                }
            }
        }
    }
    if (order.size() != indeg.size()) return {}; // 有环或未覆盖
    return order;
}

// 基于优先级顺序的拓扑 + EFT 卡分配：对已选就绪节点在所有卡上评估最早完成时间
std::vector<std::pair<int,int>> TopoByPriorityWithEFT(
        const std::unordered_map<int,int>& indeg0,
        const std::unordered_map<int,std::vector<int>>& adj,
        const std::unordered_map<int, const Node*>& id2node,
        int card_num,
        std::mt19937& rng,
        const std::unordered_map<int,double>& priority,
        const std::unordered_map<int,int>* inherit_cards)
{
    if (card_num <= 0) return {};
    // 计算最大 id（文件保证 id 连续递增）
    int max_id = -1;
    for (const auto& kv : id2node) if (kv.second) max_id = std::max(max_id, kv.first);
    if (max_id < 0) return {};

    std::vector<long long> card_ready(card_num, 0);
    std::vector<long long> inbound_ready(card_num, 0);
    std::vector<int> data_card(max_id + 1, -1);
    std::vector<long long> finish_time(max_id + 1, -1);

    // 拷贝入度并初始化就绪堆
    std::unordered_map<int,int> indeg = indeg0;
    using Key = std::pair<double,int>;
    struct PrioCmp2 {
        bool operator()(const Key& a, const Key& b) const {
            if (a.first != b.first) return a.first > b.first;
            return a.second > b.second;
        }
    };
    std::priority_queue<Key, std::vector<Key>, PrioCmp2> ready;
    for (const auto& kv : indeg) {
        if (kv.second == 0) {
            double p = 0.0;
            auto itp = priority.find(kv.first);
            if (itp != priority.end()) p = itp->second;
            ready.push({p, kv.first});
        }
    }

    std::vector<std::pair<int,int>> order;
    order.reserve(indeg.size());

    auto choose_card_eft = [&](int nid) -> std::pair<int,long long> {
        const Node* node = id2node.at(nid);
        int best_card = 0;
        long long best_end = std::numeric_limits<long long>::max();
        // 如果存在继承卡，作为并列时偏好
        int inherit = (inherit_cards && inherit_cards->count(nid)) ? inherit_cards->at(nid) : -1;
        for (int c = 0; c < card_num; ++c) {
            long long start = card_ready[c];
            long long local_max = 0;
            long long tmp_inbound = inbound_ready[c];
            std::vector<std::pair<long long,long long>> cross; // (finish, transfer)
            for (const Node* pred : node->inputs()) {
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
            if (local_max >= std::numeric_limits<long long>::max()/8) continue;
            if (!cross.empty()) {
                std::sort(cross.begin(), cross.end(), [](const auto& x, const auto& y){ return x.first < y.first; });
                for (const auto& rec : cross) tmp_inbound = std::max(tmp_inbound, rec.first) + rec.second;
            }
            long long ready_at = std::max(start, std::max(local_max, tmp_inbound));
            long long end = ready_at + node->exec_time();
            if (end < best_end || (end == best_end && inherit == c)) {
                best_end = end;
                best_card = c;
            }
        }
        return {best_card, best_end};
    };

    while (!ready.empty()) {
        auto cur = ready.top(); ready.pop();
        int chosen = cur.second;
        auto [best_card, best_end] = choose_card_eft(chosen);
        if (best_end >= std::numeric_limits<long long>::max()/8) return {}; // 保护

        const Node* node = id2node.at(chosen);
        // 重新计算 inbound_avail 与 local_max 以提交更新状态
        long long inbound_avail = inbound_ready[best_card];
        long long local_max = 0;
        std::vector<std::pair<long long,long long>> cross;
        for (const Node* pred : node->inputs()) {
            if (!pred) continue;
            int pid = pred->id();
            long long ft = (pid >= 0 && pid <= max_id) ? finish_time[pid] : -1;
            if (ft < 0) continue;
            if (data_card[pid] == best_card) {
                local_max = std::max(local_max, ft);
            } else {
                cross.emplace_back(ft, pred->transfer_time());
            }
        }
        if (!cross.empty()) {
            std::sort(cross.begin(), cross.end(), [](const auto& x, const auto& y){ return x.first < y.first; });
            for (const auto& rec : cross) {
                inbound_avail = std::max(inbound_avail, rec.first) + rec.second;
            }
        }
        long long ready_at = std::max(card_ready[best_card], std::max(local_max, inbound_avail));
        long long end = ready_at + node->exec_time();
        card_ready[best_card] = end;
        inbound_ready[best_card] = inbound_avail;
        finish_time[chosen] = end;
        order.emplace_back(chosen, best_card);
        // 标记数据驻留到目的卡，减少后续重复迁移
        for (const Node* pred : node->inputs()) {
            if (!pred) continue; data_card[pred->id()] = best_card;
        }

        // 更新后继入度并将新的就绪节点入堆
        auto it = adj.find(chosen);
        if (it != adj.end()) {
            for (int succ : it->second) {
                auto inIt = indeg.find(succ);
                if (inIt != indeg.end() && --(inIt->second) == 0) {
                    double p = 0.0;
                    auto itp = priority.find(succ);
                    if (itp != priority.end()) p = itp->second;
                    ready.push({p, succ});
                }
            }
        }
    }

    if (order.size() != indeg.size()) return {};
    return order;
}

// 基于当前图计算 HEFT upward-rank
static std::unordered_map<int,double> ComputeUpwardRank(
        const std::unordered_map<int,int>& indeg0,
        const std::unordered_map<int,std::vector<int>>& adj,
        const std::unordered_map<int, const Node*>& id2node)
{
    // 拓扑序
    std::unordered_map<int,int> indeg = indeg0;
    std::queue<int> q;
    for (const auto& kv : indeg) if (kv.second == 0) q.push(kv.first);
    std::vector<int> topo;
    topo.reserve(indeg.size());
    while (!q.empty()) {
        int u = q.front(); q.pop();
        topo.push_back(u);
        auto it = adj.find(u);
        if (it != adj.end()) {
            for (int v : it->second) {
                if (--indeg[v] == 0) q.push(v);
            }
        }
    }
    std::unordered_map<int,double> rank_u;
    rank_u.reserve(id2node.size());
    for (const int nid : topo) {
        auto it = id2node.find(nid);
        if (it != id2node.end() && it->second) rank_u[nid] = static_cast<double>(it->second->exec_time());
        else rank_u[nid] = 0.0;
    }
    // 逆拓扑计算：rank_u(n) = exec(n) + max_s( transfer(n) + rank_u(s) )
    for (auto it = topo.rbegin(); it != topo.rend(); ++it) {
        int u = *it;
        const Node* node = id2node.count(u) ? id2node.at(u) : nullptr;
        if (!node) continue;
        double base = static_cast<double>(node->exec_time());
        double best_succ = 0.0;
        auto jt = adj.find(u);
        if (jt != adj.end()) {
            for (int v : jt->second) {
                double cand = static_cast<double>(node->transfer_time()) + (rank_u.count(v) ? rank_u[v] : 0.0);
                if (cand > best_succ) best_succ = cand;
            }
        }
        rank_u[u] = base + best_succ;
    }
    return rank_u;
}

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

    // 新增：长运行优先的贪心（按 exec_time 降序的优先级拓扑），卡分配用 EFT
    {
        std::unordered_map<int,double> long_prio;
        long_prio.reserve(node_ids.size());
        for (int nid : node_ids) {
            auto it = id2node.find(nid);
            if (it != id2node.end() && it->second) {
                long_prio[nid] = -static_cast<double>(it->second->exec_time());
            } else {
                long_prio[nid] = 0.0;
            }
        }
        auto long_first = TopoByPriorityWithEFT(indeg0, adj, id2node, card_num, rng, long_prio, nullptr);
        if (!long_first.empty()) population.push_back(std::move(long_first));
    }

    // 新增：HEFT upward-rank 初始个体（按关键路径优先），卡分配用 EFT
    {
        auto rank_u = ComputeUpwardRank(indeg0, adj, id2node);
        std::unordered_map<int,double> heft_prio;
        heft_prio.reserve(rank_u.size());
        for (const auto& kv : rank_u) heft_prio[kv.first] = -kv.second;
        auto heft_indiv = TopoByPriorityWithEFT(indeg0, adj, id2node, card_num, rng, heft_prio, nullptr);
        if (!heft_indiv.empty()) population.push_back(std::move(heft_indiv));
    }

    // 其余用启发式 + 随机噪声生成，卡分配用 EFT
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
        auto indiv = TopoByPriorityWithEFT(indeg0, adj, id2node, card_num, rng, prio, nullptr);
        if (indiv.empty()) return {}; // 有环，无法调度
        population.push_back(std::move(indiv));
    }
    return population;
}