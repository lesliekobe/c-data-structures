/*
 * pcie_map.h - PCIe memory map access definitions
 *
 * For Xilinx Spartan-6 PCIe Endpoint or similar devices.
 */

#ifndef PCIe_MAP_H_
#define PCIe_MAP_H_

#include <stdint.h>

/* PCIe memory mapping interface */
typedef struct {
    uintptr_t bar0_virt;   /* BAR0 virtual address */
    uint64_t  bar0_phys;   /* BAR0 physical address */
    size_t    bar0_len;    /* BAR0 length */

    uintptr_t bar1_virt;   /* BAR1 virtual address */
    uint64_t  bar1_phys;   /* BAR1 physical address */
    size_t    bar1_len;    /* BAR1 length */

    int        fd;         /* /dev/mem file descriptor */
} pcie_map_t;

/* Register access helpers */
static inline uint32_t pcie_read32(uintptr_t base, uint32_t offset) {
    return *(volatile uint32_t *)(base + offset);
}

static inline void pcie_write32(uintptr_t base, uint32_t offset, uint32_t val) {
    *(volatile uint32_t *)(base + offset) = val;
}

static inline uint64_t pcie_read64(uintptr_t base, uint32_t lo, uint32_t hi) {
    uint32_t low = pcie_read32(base, lo);
    uint32_t high = pcie_read32(base, hi);
    return ((uint64_t)high << 32) | low;
}

static inline void pcie_write64(uintptr_t base, uint32_t lo, uint32_t hi, uint64_t val) {
    pcie_write32(base, lo, (uint32_t)(val & 0xFFFFFFFF));
    pcie_write32(base, hi, (uint32_t)(val >> 32));
}

/* PCIe map/unmap functions */
int pcie_map_open(pcie_map_t *map, uint16_t vendor_id, uint16_t device_id);
int pcie_map_close(pcie_map_t *map);

/* Common Xilinx Spartan-6 registers (example offsets) */
#define REG_CTRL         0x00
#define REG_STATUS       0x04
#define REG_RING_BASE_LO 0x08
#define REG_RING_BASE_HI 0x0C
#define REG_RING_SIZE    0x10
#define REG_INT_ENABLE   0x1C
#define REG_INT_STATUS   0x20

/* Control register bits */
#define CTRL_START   (1 << 0)
#define CTRL_STOP    (1 << 1)
#define CTRL_RESET   (1 << 2)
#define CTRL_IRQ_EN  (1 << 3)

/* Status register bits */
#define STATUS_IDLE       (1 << 0)
#define STATUS_RUNNING    (1 << 1)
#define STATUS_ERROR      (1 << 2)
#define STATUS_DMA_DONE   (1 << 4)
#define STATUS_DMA_ERR    (1 << 5)

#endif /* PCIe_MAP_H_ */