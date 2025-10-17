#!/bin/bash
cd $PWD
bash build.sh

cd output

for i in {0..5}
do
  echo "==========  running $i  =========="
  ./execution_order "$i"
done