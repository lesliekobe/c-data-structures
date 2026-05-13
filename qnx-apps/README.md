# QNX Applications Repository

Example QNX Neutrino programs covering resource managers, PCIe device access, IPC, and embedded system patterns.

## Directory Structure

```
qnx-apps/
├── common/              # Shared headers and utilities
├── hello_qnx/           # Minimal hello world
├── resmgr_skeleton/     # Resource manager template
├── pcie_access/         # PCIe BAR memory access
├── interrupt_example/   # Interrupt handling
├── shared_memory/       # Shared memory / message passing
├── qnxIPC/              # QNX IPC examples (MsgSend, ChannelCreate, etc)
└── build/               # Build files (Mkfiles)
```

## Build Requirements

- QNX Neutrino RTOS 6.x or later
- QNX Development Kit (qnxdevenv or qcc compiler)

## Building

```bash
# Each sub-project has its own Mkfile
# From project root:
make <target>

# Or cd into sub-project:
cd pcie_access && make
```

## Repository License

GPL-2.0

## Status

Work-in-progress. QNX application examples being added.