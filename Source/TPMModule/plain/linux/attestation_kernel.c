#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <inttypes.h>
#include <fcntl.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <libvirt/libvirt.h>

#define DEST_FILE_LOCATION "/root/tpm-emulator/PCR_VALUE"
#define TEMP_FILE_LOCATION "/root/temp_log"

#define REF_INTERVAL 30000000
#define MON_INTERVAL 30000000
#define REF_SAMPLE 100
#define MON_SAMPLE 100

#define REF_PERIOD 4
#define MON_PERIOD 3000000000
#define THRESHOLD 0.5

uint64_t reference_sample[REF_SAMPLE];
uint64_t monitored_sample[MON_SAMPLE];

int *cpu_id;
int num_vm;


uint64_t rdtsc(void) {
    uint64_t a, d;
    __asm__ volatile ("rdtsc" : "=a" (a), "=d" (d));
    return (d<<32) | a;
}

int cmpfunc (const void * a, const void * b) {
    return ( *(int*)a - *(int*)b );
}

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

/**
 * Write the results to the PCR
 */
static int write_pcr(int pcr_index, int result) {
    FILE *pcr_file = fopen(DEST_FILE_LOCATION, "r+");
    assert(pcr_file);
    char cursor;
    char value[16] = "";

    int i = 0;
    while (i<= pcr_index) {
        cursor = fgetc(pcr_file);
        if (cursor == ':')
            i++;
    }

    sprintf(value, "%16x", result);
    for (i=0; i<16; i++) {
        if (value[i] == ' ')
            value[i] = '0';
        fputc(value[i], pcr_file);
        if ((i%2) == 1)
            fputc(' ', pcr_file);
    }

    fclose(pcr_file);
    return 0;
}

/**
 * Get pid from the uuid. 
 * From ps aux
 */

/*
pid_t map_uuid_pid(char* uuid) {
        pid_t vm_pid = -1;
        char cmd[256] = "";
        strcat(cmd, "ps aux | grep kvm | grep ");
        strcat(cmd, uuid);
        strcat(cmd, ">");
        strcat(cmd, TEMP_FILE_LOCATION);
        system(cmd);

        FILE* fp;
        char* line = NULL;
        char* content;
        size_t len = 0;
        ssize_t read;

        fp = fopen(TEMP_FILE_LOCATION, "r");
        if (fp == NULL)
                return vm_pid;

        while ((read = getline(&line, &len, fp)) != -1) {
                content = strtok(line, " ");

                if (len>400) {
                        content = strtok(NULL, " ");
                        vm_pid = atoi(content);
                        break;
                }
        }

        fclose(fp);
        remove(TEMP_FILE_LOCATION);
        if (line)
                free(line);

        return vm_pid;
}
*/

double KS_Test(uint64_t *sample1, int l1, uint64_t *sample2, int l2) {

    qsort(sample1, l1, sizeof(uint64_t), cmpfunc);
    qsort(sample2, l2, sizeof(uint64_t), cmpfunc);
    uint64_t *total = malloc((l1+l2)*sizeof(uint64_t));
    memcpy(total, sample1, l1*sizeof(uint64_t));
    memcpy(total+l1, sample2, l2*sizeof(uint64_t));
    qsort(total, l1+l2, sizeof(uint64_t), cmpfunc);
    int i1 = 0;
    int i2 = 0;
    int i = 0;
    double ks_val = 0.0;
    while (i < l1+l2) {
        while (i < l1+l2-1 && total[i] == total[i+1])
            i ++;
        while (i1 < l1 && sample1[i1] <= total[i])
            i1 ++;
        while (i2 < l2 && sample2[i2] <= total[i])
            i2 ++;
        ks_val = MAX(ks_val, fabs(i1*1.0/l1 - i2*1.0/l2));
        i ++;
    }
    return ks_val;

}

void throttle_down() {
    char cmd[256] = "";
    int i;
    for (i=0; i<num_vm-1; i++) {
        sprintf(cmd, "wrmsr -p %d 0x19A 17", cpu_id[i]);
        system(cmd);
    }
}

void throttle_up() {
    char cmd[256] = "";
    int i;
    for (i=0; i<num_vm-1; i++) {
        sprintf(cmd, "wrmsr -p %d 0x19A 0", cpu_id[i]);
        system(cmd);
    }
}

int main(int argc, char **argv) {

    virConnectPtr conn = NULL;
    virDomainPtr dom = NULL;    
    virVcpuInfoPtr vcpuinfo = (virVcpuInfo *)calloc(1, sizeof(virVcpuInfo));

    conn = virConnectOpen(NULL);
    if (conn == NULL) {
        fprintf(stderr, "Failed to connect to hypervisor\n");
        return -1;;
    }

    dom = virDomainLookupByUUIDString(conn, argv[1]);
    if (dom == NULL) {
        fprintf(stderr, "Failed to find Domain %s\n", argv[1]);
        virConnectClose(conn);
        return -1;
    }

    char cpumap[1];
    virDomainGetVcpus(dom, vcpuinfo, 1, cpumap, 1);

    int core_id = vcpuinfo[0].cpu;
    int vm_id = virDomainGetID(dom);

    num_vm = virConnectNumOfDomains(conn);
    int *ids = (int *)calloc(num_vm, sizeof(int));
    cpu_id = (int *)calloc(num_vm-1, sizeof(int));
    virConnectListDomains(conn, ids, num_vm);
    int i;
    int j = 0;
    for (i=0; i<num_vm; i++) {
        if (ids[i] !=  vm_id) {
            dom = virDomainLookupByID(conn, ids[i]);
            virDomainGetVcpus(dom, vcpuinfo, 1, cpumap, 1);
            cpu_id[j] = vcpuinfo[0].cpu;
            j++;
        }
    }



    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(1, &set);
    if (sched_setaffinity(syscall(SYS_gettid), sizeof(cpu_set_t), &set)) {
        fprintf(stderr, "Error set affinity\n")  ;
        return 0;
    }

    struct perf_event_attr pe1;
    struct perf_event_attr pe2;
    int fd1, fd2;

    memset(&pe1, 0, sizeof(struct perf_event_attr));
    pe1.type = PERF_TYPE_HW_CACHE;
    pe1.config = (PERF_COUNT_HW_CACHE_LL)|(PERF_COUNT_HW_CACHE_OP_WRITE << 8)|(PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
    pe1.size = sizeof(struct perf_event_attr);
    pe1.disabled = 1;
    pe1.inherit = 0;
    pe1.pinned = 1;
    pe1.exclude_kernel = 0;
    pe1.exclude_user = 0;
    pe1.exclude_hv = 0;
    pe1.exclude_host = 0;
    pe1.exclude_guest = 0;

    memset(&pe2, 0, sizeof(struct perf_event_attr));
    pe2.type = PERF_TYPE_HW_CACHE;
    pe2.config = (PERF_COUNT_HW_CACHE_LL)|(PERF_COUNT_HW_CACHE_OP_READ << 8)|(PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
    pe2.size = sizeof(struct perf_event_attr);
    pe2.disabled = 1;
    pe2.inherit = 0;
    pe2.pinned = 1;
    pe2.exclude_kernel = 0;
    pe2.exclude_user = 0;
    pe2.exclude_hv = 0;
    pe2.exclude_host = 0;
    pe2.exclude_guest = 0;

    fd1 = syscall(__NR_perf_event_open, &pe1, core_id, -1, -1, 0);
    fd2 = syscall(__NR_perf_event_open, &pe2, core_id, -1, -1, 0);
    int k;
    uint64_t start_cycle;
    uint64_t read_access, write_access;
    int flag = 0;

    throttle_down();
    for (j = 0; j<REF_SAMPLE; j++) {
        ioctl(fd1, PERF_EVENT_IOC_RESET, 0);
        ioctl(fd2, PERF_EVENT_IOC_RESET, 0);
        ioctl(fd1, PERF_EVENT_IOC_ENABLE, 0);
        ioctl(fd2, PERF_EVENT_IOC_ENABLE, 0);

        start_cycle = rdtsc();
        while(rdtsc() - start_cycle < REF_INTERVAL);

        ioctl(fd1, PERF_EVENT_IOC_DISABLE, 0);
        ioctl(fd2, PERF_EVENT_IOC_DISABLE, 0);
        read(fd1, &read_access, sizeof(uint64_t));
        read(fd2, &write_access, sizeof(uint64_t));
        reference_sample[j] = read_access + write_access;
    }
    throttle_up();
    for (k = 0; k<REF_PERIOD; k++) {
        start_cycle = rdtsc();
        while(rdtsc() - start_cycle < MON_PERIOD);
        for (j = 0; j<MON_SAMPLE; j++) {
            ioctl(fd1, PERF_EVENT_IOC_RESET, 0);
            ioctl(fd2, PERF_EVENT_IOC_RESET, 0);
            ioctl(fd1, PERF_EVENT_IOC_ENABLE, 0);
            ioctl(fd2, PERF_EVENT_IOC_ENABLE, 0);

            start_cycle = rdtsc();
            while(rdtsc() - start_cycle < MON_INTERVAL);

            ioctl(fd1, PERF_EVENT_IOC_DISABLE, 0);
            ioctl(fd2, PERF_EVENT_IOC_DISABLE, 0);
            read(fd1, &read_access, sizeof(uint64_t));
            read(fd2, &write_access, sizeof(uint64_t));
            monitored_sample[j] = read_access + write_access;
        }
        double ret = KS_Test(reference_sample, REF_SAMPLE, monitored_sample, MON_SAMPLE);
        if (ret < THRESHOLD)
            flag = 1;
    }

    if (flag == 0) {
        throttle_down();
        write_pcr(1, 1);
    } else {
        write_pcr(1, 0);
    }
    close(fd1);
    close(fd2);

    return 0;
}
