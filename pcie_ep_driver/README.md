# Xilinx Spartan-6 PCIe Endpoint Linux Driver

Generic Linux kernel driver for Xilinx Spartan-6 FPGA integrated PCIe Endpoint block.

## Directory Structure

```
pcie_ep_driver/
├── xilinx_pcie_ep.c    # Main kernel module source
├── xilinx_pcie_ep.h    # Public userspace header (ioctl definitions)
├── Makefile            # Out-of-tree module makefile
├── Kconfig             # Kernel config fragment
├── test/
│   └── test_app.c     # Simple test userspace application
└── udev/
    └── 99-xilinx-pcie-ep.rules  # udev rules for device node
```

## Requirements

- Linux kernel 3.x or later
- Kernel development headers installed
- Xilinx Spartan-6 FPGA with PCIe Endpoint design

## Building

```bash
# Build the kernel module
make

# Or with specific kernel tree
KDIR=/path/to/kernel/source make
```

## Loading

```bash
# Load the module
sudo insmod xilinx_pcie_ep.ko

# Or with debug messages
sudo insmod xilinx_pcie_ep.ko debug=1
```

## Device Access

After loading, device appears as `/dev/xilinx_ep0` (or `xilinx_ep1`, etc. for multiple devices).

Check dmesg for probe info:
```bash
dmesg | grep xilinx
```

## Userspace API

See `xilinx_pcie_ep.h` for ioctl definitions. Test application in `test/test_app.c`.

## Register Map (BAR0)

| Offset | Register | Description |
|--------|----------|-------------|
| 0x00 | CTRL | Control register |
| 0x04 | STATUS | Status register |
| 0x08 | RING_BASE | DMA ring base (low 32-bit) |
| 0x0C | RING_BASE_HI | DMA ring base (high 32-bit) |
| 0x10 | RING_SIZE | DMA ring size |
| 0x14 | DEV_CMD | Device command |
| 0x18 | DEV_STATUS | Device status |
| 0x1C | INT_ENABLE | Interrupt enable |
| 0x20 | INT_STATUS | Interrupt status |
| 0x24 | FLR | Function level reset |

**Note:** These are generic offsets. Update based on your specific FPGA design.

## License

GPL-2.0