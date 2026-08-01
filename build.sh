#!/bin/bash
set -x

# 检查是否有 build 目录存在，如果存在则清除，保证全新配置
if [ -d "build" ]; then
    rm -rf build
fi

mkdir build

cd build

# 生成 Makefile
cmake .. -DCMAKE_BUILD_TYPE=DEBUG

# 编译项目
make -j4
