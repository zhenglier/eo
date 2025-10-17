#ifndef EXECUTION_ORDER_TEST_CASES_H
#define EXECUTION_ORDER_TEST_CASES_H

#include "../node.h"
#include "../solution/solution.h"
#include "../utils.h"
#include <cstdlib>
#include <map>
#include <vector>

class TestCase {
public:
  explicit TestCase(const std::string &file_name) {
    char real_path[4096] = {0};
    if (file_name.length() >= 4096 ||
        realpath(file_name.c_str(), real_path) == nullptr) {
      std::cerr << "Invalid file_name: " << file_name;
      exit(1);
    }
    file_name_ = file_name;
    std::tie(all_nodes_, card_num_) = GetInputs(real_path);
  }

  void Solve() {
    auto t0 = std::chrono::high_resolution_clock::now();
    auto node_execute_order = ExecuteOrder(all_nodes_, card_num_);
    auto cost_time =
        (std::chrono::high_resolution_clock::now() - t0).count() * 1e-9;
    auto ans = GetResult(card_num_, all_nodes_, node_execute_order);
    if (ans == -1) {
      std::cerr << "Invalid Answer." << std::endl;
      exit(1);
    }
    std::cout << "Case file name: " << file_name_
              << " , get result time cost: " << cost_time
              << " , ops execution time cost: " << ans
              << std::endl;
  }

private:
  std::vector<Node *> all_nodes_;
  std::string output_name_;
  int card_num_;
  std::string file_name_;
};

void TestCaseExecute(std::string index) {
  std::string filename = "example" + index + ".txt";
  TestCase t(filename);
  t.Solve();
}

#endif // EXECUTION_ORDER_TEST_CASES_H
