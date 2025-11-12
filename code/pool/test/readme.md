# ThreadPoolV2 测试套件

## 概述

这是一个针对自定义线程池 `ThreadPoolV2` 的完整测试套件，验证线程池的基本功能、优先级调度、异常处理等核心特性。

## 功能特性

- ✅ 基础任务执行测试
- ✅ 优先级调度验证（LOW/NORMAL/HIGH/URGENT）
- ✅ 返回值类型测试（int/string/void）
- ✅ 异常处理机制测试
- ✅ 线程安全性验证
- ✅ 性能基准测试

## 快速开始

### 前提条件

- C++17 兼容编译器（g++ 7+ 或 MSVC 2019+）
- 支持 pthread（Linux/Mac）或 Windows线程API

### 编译运行

#### 方法1：直接编译（推荐）
```bash
# Debug版本（包含调试信息）
g++ -std=c++17 -pthread -g -O0 -Wall -Wextra threadPoolV2Test.cpp -o testThreadPool

# Release版本（优化性能）
g++ -std=c++17 -pthread -O2 -DNDEBUG threadPoolV2Test.cpp -o testThreadPool

# 运行测试
./testThreadPool