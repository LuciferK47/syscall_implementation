/*
 * sys_memlineage.h
 * Header definition for memlineage syscall limits and structs.
 * Written for Linux Kernel version 6.19.10.
 */

#ifndef _SYS_MEMLINEAGE_H
#define _SYS_MEMLINEAGE_H


#ifdef __KERNEL__
#  include <linux/types.h>
#  include <linux/ioctl.h>  
#else
#  include <stdint.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#  include <errno.h>

typedef uint8_t  __u8;
typedef uint16_t __u16;
typedef uint32_t u32;  
typedef uint64_t u64;
#endif /* __KERNEL__ */

#ifdef __GNUC__
#  define ML_PACKED __attribute__((packed))
#else
#  define ML_PACKED
#endif

/* 
 *  Tuneable limits
 *
 *    ML_MAX_REGIONS  – global cap on simultaneously tracked regions.
 *                      Changing this requires a kernel rebuild.
 *    ML_MAX_EVENTS   – ring-buffer depth per region.
 *                      Determines how many write events are retained before
 *                      the oldest is overwritten.
 *    ML_COMM_LEN     – bytes reserved for the process name (task->comm).
 *                      Must equal TASK_COMM_LEN (16) in the kernel. */

#define ML_MAX_REGIONS  64
#define ML_MAX_EVENTS   32
#define ML_COMM_LEN     16

/* 
 * Command codes  (first argument to the syscall)
 *
 *   ML_CMD_REGISTER         – register a memory region for write tracking
 *   ML_CMD_LOG_WRITE        – record one write event into a region's ring
 *   ML_CMD_QUERY            – read region metadata + filtered event ring
 *   ML_CMD_TRACKING_ENABLE  – resume event logging for a region
 *   ML_CMD_TRACKING_DISABLE – pause  event logging for a region */

#define ML_CMD_REGISTER         1
#define ML_CMD_LOG_WRITE        2
#define ML_CMD_QUERY            3
#define ML_CMD_TRACKING_ENABLE  4
#define ML_CMD_TRACKING_DISABLE 5

#define ML_CMD_MIN  ML_CMD_REGISTER
#define ML_CMD_MAX  ML_CMD_TRACKING_DISABLE

/* 
 * Return / error contract
 * All commands return 0 on success and a negative errno on failure.
 *
 *  -EFAULT   NULL or unmapped arg pointer; copy_from/to_user failed
 *  -EINVAL   Unknown command; zero-size register or log_write
 *  -ENOMEM   Global ML_MAX_REGIONS limit reached; kmalloc failure
 *  -EEXIST   ML_CMD_REGISTER: region at that address already registered
 *  -ENOENT   Target region not found (unregistered address)
 *  -EPERM    ML_CMD_LOG_WRITE: caller is not the region owner
 *            (and does not hold CAP_SYS_ADMIN)
 *  -EACCES   ML_CMD_QUERY / ML_CMD_TRACKING_*: caller is not the owner
 *            (and does not hold CAP_SYS_ADMIN)
 *  -EAGAIN   ML_CMD_LOG_WRITE: tracking is currently disabled for region
 *  -ERANGE   ML_CMD_LOG_WRITE: write_offset or write_size exceeds region */

struct ml_event {
    u32  pid;
    u32  tgid;
    u64  timestamp_ns;
    u64  write_offset;
    u64  write_size;
    char comm[ML_COMM_LEN];
} ML_PACKED;

struct ml_region {
    u64             start_addr;
    u64             size;
    u32             owner_pid;
    u32             owner_uid;         
    u32             tracking_enabled;
    u32             event_count;
    struct ml_event events[ML_MAX_EVENTS];
} ML_PACKED;

struct ml_register_args {
    u64 start_addr;
    u64 size;
} ML_PACKED;

struct ml_log_write_args {
    u64 region_addr;
    u64 write_offset;
    u64 write_size;
} ML_PACKED;

struct ml_query_args {
    u64              region_addr;   
    u32              event_count;   
    u32              _pad;          
    struct ml_region region_data;   
} ML_PACKED;


struct ml_tracking_control {
    u64 region_addr;
} ML_PACKED;


#ifndef __KERNEL__

#ifndef MEMLINEAGE_SYSCALL_NR
#  define MEMLINEAGE_SYSCALL_NR  471
#endif

static inline long memlineage(int cmd, void *arg)
{
    return syscall(MEMLINEAGE_SYSCALL_NR, cmd, arg);
}

#endif /* !__KERNEL__ */

#endif /* _SYS_MEMLINEAGE_H */