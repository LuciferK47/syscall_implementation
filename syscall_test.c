/*
 * syscall_test.c
 * User-space test wrapper for the memlineage system call.
 * Validates constraints in Linux Kernel version 6.19.10.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "sys_memlineage.h"

#define REGION_SIZE 256

int main() {
    int tests_passed = 0;
    int tests_total = 0;

    char *mem = malloc(1024 * 1024);
    if (!mem) {
        printf("Failed to allocate test memory.\n");
        return 1;
    }

    // 1) Registration and Collisions
    printf("1) Registration and Collisions\n");
    tests_total++;
    
    void *mem1 = mem;
    struct ml_register_args reg1 = { (u64)mem1, REGION_SIZE };
    
    int t1_reg_ok = (memlineage(ML_CMD_REGISTER, &reg1) == 0);
    int t1_dup_err = (memlineage(ML_CMD_REGISTER, &reg1) == -1 && errno == EEXIST);

    if (t1_reg_ok && t1_dup_err) {
        printf("  -> [PASS]\n");
        tests_passed++;
    } else {
        printf("  -> [FAIL]\n");
    }

    // 2) Bounds Checking
    printf("2) Bounds Checking\n");
    tests_total++;
    
    void *mem2 = mem + REGION_SIZE;
    struct ml_register_args reg2 = { (u64)mem2, REGION_SIZE };
    memlineage(ML_CMD_REGISTER, &reg2);
    
    struct ml_log_write_args oob_log = { (u64)mem2, 200, 100 };
    
    if (memlineage(ML_CMD_LOG_WRITE, &oob_log) == -1 && errno == ERANGE) {
        printf("  -> [PASS]\n");
        tests_passed++;
    } else {
        printf("  -> [FAIL]\n");
    }

    // 3) Tracking Controls
    printf("3) Tracking Controls\n");
    tests_total++;
    
    void *mem3 = mem + (REGION_SIZE * 2);
    struct ml_register_args reg3 = { (u64)mem3, REGION_SIZE };
    memlineage(ML_CMD_REGISTER, &reg3);
    
    struct ml_tracking_control ctrl = { (u64)mem3 };
    struct ml_log_write_args log3 = { (u64)mem3, 10, 50 };
    
    memlineage(ML_CMD_TRACKING_DISABLE, &ctrl);
    int t3_disabled_rc = memlineage(ML_CMD_LOG_WRITE, &log3);
    int t3_disabled_err = errno;
    
    memlineage(ML_CMD_TRACKING_ENABLE, &ctrl);
    int t3_enabled_rc = memlineage(ML_CMD_LOG_WRITE, &log3);
    
    if (t3_disabled_rc == -1 && t3_disabled_err == EAGAIN && t3_enabled_rc == 0) {
        printf("  -> [PASS]\n");
        tests_passed++;
    } else {
        printf("  -> [FAIL]\n");
    }

    // 4) Ring Buffer Wrap Around
    printf("4) Ring Buffer Wrap Around\n");
    tests_total++;
    
    void *mem4 = mem + (REGION_SIZE * 3);
    struct ml_register_args reg4 = { (u64)mem4, REGION_SIZE };
    memlineage(ML_CMD_REGISTER, &reg4);
    
    struct ml_log_write_args log4 = { (u64)mem4, 0, 10 };
    for (int i = 0; i < 40; i++) {
        log4.write_offset = i;
        memlineage(ML_CMD_LOG_WRITE, &log4);
    }
    
    struct ml_query_args query = { .region_addr = (u64)mem4 };
    memlineage(ML_CMD_QUERY, &query);
    
    if (query.event_count == ML_MAX_EVENTS && query.region_data.event_count == 40) {
        printf("  -> [PASS]\n");
        tests_passed++;
    } else {
        printf("  -> [FAIL]\n");
    }

    printf("\nResults: %d/%d Passed\n", tests_passed, tests_total);
    
    free(mem);
    return (tests_passed == tests_total) ? 0 : 1;
}