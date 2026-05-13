# Mailbox Framework 开发文档

本目录包含共享内存邮箱框架的完整开发文档。

## 文档列表

| 章节 | 文件 | 内容 |
|------|------|------|
| 概述 | [README.md](README.md) | 框架概览和快速开始 |
| 架构 | [01_architecture.md](01_architecture.md) | 整体架构和数据流 |
| 内核 | [02_kernel_module.md](02_kernel_module.md) | 内核模块设计与实现 |
| 共享内存 | [03_shared_memory.md](03_shared_memory.md) | 共享内存布局和 Ring Buffer |
| API | [04_api_reference.md](04_api_reference.md) | 用户态 API 详解 |
| 示例 | [05_examples.md](05_examples.md) | 使用示例和模式 |
| 调试 | [06_debugging.md](06_debugging.md) | 调试方法和工具 |
| 性能 | [07_performance.md](07_performance.md) | 性能优化和 benchmark |

## 阅读顺序

1. [README.md](README.md) - 快速了解
2. [01_architecture.md](01_architecture.md) - 理解整体设计
3. [04_api_reference.md](04_api_reference.md) - 查看 API 细节
4. [05_examples.md](05_examples.md) - 学习使用模式
5. 按需深入各章节

## 代码对照

源代码位于 `../` 目录：
- `kernel/mbox_kern.c` - 内核模块
- `lib/mbox_lib.c` - 用户态库
- `include/mbox.h` - 公开 API

## 反馈

有问题欢迎提交 Issue。