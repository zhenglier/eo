#include "solution.h"

#include <unordered_map>
#include <limits>
#include <algorithm>
#include <random>
#include <chrono>
#include <string>
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

    // 加载配置并初始化随机源
    GAConfig cfg = LoadGAConfig("ga_config.txt");
    std::mt19937 rng(static_cast<unsigned int>(
        (cfg.seed >= 0) ? cfg.seed : std::chrono::high_resolution_clock::now().time_since_epoch().count()));

    // 时间预算：50,000 点 ≈ 1 分钟，按点数线性缩放
    auto t_start = std::chrono::high_resolution_clock::now();
    double time_budget_seconds = 60.0 * (static_cast<double>(node_ids.size()) / 50000.0);

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

    // 锦标赛选择返回索引，使用缓存适应度比较
    auto tournament_select_idx = [&](const std::vector<std::vector<std::pair<int,int>>>& pop,
                                     const std::vector<long long>& fit) {
        std::uniform_int_distribution<int> idx_dist(0, static_cast<int>(pop.size()) - 1);
        int winner = idx_dist(rng);
        long long winner_fit = fit[winner];
        for (int i = 1; i < tournament_k; ++i) {
            int cand = idx_dist(rng);
            if (fit[cand] < winner_fit) { winner = cand; winner_fit = fit[cand]; }
        }
        return winner;
    };

    auto crossover = [&](const std::vector<std::pair<int,int>>& A,
                         const std::vector<std::pair<int,int>>& B) {
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
        return TopoByPriority(indeg0, adj, card_num, rng, prio, &inherit_cards);
    };

    auto mutate = [&](std::vector<std::pair<int,int>>& indiv) {
        std::uniform_real_distribution<double> prob(0.0, 1.0);
        if (prob(rng) < mutation_rate) {
            // 轻微调整优先级（通过位置噪声重建拓扑）和随机修改部分卡分配
            std::uniform_real_distribution<double> prio_noise(0.0, 1.0);
            std::unordered_map<int, double> prio;
            for (size_t i = 0; i < indiv.size(); ++i) prio[indiv[i].first] = static_cast<double>(i) + prio_noise(rng) * 0.5;
            std::unordered_map<int,int> inherit_cards;
            for (const auto& p : indiv) inherit_cards[p.first] = p.second;
            indiv = TopoByPriority(indeg0, adj, card_num, rng, prio, &inherit_cards);
            // 随机挑选若干节点重新分配卡
            std::uniform_int_distribution<int> card_dist(0, std::max(0, card_num - 1));
            for (auto& g : indiv) if (prob(rng) < 0.15) g.second = card_dist(rng);
        }
    };

    // 进化（仅按时间终止）
    while (true) {
        double elapsed_sec = std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - t_start).count();
        if (elapsed_sec >= time_budget_seconds) break;
        // 子代集合
        std::vector<std::vector<std::pair<int,int>>> next;
        next.reserve(pop_size);

        // 精英保留（基于适应度缓存，避免全排序）
        std::vector<int> idx(population.size());
        std::iota(idx.begin(), idx.end(), 0);
        auto compIdx = [&](int a, int b){ return fitness[a] < fitness[b]; };
        if (!idx.empty()) {
            if (idx.size() >= 2) {
                std::nth_element(idx.begin(), idx.begin() + 2, idx.end(), compIdx);
                next.push_back(population[idx[0]]);
                if (pop_size > 1) next.push_back(population[idx[1]]);
            } else {
                next.push_back(population[idx[0]]);
            }
        }

        while (static_cast<int>(next.size()) < pop_size) {
            int parentA_idx = tournament_select_idx(population, fitness);
            int parentB_idx = tournament_select_idx(population, fitness);
            auto child = crossover(population[parentA_idx], population[parentB_idx]);
            if (child.empty()) child = population[parentA_idx]; // 保护：若失败则继承父代
            mutate(child);
            next.push_back(std::move(child));
        }

        // 计算子代适应度并更新当前种群与历史最优（复用精英适应度）
        std::vector<long long> fitness_next(next.size());
        size_t elite_count = 0;
        if (!idx.empty()) {
            fitness_next[0] = fitness[idx[0]];
            elite_count = 1;
            if (pop_size > 1 && idx.size() >= 2 && next.size() >= 2) {
                fitness_next[1] = fitness[idx[1]];
                elite_count = 2;
            }
        }
        for (size_t i = elite_count; i < next.size(); ++i) {
            fitness_next[i] = evaluate(next[i]);
        }
        population = std::move(next);
        fitness = std::move(fitness_next);
        int cur_best_idx = static_cast<int>(std::min_element(fitness.begin(), fitness.end()) - fitness.begin());
        if (fitness[cur_best_idx] < best_fit) {
            best_fit = fitness[cur_best_idx];
            best = population[cur_best_idx];
        }
    }

    // 将最终 best 转换为 size_t 类型返回
    std::vector<std::pair<size_t,size_t>> result;
    result.reserve(best.size());
    for (const auto& p : best) result.emplace_back(static_cast<size_t>(p.first), static_cast<size_t>(p.second));
    return result;
}