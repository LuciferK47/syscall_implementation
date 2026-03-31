/*
 * sys_memlineage.c
 * Custom system call implementation to track memory writes.
 * Written for Linux Kernel version 6.19.10.
 */

#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/capability.h> 
#include <include/sys_memlineage.h>

//Internal region wrapper
struct ml_region_internal {
    struct list_head node;
    struct ml_region data;
    kuid_t owner_kuid;
};

static LIST_HEAD(g_tracked_regions);
static DEFINE_SPINLOCK(g_regions_lock);
static atomic_t g_region_count = ATOMIC_INIT(0);

// Helpers

static struct ml_region_internal *ml_find_region(u64 addr)
{
    struct ml_region_internal *entry;

    list_for_each_entry(entry, &g_tracked_regions, node) {
        if (addr >= entry->data.start_addr && 
            addr < (entry->data.start_addr + entry->data.size))
            return entry;
    }
    return NULL;
}

static bool ml_is_owner(const struct ml_region_internal *r,
                        const struct cred *cred)
{
    return (r->data.owner_pid == task_tgid_vnr(current)) &&
           uid_eq(r->owner_kuid, cred->uid);
}

// Command handlers

static int ml_register_region(struct ml_register_args *args,
                               const struct cred *cred)
{
    struct ml_region_internal *new_region;
    unsigned long flags;

    if (!args || args->size == 0)
        return -EINVAL;

    new_region = kmalloc(sizeof(*new_region), GFP_KERNEL);
    if (!new_region)
        return -ENOMEM;

    // Initialize all fields to avoid garbage data
    memset(new_region, 0, sizeof(*new_region));
    new_region->data.start_addr       = args->start_addr;
    new_region->data.size             = args->size;
    new_region->data.owner_pid        = task_tgid_vnr(current);
    new_region->data.tracking_enabled = 1;
    new_region->data.event_count      = 0; // Crucial fix
    new_region->owner_kuid            = cred->uid;
    new_region->data.owner_uid        = from_kuid_munged(current_user_ns(), cred->uid);

    spin_lock_irqsave(&g_regions_lock, flags);

    if (atomic_read(&g_region_count) >= ML_MAX_REGIONS) {
        spin_unlock_irqrestore(&g_regions_lock, flags);
        kfree(new_region);
        return -ENOMEM;
    }

    // Check specifically for exact start_addr collision for EEXIST
    if (ml_find_region(args->start_addr)) {
        spin_unlock_irqrestore(&g_regions_lock, flags);
        kfree(new_region);
        return -EEXIST;
    }

    list_add_tail(&new_region->node, &g_tracked_regions);
    atomic_inc(&g_region_count);
    spin_unlock_irqrestore(&g_regions_lock, flags);

    return 0;
}

static int ml_log_write_event(struct ml_log_write_args *args,
                               const struct cred *cred)
{
    struct ml_region_internal *region;
    struct ml_event *event;
    unsigned long flags;
    u32 event_idx;

    if (!args || args->write_size == 0)
        return -EINVAL;

    spin_lock_irqsave(&g_regions_lock, flags);
    region = ml_find_region(args->region_addr);

    if (!region) {
        spin_unlock_irqrestore(&g_regions_lock, flags);
        return -ENOENT;
    }

    if (!region->data.tracking_enabled) {
        spin_unlock_irqrestore(&g_regions_lock, flags);
        return -EAGAIN;
    }

    if (!ml_is_owner(region, cred) && !capable(CAP_SYS_ADMIN)) {
        spin_unlock_irqrestore(&g_regions_lock, flags);
        return -EPERM;
    }

    // Range check within the region
    if (args->write_offset >= region->data.size ||
        args->write_size > region->data.size - args->write_offset) {
        spin_unlock_irqrestore(&g_regions_lock, flags);
        return -ERANGE;
    }

    event_idx = region->data.event_count % ML_MAX_EVENTS;
    event     = &region->data.events[event_idx];

    event->pid          = task_pid_vnr(current);
    event->tgid         = task_tgid_vnr(current);
    event->timestamp_ns = ktime_get_ns();
    event->write_offset = args->write_offset;
    event->write_size   = args->write_size;
    strncpy(event->comm, current->comm, ML_COMM_LEN - 1);
    event->comm[ML_COMM_LEN - 1] = '\0';

    region->data.event_count++;
    spin_unlock_irqrestore(&g_regions_lock, flags);

    return 0;
}

static int ml_query_region(struct ml_query_args *args,
                            const struct cred *cred)
{
    struct ml_region_internal *region;
    unsigned long flags;
    bool privileged;
    u32 visible_total;
    u32 i;

    if (!args)
        return -EINVAL;

    privileged = capable(CAP_SYS_ADMIN);

    spin_lock_irqsave(&g_regions_lock, flags);
    region = ml_find_region(args->region_addr);

    if (!region) {
        spin_unlock_irqrestore(&g_regions_lock, flags);
        return -ENOENT;
    }

    if (!privileged && !ml_is_owner(region, cred)) {
        spin_unlock_irqrestore(&g_regions_lock, flags);
        return -EACCES;
    }

    // Start with the raw region data
    args->region_data = region->data;
    visible_total = min((u32)region->data.event_count, (u32)ML_MAX_EVENTS);

    if (!privileged) {
        pid_t caller_tgid = task_tgid_vnr(current);
        u32 filtered_count = 0;
        struct ml_event temp_buf[ML_MAX_EVENTS];
        memset(temp_buf, 0, sizeof(temp_buf));

        for (i = 0; i < visible_total; i++) {
            if (args->region_data.events[i].tgid == (u32)caller_tgid) {
                memcpy(&temp_buf[filtered_count], &args->region_data.events[i], sizeof(struct ml_event));
                filtered_count++;
            }
        }
        memcpy(args->region_data.events, temp_buf, sizeof(struct ml_event) * ML_MAX_EVENTS);
        args->event_count = filtered_count;
    } else {
        args->event_count = visible_total;
    }

    spin_unlock_irqrestore(&g_regions_lock, flags);
    return 0;
}

static int ml_set_tracking_state(u64 region_addr, u32 enabled,
                                  const struct cred *cred)
{
    struct ml_region_internal *region;
    unsigned long flags;
    bool privileged = capable(CAP_SYS_ADMIN);

    spin_lock_irqsave(&g_regions_lock, flags);
    region = ml_find_region(region_addr);

    if (!region) {
        spin_unlock_irqrestore(&g_regions_lock, flags);
        return -ENOENT;
    }

    if (!privileged && !ml_is_owner(region, cred)) {
        spin_unlock_irqrestore(&g_regions_lock, flags);
        return -EACCES;
    }

    region->data.tracking_enabled = enabled ? 1 : 0;
    spin_unlock_irqrestore(&g_regions_lock, flags);
    return 0;
}

// Syscall entry point
SYSCALL_DEFINE2(memlineage, int, cmd, void __user *, arg)
{
    const struct cred *cred;
    long ret = 0;

    if (!arg)
        return -EFAULT;

    cred = get_current_cred();

    switch (cmd) {
    case ML_CMD_REGISTER: {
        struct ml_register_args kargs;
        if (copy_from_user(&kargs, arg, sizeof(kargs))) {
            ret = -EFAULT;
            break;
        }
        ret = ml_register_region(&kargs, cred);
        break;
    }

    case ML_CMD_LOG_WRITE: {
        struct ml_log_write_args kargs;
        if (copy_from_user(&kargs, arg, sizeof(kargs))) {
            ret = -EFAULT;
            break;
        }
        ret = ml_log_write_event(&kargs, cred);
        break;
    }

    case ML_CMD_QUERY: {
        struct ml_query_args kargs;
        // Copy user input to get the region_addr
        if (copy_from_user(&kargs, arg, sizeof(kargs))) {
            ret = -EFAULT;
            break;
        }
        ret = ml_query_region(&kargs, cred);
        if (ret == 0) {
            if (copy_to_user(arg, &kargs, sizeof(kargs)))
                ret = -EFAULT;
        }
        break;
    }

    case ML_CMD_TRACKING_ENABLE:
    case ML_CMD_TRACKING_DISABLE: {
        struct ml_tracking_control kargs;
        if (copy_from_user(&kargs, arg, sizeof(kargs))) {
            ret = -EFAULT;
            break;
        }
        ret = ml_set_tracking_state(kargs.region_addr,
                                    cmd == ML_CMD_TRACKING_ENABLE,
                                    cred);
        break;
    }

    default:
        ret = -EINVAL;
        break;
    }

    put_cred(cred);
    return ret;
}