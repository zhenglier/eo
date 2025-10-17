#include "test_cases/test_cases.h"

int main(int argc, char* argv[]) {
  if (argc != 2) {
    exit(1);
  }
  TestCaseExecute(argv[1]);
  return 0;
}
