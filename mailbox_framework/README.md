# Mailbox Framework

Shared memory based mailbox IPC mechanism for Linux.

## Architecture

```
┌─────────────────────────────────────────────────────┐
│              Application Layer                        │
│                                                      │
│   Producer Process          Consumer Process          │
│   ┌──────────┐             ┌──────────┐             │
│   │ mbox_send()│◄───────────►│mbox_recv()│            │
│   └────┬─────┘             └────┬─────┘             │
│        │                         │                   │
│        ▼                         ▼                   │
│   ┌──────────────────────────────────────┐          │
│   │        Userspace Library (libmbox)   │          │
│   │  - mbox_open/close/send/recv         │          │
│   │  - mbox_get_fd (for poll/select)     │          │
│   └────────────┬─────────────────────────┘          │
└────────────────┼───────────────────────────────────┘
                 │ ioctl / read / write / poll
┌────────────────┼───────────────────────────────────┐
│                ▼            Kernel Layer            │
│   ┌──────────────────────────────────────┐          │
│   │     Mailbox Kernel Module (mbox)     │          │
│   │                                      │          │
│   │  - Ring buffer queue                 │          │
│   │  - Per-channel message routing       │          │
│   │  - Wait queues for blocking ops      │          │
│   │  - poll() / wait_event support      │          │
│   └────────────┬─────────────────────────┘          │
│                │                                     │
│                ▼                                     │
│   ┌──────────────────────────────────────┐          │
│   │       Shared Memory (vmalloc)        │          │
│   │                                      │          │
│   │  ┌────────────────────────────┐      │          │
│   │  │ struct mbox_queue header  │      │          │
│   │  ├────────────────────────────┤      │          │
│   │  │ struct mbox_msg_entry[0]  │      │          │
│   │  │ struct mbox_msg_entry[1] │      │          │
│   │  │ ...                       │      │          │
│   │  └────────────────────────────┘      │          │
│   └──────────────────────────────────────┘          │
└─────────────────────────────────────────────────────┘
```

## Components

```
mailbox_framework/
├── include/
│   └── mbox.h              # Public userspace API
├── kernel/
│   └── mbox_kern.c         # Linux kernel module
├── lib/
│   └── mbox_lib.c          # Userspace library
├── apps/
│   └── example/
│       └── mbox_example.c  # Example send/receive
├── kernel_module/
│   ├── Makefile            # Build kernel module
│   └── Kconfig
├── userspace/
│   ├── lib/
│   │   └── Makefile
│   └── apps/
│       └── Makefile
└── README.md
```

## Building

### Kernel Module

```bash
cd kernel_module
make
sudo insmod mbox.ko
```

### Userspace Library

```bash
cd userspace/lib
make
sudo make install  # Optional
```

### Example Applications

```bash
cd userspace/apps
make
```

## API Overview

```c
#include <mbox.h>

/* Open mailbox */
int mbox = mbox_open("my_mbox", MBOX_O_RDWR | MBOX_O_CREATE);

/* Send message */
mbox_send(mbox, channel, payload, size, flags);

/* Receive (blocking with timeout) */
mbox_recv(mbox, channel_mask, buf, max_size, timeout_ms);

/* Poll/select support */
int fd = mbox_get_fd(mbox);
poll(&pfd, 1, timeout_ms);

/* Statistics */
mbox_stats_t stats;
mbox_stats(mbox, &stats);

/* Close */
mbox_close(mbox);
```

## Running the Example

```bash
# Terminal 1 - Start server (receiver)
./userspace/apps/example/mbox_example server

# Terminal 2 - Start client (sender)
./userspace/apps/example/mbox_example client 20
```

## Features

- **Multi-channel**: Up to 16 channels per mailbox
- **Configurable depth**: 2-4096 messages
- **Configurable message size**: 64B - 1MB
- **Blocking and non-blocking** operations
- **poll()/select()** support via file descriptor
- **Broadcast** and **urgent** message flags
- **Statistics**: messages sent/received/dropped, byte counts
- **Lock-free** internal design (single-producer assumed)

## Message Flow

1. Producer calls `mbox_send()`
2. Library writes message to shared memory ring buffer
3. Consumer's `poll()`/`recv()` wakes up
4. Consumer reads message from shared memory
5. Message removed from queue (unless PEEK flag)

## License

GPL-2.0