# TPS6594-Q1 PMIC Driver 开发文档

本目录包含 TPS6594-Q1 汽车级 PMIC 驱动的完整开发文档。

## 文档列表

| 章节 | 文件 | 内容 |
|------|------|------|
| 概述 | [README.md](README.md) | 快速开始和特性概览 |
| 架构 | [01_architecture.md](01_architecture.md) | 驱动架构和模块设计 |
| 寄存器 | [02_register_map.md](02_register_map.md) | 完整寄存器映射表 |
| API | [03_api_reference.md](03_api_reference.md) | API 参考详解 |
| SafeRTOS | [04_safertos_integration.md](04_safertos_integration.md) | SafeRTOS 集成说明 |
| 示例 | [05_examples.md](05_examples.md) | 使用示例代码 |

## 代码结构

```
tps6594_driver/
├── include/
│   └── tps6594.h           # 公开 API
├── src/
│   ├── tps6594.c           # 核心驱动（平台无关）
│   └── tps6594_safertos.c  # SafeRTOS 集成
├── examples/
│   └── main.c             # 使用示例
└── Makefile
```

## 快速开始

1. 包含头文件：`#include "tps6594.h"`
2. 初始化：`tps6594_safertos_init(&pmic, addr, irq)`
3. 启动顺序：`tps6594_boot_sequence(&pmic, &config)`
4. 运行时控制：`tps6594_regulator_set_voltage()`, `tps6594_regulator_enable()`
5. 休眠：`tps6594_request_sleep()`

## 反馈

有问题欢迎提交 Issue。