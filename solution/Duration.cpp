#include "solution.h"
#include "utils.h"

#include <unordered_map>
#include <algorithm>
#include <tuple>

int64 CalcTotalDuration(const std::vector<std::pair<size_t, size_t>> &order_list,
                        const std::vector<Node *> &nodes,
                        size_t card_num) {
    auto total_size = nodes.size();
    if (card_num < 0) {
        std::cerr << "Invalid card num: " << card_num << std::endl;
        return -1;
    }
    if (order_list.size() != total_size) {
        std::cerr << "Invalid order list size: " << order_list.size()
                  << " , need equal to: " << total_size << std::endl;
        return -1;
    }
    std::vector<int64> card_ready_time(card_num, 0);
    std::vector<int64> inbound_ready_time(card_num, 0); // 入站迁移资源
    // {op finish card, finish time}
    std::vector<std::pair<size_t, int64>> op_exec_info(total_size, {0, -1});
    for (const auto &order : order_list) {
        auto cur_op_id = order.first;
        auto cur_card_id = order.second;
        // Invalid node id
        if (cur_op_id >= total_size) {
            std::cerr << "Invalid order, node id out of range: " << cur_op_id
                      << std::endl;
            return -1;
        }
        // Invalid card id
        if (cur_card_id >= card_num) {
            std::cerr << "Invalid order, card id out of range: " << cur_card_id
                      << std::endl;
            return -1;
        }
        // Repeat node id
        if (op_exec_info[cur_op_id].second != -1) {
            std::cerr << "Invalid order, node already executed: " << cur_op_id
                      << std::endl;
            return -1;
        }
        // Check inputs and schedule early inbound transfers
        const auto &inputs = nodes[cur_op_id]->inputs();
        int64 local_inputs_max_ft = 0;
        std::vector<std::tuple<int64, int64, size_t>> cross_inputs; // {finish_time, transfer_time, input_id}
        cross_inputs.reserve(inputs.size());
        for (const auto &input : inputs) {
            auto input_id = input->id();
            auto input_finish_time = op_exec_info[input_id].second;
            if (input_finish_time == -1) {
                std::cerr << "Invalid order, inputs not finshed, node id: " << cur_op_id
                          << " , input id: " << input_id << std::endl;
                return -1;
            }
            if (op_exec_info[input_id].first == cur_card_id) {
                local_inputs_max_ft = std::max(local_inputs_max_ft, input_finish_time);
            } else {
                cross_inputs.emplace_back(input_finish_time, input->transfer_time(), input_id);
            }
        }
        std::sort(cross_inputs.begin(), cross_inputs.end(),
                  [](const std::tuple<int64,int64,size_t> &x, const std::tuple<int64,int64,size_t> &y){ return std::get<0>(x) < std::get<0>(y); });
        int64 last_transfer_arrival = 0;
        for (const auto &ci : cross_inputs) {
            int64 start_transfer = std::max(std::get<0>(ci), inbound_ready_time[cur_card_id]);
            int64 arrival = start_transfer + std::get<1>(ci);
            inbound_ready_time[cur_card_id] = arrival;
            last_transfer_arrival = arrival;
            // Mark this input as now present on cur_card_id to avoid duplicate transfers later
            op_exec_info[std::get<2>(ci)].first = cur_card_id;
        }
        int64 start_time = std::max(card_ready_time[cur_card_id], std::max(local_inputs_max_ft, last_transfer_arrival));
        auto end = start_time + nodes[cur_op_id]->exec_time();
        op_exec_info[cur_op_id] = {cur_card_id, end};
        card_ready_time[cur_card_id] = end;
    }

    int64 execute_time = 0;
    for (int card = 0; card < card_num; ++card) {
        execute_time = std::max(execute_time, card_ready_time[card]);
    }
    return execute_time;
}