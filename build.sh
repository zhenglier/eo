#!/bin/bash
set -e

rm -rf build 
rm -rf output
mkdir build
mkdir output
cd build
cmake ..
make
cp execution_order ../output
cd ..
cp test_cases/example*.txt output
