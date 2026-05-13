# 第八章：调试技巧

## 8.1 printk 和 dmesg

内核调试最基本工具：`printk()` 输出到内核日志，`dmesg` 查看。

```c
// 调试级别
printk(KERN_DEBUG "ep: debug message\n");    // DEBUG 级别
printk(KERN_INFO "ep: info message\n");      // INFO 级别
printk(KERN_WARNING "ep: warning\n");        // WARNING 级别
printk(KERN_ERR "ep: error: %d\n", ret);     // ERROR 级别
printk(KERN_CRIT "ep: critical\n");           // CRITICAL 级别

// 动态调试控制（需要 DEBUG 定义）
#ifdef DEBUG
#define ep_dbg(dev, fmt, ...) dev_dbg(dev, fmt, ##__VA_ARGS__)
#else
#define ep_dbg(dev, fmt, ...) no_printk(fmt, ##__VA_ARGS__)
#endif
```

```bash
# 查看驱动输出
dmesg | grep -i xilinx

# 持续监控（实时）
dmesg -w | grep xilinx

# 清空日志后重新加载驱动
dmesg -c
insmod xilinx_pcie_ep.ko
dmesg
```

## 8.2 编译调试

```makefile
# Makefile 中启用调试符号
EXTRA_CFLAGS += -g -DDEBUG

# 启用动态调试（需要内核开启 CONFIG_DYNAMIC_DEBUG）
obj-m := xilinx_pcie_ep.o
ccflags-y := -DDEBUG
```

## 8.3 使用 /proc 和 /sys 调试

```c
// 创建调试文件节点
static int ep_debugfs_init(struct xilinx_ep_dev *ep)
{
    struct dentry *root;

    // 创建 /sys/kernel/debug/xilinx_ep0/
    root = debugfs_create_dir("xilinx_ep", NULL);
    if (!root)
        return -ENOMEM;

    // 创建只读寄存器文件
    debugfs_create_file("registers", 0444, root, ep, &reg_fops);

    // 创建读写统计文件
    debugfs_create_u32("tx_count", 0444, root, &ep->tx_count);
    debugfs_create_u32("rx_count", 0444, root, &ep->rx_count);

    ep->debugfs_root = root;
    return 0;
}

static const struct file_operations reg_fops = {
    .owner = THIS_MODULE,
    .read = debug_reg_read,
};

static ssize_t debug_reg_read(struct file *filp, char __user *buf,
                               size_t count, loff_t *ppos)
{
    struct xilinx_ep_dev *ep = filp->private_data;
    char tmp[256];
    int len;

    len = snprintf(tmp, sizeof(tmp),
                   "CTRL: 0x%08x\nSTATUS: 0x%08x\n",
                   ep_read32(ep, REG_CTRL),
                   ep_read32(ep, REG_STATUS));

    return simple_read_from_buffer(buf, count, ppos, tmp, len);
}
```

```bash
# 查看调试信息
cat /sys/kernel/debug/xilinx_ep0/registers
cat /sys/kernel/debug/xilinx_ep0/tx_count

# 列出所有 debugfs 文件
ls -la /sys/kernel/debug/
```

## 8.4 使用 kgdb 调试

```bash
# 1. 内核启动时加 kgdbwait 参数
# GRUB: kgdbwait kgdb8250=io,0x3f8 if in doubt

# 2. 在主机上启动 gdb
gdb vmlinux

# 3. 连接目标机
target remote /dev/ttyS0

# 4. 设置断点
break xilinx_ep_probe
continue

# 5. 目标机触发断点后，可以检查变量、单步执行
print ep
info locals
next
```

## 8.5 PCIe 配置空间读取

```bash
# 使用 setpci 读取配置空间
setpci -s 01:00.0 04.w   # 读 Command register
setpci -s 01:00.0 10.l   # 读 BAR0 (32-bit)
setpci -s 01:00.0 14.l   # 读 BAR1

# 写配置空间
setpci -s 01:00.0 04.w=0x07  # 启用 Memory、Master、IO Space

# 读 Capabilities
setpci -s 01:00.0 34.b   # 读 Capabilities Pointer
```

## 8.6 读取 BAR 资源

```bash
# 查看所有 BAR 资源
cat /sys/bus/pci/devices/0000:01:00.0/resource*

# 查看 BAR 起始地址
cat /sys/bus/pci/devices/0000:01:00.0/resource

# 读 bar0 的前 256 字节（配置空间镜像）
dd if=/sys/bus/pci/devices/0000:01:00.0/resource0 of=/tmp/bar0.bin bs=1 count=256

# 用 hexdump 查看
hexdump -C /tmp/bar0.bin
```

## 8.7 中断调试

```bash
# 查看中断分配
cat /proc/interrupts | grep -i xilinx

# 查看 MSI 中断
cat /proc/interrupts | grep MSI

# 查看设备 IRQ
cat /sys/bus/pci/devices/0000:01:00.0/irq

# 触发中断测试（需要硬件配合）
# 写一个用户程序循环触发 DMA，观察中断计数变化
```

## 8.8 DMA 调试

```c
// 在 DMA 操作前后打印关键信息
dev_info(&ep->pdev->dev, "DMA: buf=%pad, size=%zu\n",
         &ep->dma_phys, ep->dma_size);

// 检查描述符
dev_dbg(&ep->pdev->dev, "Desc: src=0x%llx, dst=0x%llx, cnt=%d\n",
        desc->src_addr, desc->dst_addr, desc->count);

// 检查状态机
dev_dbg(&ep->pdev->dev, "DMA state: ctrl=0x%08x, status=0x%08x\n",
        ep_read32(ep, REG_CTRL), ep_read32(ep, REG_STATUS));
```

## 8.9 使用 dev_err/dev_info 替代 printk

```c
// 推荐用法（自动添加设备前缀）
dev_err(&ep->pdev->dev, "Failed to enable device: %d\n", ret);
dev_info(&ep->pdev->dev, "Mapped BAR0 at %p\n", ep->bar0);
dev_warn(&ep->pdev->dev, "DMA allocation failed, using fallback\n");

// vs 旧式 printk
printk(KERN_ERR "xilinx_ep: error %d\n", ret);  // 不推荐
```

## 8.10 内存泄漏检测

```c
// 在 remove 中验证所有资源已释放
static void xilinx_ep_remove(struct pci_dev *pdev)
{
    struct xilinx_ep_dev *ep = pci_get_drvdata(pdev);

    // 检查引用计数
    if (atomic_read(&ep->refcount) != 0) {
        dev_err(&pdev->dev, "Device still open, refcount=%d\n",
                atomic_read(&ep->refcount));
    }

    // 检查是否有 pending DMA
    if (ep_read32(ep, REG_STATUS) & STATUS_RUNNING) {
        dev_warn(&pdev->dev, "DMA engine still running\n");
        ep_write32(ep, REG_CTRL, 0);
    }

    // ... 释放资源 ...
}
```

## 8.11 模块参数调试

```c
// 定义可调节的调试参数
static int debug_level = 1;
module_param(debug_level, int, 0644);
MODULE_PARM_DESC(debug_level, "Debug level (0=off, 1=info, 2=debug)");

#define ep_debug(ep, fmt, ...) do { \
    if (debug_level >= 2) \
        dev_dbg(&(ep)->pdev->dev, fmt, ##__VA_ARGS__); \
} while (0)

#define ep_info(ep, fmt, ...) do { \
    if (debug_level >= 1) \
        dev_info(&(ep)->pdev->dev, fmt, ##__VA_ARGS__); \
} while (0)
```

```bash
# 加载时设置调试级别
insmod xilinx_pcie_ep.ko debug_level=2

# 查看当前参数
cat /sys/module/xilinx_pcie_ep/parameters/debug_level
```

## 8.12 内核配置选项

```bash
# 调试相关内核选项（make menuconfig）
CONFIG_DEBUG_KERNEL=y           # 启用内核调试
CONFIG_DEBUG_INFO=y             # 编译包含调试符号
CONFIG_DYNAMIC_DEBUG=y          # 动态调试（pr_debug）
CONFIG_DEBUG_DRIVER=y           # 驱动调试信息
CONFIG_DEBUG_SLAB=y             # 内存分配调试
CONFIG_DEBUG_SPINLOCK=y         # 自旋锁调试
CONFIG_DEBUG_MUTEXES=y          # 互斥锁调试
CONFIG_DEBUG_RT_MUTEXES=y       # RT 互斥锁调试
```

## 8.13 常见问题排查表

| 现象 | 原因 | 解决方法 |
|------|------|----------|
| probe 没被调用 | Vendor/Device ID 不匹配 | 检查 ID 表和 `lspci -nn` |
| BAR 映射返回 NULL | BAR 未使能 | 用 `setpci` 检查 Command register |
| 中断不触发 | MSI 未使能或 BAR 冲突 | 检查配置空间 Cap 指针 |
| DMA 不工作 | 没设 Bus Master | 调用 `pci_set_master()` |
| 数据错乱 | cache 未同步 | 用 `dma_sync_*()` |
| mmap 失败 | 偏移/大小参数错 | 检查 `vm_pgoff` 和 size |
| 设备节点不存在 | udev 规则缺失 | 检查 `/etc/udev/rules.d/` |

## 8.14 下一步

下一章：[第九章：热插拔与电源管理](./09_hotplug.md)