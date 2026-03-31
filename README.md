# Custom Linux System Call: `memlineage`

This repository documents the implementation, integration, and testing of a custom Linux system call named `memlineage`. It is designed to track memory writes to registered memory regions using a ring-buffer event logging mechanism. 

The implementation was developed and successfully built directly into **Linux Kernel Version 6.19.10**.

---

## Step-by-Step Implementation Guide

The following steps outline the entire process taken to compile the kernel and inject the custom system call, as documented in our project report:

**1. Environment Setup**
* **Step 1:** Downloaded Linux Kernel version `6.19.10` source code. Set up `/usr/src` as the primary working directory.
* **Step 2:** Extracted the Kernel files using `tar -xvf linux-6.19.10.tar.xz`.
* **Step 3:** Installed all required build dependencies (e.g., `build-essential`, `libncurses-dev`, `bison`, `flex`, `libssl-dev`, `libelf-dev`) necessary for recompiling the kernel.

**2. Initial Kernel Compilation**
* **Step 4:** Configured the kernel using `make menuconfig` / `make defconfig` to generate the `.config` file.
* **Step 5:** Executed `make` to compile the baseline kernel.
* **Step 6:** Ran `make modules_install` to install dynamically linked kernel modules.
* **Step 7:** Ran `make install` to copy the kernel into the boot partition and update system bootloaders (GRUB).

**3. System Call Injection**
* **Step 8 (Makefile):** Updated `kernel/Makefile` by appending `obj-y += sys_memlineage.o` to link our logic natively into the kernel.
* **Step 9 (Syscall Table):** Updated the system call table located at `arch/x86/entry/syscalls/syscall_64.tbl` to assign the ID `471` to `memlineage`.
* **Step 10 (ASM Generic):** Updated `include/asm-generic/syscalls.h` to include the `#ifndef` guard declaration for our system call.
* **Step 11 (Linux Headers):** Updated `include/linux/syscalls.h` by registering the global system call prototype: `asmlinkage long sys_memlineage(int cmd, void __user *arg, size_t len);`.

**4. Core Logic Integration**
* **Step 12:** Implemented and stored `sys_memlineage.c` directly within the `kernel/` directory.
* **Step 13:** Implemented and stored the struct definition header `sys_memlineage.h` in the `include/linux/` directory for unified access.

**5. Final Build & Validation**
* **Step 14:** Recompiled the kernel with the new system call fully mapped and restarted the machine into the updated Kernel.
* **Step 15:** Compiled and ran the user-space testing suite (`syscall_test.c`) to validate memory region tracking, ring buffer bounds, and error handling.

---

## File Structure & Modifications

### Core Implementation Files
* **`sys_memlineage.c`**: Contains the core logic manipulating linked lists (`list_head`) and handling spinlocks (`spinlock_t`) to safely manage region states and writes securely. 
* **`sys_memlineage.h`**: Exposes system tuning limits ($ML\_MAX\_REGIONS$, $ML\_MAX\_EVENTS$), command structures (e.g., `ml_region`, `ml_event`), and command macros to the userland testing application.
* **`syscall_test.c`**: A robust user-space C testing wrapper used to deliberately trigger and validate success criteria and potential failure states across $-ERANGE$, $-EAGAIN$, and collision ($-EEXIST$) checks.

### Modified Native Kernel Files
* `arch/x86/entry/syscalls/syscall_64.tbl`
* `include/asm-generic/syscalls.h`  *(Represented by syscalls generic-asm.h in the repo)*
* `include/linux/syscalls.h` *(Represented by syscalls linux.h in the repo)*
* `kernel/Makefile`

---

## Architecture & Commands

The `memlineage` system call operates as a multiplexer controlled via an integer `cmd` parameter:

1. **`ML_CMD_REGISTER` (1):** Allocates tracking capability against a user-defined memory region. Operates at $\mathcal{O}(N)$ lookup time where $N$ is capped to a highly performant $M_{MAX} = 64$.
2. **`ML_CMD_LOG_WRITE` (2):** Appends a write-event log to the designated ring buffer structure. Timestamps are generated using `ktime_get_ns()`. Ring buffer arrays maintain strictly capped sizes modulo $N_{MAX}$.
3. **`ML_CMD_QUERY` (3):** Securely iterates the event ring. Access controls strictly limit reading data unless authorized by the proper UID matching the original tracking instantiator or elevated `capable(CAP_SYS_ADMIN)` authority.
4. **`ML_CMD_TRACKING_ENABLE` (4) / `ML_CMD_TRACKING_DISABLE` (5):** Direct boolean flags to safely halt background processing logic for the selected context tracking without wiping historical memory events.
