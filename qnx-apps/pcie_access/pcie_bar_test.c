/*
 * pcie_bar_test.c - PCIe BAR memory access on QNX
 *
 * Demonstrates opening /dev/mem and mapping PCIe BAR space
 * to access FPGA registers directly.
 *
 * This is the QNX-side counterpart to access an FPGA PCIe device
 * (e.g., Xilinx Spartan-6) from user space.
 *
 * Build: qcc -o pcie_bar_test pcie_bar_test.c
 * Run: ./pcie_bar_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <hw/inout.h>

/* For Intel x86 PCI config space access */
#include <pci/pci.h>

#include "../common/pcie_map.h"

/* Function to find and map a PCIe device by vendor/device ID */
int pcie_device_find(uint16_t vendor_id, uint16_t device_id,
                     uint64_t *bar0_phys, size_t *bar0_len,
                     uint64_t *bar1_phys, size_t *bar1_len)
{
    /* QNX uses pci_attach() for PCI access */
    int pci_handle;
    struct pci_dev_info info;
    uint32_t bar_addr[6];
    uint32_t cmd_reg;

    /* Attach to PCI manager */
    pci_handle = pci_attach(0);
    if (pci_handle == -1) {
        fprintf(stderr, "pci_attach failed\n");
        return -1;
    }

    /* Scan for device */
    memset(&info, 0, sizeof(info));
    info.VendorId = vendor_id;
    info.DeviceId = device_id;

    /* Find first matching device */
    if (pci_device_find_info(pci_handle, 0, &info) != 0) {
        fprintf(stderr, "Device %04x:%04x not found\n", vendor_id, device_id);
        pci_detach(pci_handle);
        return -1;
    }

    /* Read BAR addresses */
    memset(bar_addr, 0, sizeof(bar_addr));
    pci_read_config32(pci_handle, info.Bus, info.DevFunc,
                      PCI_BAR0, &bar_addr[0], 6);

    /* Print BAR info */
    printf("Found device at bus %d, devfn %d\n", info.Bus, info.DevFunc);
    for (int i = 0; i < 6; i++) {
        if (bar_addr[i] != 0 && bar_addr[i] != 0xFFFFFFFF) {
            int type = bar_addr[i] & 0x01;
            uint64_t addr = bar_addr[i] & ~0x07;
            printf("  BAR%d: 0x%llx (%s)\n", i, (unsigned long long)addr,
                   type ? "IO" : "MEM");
        }
    }

    /* Enable memory space access in command register */
    pci_read_config32(pci_handle, info.Bus, info.DevFunc, PCI_CMD, &cmd_reg);
    cmd_reg |= PCI_CMD_MEM_EN;
    pci_write_config32(pci_handle, info.Bus, info.DevFunc, PCI_CMD, cmd_reg);

    /* Return BAR0 and BAR1 if present */
    if (bar_addr[0] != 0 && bar_addr[0] != 0xFFFFFFFF) {
        *bar0_phys = bar_addr[0] & ~0x07;
        *bar0_len = 0x10000;  /* Default 64KB for CSR */
    }

    if (bar_addr[1] != 0 && bar_addr[1] != 0xFFFFFFFF) {
        *bar1_phys = bar_addr[1] & ~0x07;
        *bar1_len = 0x100000; /* Default 1MB for DMA */
    }

    pci_detach(pci_handle);
    return 0;
}

/* Map physical address to virtual address */
void *map_physical(uint64_t phys_addr, size_t len)
{
    int fd;
    void *virt;

    fd = open("/dev/mem", O_RDWR);
    if (fd == -1) {
        /* Try /dev/shmem for some systems */
        fd = open("/dev/shmem", O_RDWR);
        if (fd == -1) {
            fprintf(stderr, "open /dev/mem failed: %s\n", strerror(errno));
            return NULL;
        }
    }

    virt = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)phys_addr);
    if (virt == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }

    close(fd);
    return virt;
}

/* Unmap virtual address */
void unmap_physical(void *virt, size_t len)
{
    if (virt) {
        munmap(virt, len);
    }
}

int main(int argc, char *argv[])
{
    pcie_map_t map;
    int ret = EXIT_FAILURE;
    uint32_t val;

    printf("PCIe BAR Access Test (QNX)\n");
    printf("=============================\n\n");

    /* Find Xilinx device (default Spartan-6 ID) */
    uint16_t vendor_id = 0x10EE;
    uint16_t device_id = 0x0007;

    if (argc >= 3) {
        vendor_id = (uint16_t)strtol(argv[1], NULL, 0);
        device_id = (uint16_t)strtol(argv[2], NULL, 0);
    }

    printf("Looking for device %04x:%04x...\n", vendor_id, device_id);

    /* Find and map device */
    memset(&map, 0, sizeof(map));
    if (pcie_device_find(vendor_id, device_id,
                         &map.bar0_phys, &map.bar0_len,
                         &map.bar1_phys, &map.bar1_len) != 0) {
        fprintf(stderr, "Device not found or mapping failed\n");
        return ret;
    }

    /* Map BAR0 */
    printf("\nMapping BAR0: phys=0x%llx, len=%zu\n",
           (unsigned long long)map.bar0_phys, map.bar0_len);

    map.bar0_virt = (uintptr_t)map_physical(map.bar0_phys, map.bar0_len);
    if (!map.bar0_virt) {
        fprintf(stderr, "Failed to map BAR0\n");
        return ret;
    }

    printf("BAR0 mapped to virt=0x%lx\n", (unsigned long)map.bar0_virt);

    /* Read registers */
    printf("\nReading FPGA registers:\n");

    /* Read control register */
    val = pcie_read32(map.bar0_virt, REG_CTRL);
    printf("  CTRL (0x%02x)  = 0x%08x\n", REG_CTRL, val);

    /* Read status register */
    val = pcie_read32(map.bar0_virt, REG_STATUS);
    printf("  STATUS (0x%02x) = 0x%08x\n", REG_STATUS, val);

    /* Read interrupt enable */
    val = pcie_read32(map.bar0_virt, REG_INT_ENABLE);
    printf("  INT_EN  (0x%02x) = 0x%08x\n", REG_INT_ENABLE, val);

    /* Read interrupt status */
    val = pcie_read32(map.bar0_virt, REG_INT_STATUS);
    printf("  INT_STS (0x%02x) = 0x%08x\n", REG_INT_STATUS, val);

    /* Test write to control register (toggle START bit) */
    printf("\nTesting register write...\n");
    uint32_t ctrl = pcie_read32(map.bar0_virt, REG_CTRL);
    printf("  Setting START bit...\n");
    pcie_write32(map.bar0_virt, REG_CTRL, ctrl | CTRL_START);
    val = pcie_read32(map.bar0_virt, REG_CTRL);
    printf("  CTRL now = 0x%08x\n", val);

    /* Clear START bit */
    pcie_write32(map.bar0_virt, REG_CTRL, ctrl & ~CTRL_START);
    val = pcie_read32(map.bar0_virt, REG_CTRL);
    printf("  CTRL after clear = 0x%08x\n", val);

    printf("\nAll tests passed!\n");
    ret = EXIT_SUCCESS;

cleanup:
    if (map.bar0_virt) {
        unmap_physical((void *)map.bar0_virt, map.bar0_len);
    }

    return ret;
}