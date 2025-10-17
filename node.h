#ifndef EXECUTION_ORDER_NODE_H
#define EXECUTION_ORDER_NODE_H
#include <string>
#include <vector>

class Node {
public:
  Node() = default;
  Node(size_t id, const std::vector<Node *> &inputs, long exec_time,
       long transfer_time)
      : id_(id), inputs_(inputs), exec_time_(exec_time),
        transfer_time_(transfer_time) {}

  const std::vector<Node *> &inputs() const { return inputs_; }

  size_t id() const { return id_; }

  long exec_time() const { return exec_time_; }

  long transfer_time() const { return transfer_time_; }

private:
  size_t id_;
  std::vector<Node *> inputs_;
  long exec_time_;
  long transfer_time_;
};
#endif // EXECUTION_ORDER_NODE_H
