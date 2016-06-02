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
#include <pthread.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#define REF_INTERVAL 30000000
#define MON_INTERVAL 30000000
#define REF_SAMPLE 100
#define MON_SAMPLE 100

#define REF_PERIOD 4
#define MON_PERIOD 3000000000

uint64_t reference_sample[REF_SAMPLE];
uint64_t monitored_sample[MON_SAMPLE];

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
    system("wrmsr -p 6 0x19A 17");
    system("wrmsr -p 7 0x19A 17");
    system("wrmsr -p 8 0x19A 17");
    system("wrmsr -p 9 0x19A 17");
    system("wrmsr -p 10 0x19A 17");
    system("wrmsr -p 11 0x19A 17");
}

void throttle_up() {
    system("wrmsr -p 6 0x19A 0");
    system("wrmsr -p 7 0x19A 0");
    system("wrmsr -p 8 0x19A 0");
    system("wrmsr -p 9 0x19A 0");
    system("wrmsr -p 10 0x19A 0");
    system("wrmsr -p 11 0x19A 0");
}

int main(int argc, char **argv) {
    int core_id = atoi(argv[1]);
    int ROUND = atoi(argv[2]);

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
    int i, j, k;
    uint64_t start_cycle;
    uint64_t read_access, write_access;
    for (i = 0; i<ROUND; i++) {
        FILE *fp = fopen ("res", "a");
        fprintf(fp, "resampling monitored samples: \n");
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
            fprintf(fp, "%f\n", ret);
        }
        fclose(fp);
    }
    close(fd1);
    close(fd2);
    return 0;
}
