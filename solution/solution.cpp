#include "solution.h"

#include <unordered_map>
#include <algorithm>
#include <random>
#include <chrono>
#include <numeric>
#include "GAConfig.h"
#include "GAInit.h"

std::vector<std::pair<size_t,size_t>> ExecuteOrder(const std::vector<Node*>& all_nodes, int card_num) {
    if (card_num <= 0) return {};

    // 建图：id 映射、入度与邻接
    std::unordered_map<int, const Node*> id2node;
    id2node.reserve(all_nodes.size());
    for (const Node* n : all_nodes) {
        if (n) id2node[n->id()] = n;
    }

    std::unordered_map<int, int> indeg0;
    std::unordered_map<int, std::vector<int>> adj;
    for (const Node* n : all_nodes) {
        if (!n) continue;
        int id = n->id();
        indeg0[id] += 0; // ensure key exists
        for (const Node* pred : n->inputs()) {
            if (pred) {
                indeg0[id]++;
                adj[pred->id()].push_back(id);
            }
        }
    }

    // 节点列表
    std::vector<int> node_ids;
    node_ids.reserve(indeg0.size());
    for (auto& kv : indeg0) node_ids.push_back(kv.first);

    if (node_ids.empty()) return {};

    // 使用内置默认配置，不读取本地文件
    GAConfig cfg;
    std::mt19937 rng(static_cast<unsigned int>(
        (cfg.seed >= 0) ? cfg.seed : std::chrono::high_resolution_clock::now().time_since_epoch().count()));

    // 时间预算：50,000 点 ≈ 1 分钟，按点数线性缩放
    // 从进入 ExecuteOrder 开始计时；达到目标时间（按 50000 节点≈60秒）则早停，单位毫秒
    auto t_start = std::chrono::high_resolution_clock::now();
    long long time_budget_ms = static_cast<long long>(60000.0 * (static_cast<double>(node_ids.size()) / 50000.0));

    // 拓扑排序与卡分配改为调用独立实现

    // 复用转换缓冲，避免每次评估都分配新向量
    std::vector<std::pair<size_t,size_t>> order_buf;
    order_buf.reserve(node_ids.size());
    auto evaluate = [&](const std::vector<std::pair<int,int>>& orderInt) {
        order_buf.resize(orderInt.size());
        for (size_t i = 0; i < orderInt.size(); ++i) {
            order_buf[i] = { static_cast<size_t>(orderInt[i].first), static_cast<size_t>(orderInt[i].second) };
        }
        return CalcTotalDuration(order_buf, all_nodes, static_cast<size_t>(card_num));
    };

    // GA 参数来自配置（不再使用轮次，仅保留时间退出）
    const int pop_size = cfg.pop_size;
    const double mutation_rate = cfg.mutation_rate;
    const int tournament_k = cfg.tournament_k;
    // 预分配下一代与适应度缓冲，减少每轮内部分配
    std::vector<std::vector<std::pair<int,int>>> next;
    next.reserve(pop_size);
    std::vector<long long> fitness_next;
    fitness_next.reserve(pop_size);
    auto schedule_equal = [](const std::vector<std::pair<int,int>>& a,
                             const std::vector<std::pair<int,int>>& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
    };

    // 初始种群从独立文件生成（启发式优先级 + 少量随机扰动）
    auto population = InitializePopulation(node_ids, indeg0, adj, id2node, card_num, pop_size, rng);
    if (population.empty()) return {};

    // 适应度缓存：减少对 CalcTotalDuration 的重复调用
    std::vector<long long> fitness(population.size());
    for (size_t i = 0; i < population.size(); ++i) {
        fitness[i] = evaluate(population[i]);
    }
    int best_idx = static_cast<int>(std::min_element(fitness.begin(), fitness.end()) - fitness.begin());
    auto best = population[best_idx];
    long long best_fit = fitness[best_idx];
    // 设定目标时间为初始贪心解的结束时间，并要求达到其 90%
    long long target_time = fitness[0];
    long long required_time = static_cast<long long>(target_time * 0.9);

    // 锦标赛选择返回索引，使用缓存适应度比较
    auto tournament_select_idx = [&](const std::vector<std::vector<std::pair<int,int>>>& pop,
                                     const std::vector<long long>& fit) {
        std::uniform_int_distribution<int> idx_dist(0, static_cast<int>(pop.size()) - 1);
        int winner = idx_dist(rng);
        long long winner_fit = fit[winner];
        for (int i = 1; i < tournament_k; ++i) {
            int cand = idx_dist(rng);
            if (fit[cand] < winner_fit) { winner = cand; winner_fit = fit[winner]; }
        }
        return winner;
    };

    auto crossover = [&](const std::vector<std::pair<int,int>>& A,
                        const std::vector<std::pair<int,int>>& B) -> std::vector<std::pair<int,int>> {
        if (A.empty() || B.empty() || A.size() != B.size()) return {};
        // 基于位置平均的优先级，然后依优先级做拓扑排序；卡继承自对应父代（随机选）
        std::unordered_map<int, double> prio;
        for (size_t i = 0; i < A.size(); ++i) prio[A[i].first] += static_cast<double>(i);
        for (size_t i = 0; i < B.size(); ++i) prio[B[i].first] += static_cast<double>(i);
        for (auto& kv : prio) kv.second /= 2.0; // 平均位置

        std::unordered_map<int,int> inherit_cards;
        std::uniform_int_distribution<int> coin(0,1);
        for (size_t i = 0; i < A.size(); ++i) inherit_cards[A[i].first] = A[i].second;
        for (size_t i = 0; i < B.size(); ++i) {
            int nid = B[i].first;
            int chosen = (coin(rng) == 0) ? inherit_cards[nid] : B[i].second;
            inherit_cards[nid] = chosen;
        }
        auto child = TopoByPriorityWithEFT(indeg0, adj, id2node, card_num, rng, prio, &inherit_cards);
        // 对子代进行小比例 EFT 卡局部优化，进一步降低时长但控制耗时
        child = RefineCardsByEFT(child, id2node, card_num, 0.2, rng);
        return child;
    };

    auto mutate = [&](std::vector<std::pair<int,int>>& indiv) -> bool {
        std::uniform_real_distribution<double> prob(0.0, 1.0);
        bool changed = false;
        if (prob(rng) < mutation_rate) {
            changed = true;
            // 轻微调整优先级（通过位置噪声重建拓扑）和随机修改部分卡分配
            std::uniform_real_distribution<double> prio_noise(0.0, 1.0);
            std::unordered_map<int, double> prio;
            for (size_t i = 0; i < indiv.size(); ++i) prio[indiv[i].first] = static_cast<double>(i) + prio_noise(rng) * 0.5;
            std::unordered_map<int,int> inherit_cards;
            for (const auto& p : indiv) inherit_cards[p.first] = p.second;
            indiv = TopoByPriorityWithEFT(indeg0, adj, id2node, card_num, rng, prio, &inherit_cards);
            // 局部 EFT 精修代替大量随机卡重分配，降开销增质量
            indiv = RefineCardsByEFT(indiv, id2node, card_num, 0.15, rng);
        }
        return changed;
    };

    // 进化（仅按时间终止）
    while (true) {
        long long elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - t_start).count();
        if (elapsed_ms >= time_budget_ms) break;
        // 如果已满足目标阈值，提前结束
        if (best_fit <= required_time) break;
        // 子代集合（复用缓冲）
        next.clear();
        fitness_next.clear();

        // 精英保留（基于适应度缓存，避免全排序）
        std::vector<int> idx(population.size());
        std::iota(idx.begin(), idx.end(), 0);
        auto compIdx = [&](int a, int b){ return fitness[a] < fitness[b]; };
        if (!idx.empty()) {
            if (idx.size() >= 2) {
                std::nth_element(idx.begin(), idx.begin() + 2, idx.end(), compIdx);
                next.push_back(population[idx[0]]);
                fitness_next.push_back(fitness[idx[0]]);
                if (pop_size > 1) {
                    next.push_back(population[idx[1]]);
                    fitness_next.push_back(fitness[idx[1]]);
                }
            } else {
                next.push_back(population[idx[0]]);
                fitness_next.push_back(fitness[idx[0]]);
            }
        }

        while (static_cast<int>(next.size()) < pop_size) {
            int parentA_idx = tournament_select_idx(population, fitness);
            int parentB_idx = tournament_select_idx(population, fitness);
            auto child = crossover(population[parentA_idx], population[parentB_idx]);
            if (child.empty()) child = population[parentA_idx]; // 保护：若失败则继承父代
            bool changed = mutate(child);
            long long child_fit;
            if (!changed) {
                if (schedule_equal(child, population[parentA_idx])) {
                    child_fit = fitness[parentA_idx];
                } else if (schedule_equal(child, population[parentB_idx])) {
                    child_fit = fitness[parentB_idx];
                } else {
                    child_fit = evaluate(child);
                }
            } else {
                child_fit = evaluate(child);
            }
            next.push_back(std::move(child));
            fitness_next.push_back(child_fit);
        }

        population.swap(next);
        fitness.swap(fitness_next);
        int cur_best_idx = static_cast<int>(std::min_element(fitness.begin(), fitness.end()) - fitness.begin());
        if (fitness[cur_best_idx] < best_fit) {
            best_fit = fitness[cur_best_idx];
            best = population[cur_best_idx];
        }
        // 满足目标阈值则提前结束
        if (best_fit <= required_time) break;
    }

    // 将最终 best 转换为 size_t 类型返回
    std::vector<std::pair<size_t,size_t>> result;
    result.reserve(best.size());
    for (const auto& p : best) result.emplace_back(static_cast<size_t>(p.first), static_cast<size_t>(p.second));
    return result;
}