#ifndef EXECUTION_ORDER_UTILS_H
#define EXECUTION_ORDER_UTILS_H
#include "node.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using int64 = long long;

std::tuple<std::vector<Node *>, size_t>
GetInputs(const std::string &file_name) {
  std::ifstream file(file_name);
  size_t card_num;
  if (!file.is_open()) {
    std::cerr << "Unable to open file";
    exit(1);
  }
  std::string line;
  getline(file, line);
  std::istringstream first_iss(line);
  first_iss >> card_num;
  std::vector<Node *> all_nodes;
  long idx, input_size, exec_time, transfer_time;
  long start_idx = 0;
  while (getline(file, line)) {
    std::istringstream iss(line);
    iss >> idx >> input_size;
    if (idx != start_idx) {
      std::cerr << "Invalid node id: " << idx;
      exit(1);
    }
    if (input_size < 0 || input_size > all_nodes.size()) {
      std::cerr << "Invalid input size: " << input_size
                << " , for node: " << idx << std::endl;
      exit(1);
    }
    std::vector<Node *> inputs;
    inputs.reserve(input_size);
    for (size_t i = 0; i < input_size; ++i) {
      size_t input_id;
      iss >> input_id;
      if (input_id >= all_nodes.size()) {
        std::cerr << "Invalid input id: " << input_id << ", for node: " << idx
                  << std::endl;
        exit(1);
      }
      inputs.push_back(all_nodes[input_id]);
    }
    iss >> exec_time >> transfer_time;
    Node *new_node = new Node(idx, inputs, exec_time, transfer_time);
    start_idx++;
    all_nodes.emplace_back(new_node);
  }
  file.close();
  return std::make_tuple(all_nodes, card_num);
}

int64 GetResult(size_t card_num, const std::vector<Node *> &nodes,
                const std::vector<std::pair<size_t, size_t>> &order_list) {
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
    // Check input exec info
    auto inputs = nodes[cur_op_id]->inputs();
    auto start_time = card_ready_time[cur_card_id];
    // {input finish time, input transfer time}
    std::vector<std::pair<int64, int64>> input_record_times;
    for (const auto &input : inputs) {
      auto input_id = input->id();
      auto input_finish_time = op_exec_info[input_id].second;
      if (input_finish_time == -1) {
        std::cerr << "Invalid order, inputs not finshed, node id: " << cur_op_id
                  << " , input id: " << input_id << std::endl;
        return -1;
      }

      // Input execute finished, need transfer
      if (op_exec_info[input_id].first != cur_card_id) {
        input_record_times.push_back(
            {input_finish_time, input->transfer_time()});
        // Avoid repeated input
        op_exec_info[input_id].first = cur_card_id;
      }
    }
    if (!input_record_times.empty()) {
      std::sort(input_record_times.begin(), input_record_times.end(),
                [](auto &x, auto &y) { return x.first < y.first; });
      for (auto input_record_time : input_record_times) {
        start_time = std::max(start_time, input_record_time.first) +
                     input_record_time.second;
      }
    }
    for (const auto &input : inputs) {
      auto input_id = input->id();
      op_exec_info[input_id].second = start_time;
    }
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

#endif // EXECUTION_ORDER_UTILS_H
