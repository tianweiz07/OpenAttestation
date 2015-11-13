#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
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

uint64_t rdtsc(void) {
        uint64_t a, d;
        __asm__ volatile ("rdtsc" : "=a" (a), "=d" (d));
        return (d<<32) | a;
}

int *array;

#define traversals 20
#define Size 20971520

#define CACHE_LINE_SIZE 64

int initial() {
	array = mmap(0, Size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (array == MAP_FAILED) {
                printf("map source buffer error!\n");
                return 0;
        }

	int i;
        for (i=0; i <Size/sizeof(int); i++) {
                array[i] = rand();
        }
        return 0;
}

void stream_access(int cpu_id, uint64_t _size) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu_id, &set);
        if (sched_setaffinity(syscall(SYS_gettid), sizeof(cpu_set_t), &set)) {
                fprintf(stderr, "Error set affinity\n")  ;
                return;
	}

	int i, j;
        for (i=0; i < traversals; i++) {
                for (j=0; j < (_size*1024*1024)/sizeof(int); j+= CACHE_LINE_SIZE/sizeof(int)) {
			array[j] ++;
                }
        }
	return;
}

int main(int argc, char **argv) {
	initial();

        struct perf_event_attr pe1;
        struct perf_event_attr pe2;
        int fd1, fd2;

        memset(&pe1, 0, sizeof(struct perf_event_attr));
        pe1.type = PERF_TYPE_HW_CACHE;
        pe1.config = (PERF_COUNT_HW_CACHE_LL)|(PERF_COUNT_HW_CACHE_OP_READ << 8)|(PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
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

        fd1 = syscall(__NR_perf_event_open, &pe1, 0, -1, -1, 0);
        fd2 = syscall(__NR_perf_event_open, &pe2, 0, -1, -1, 0);
	long long access, misses;
	while (1) {
		int i;
		int bin[10];
		int sum = 0;
		uint64_t end_cycle;
		for (i=0; i<10; i++)
			bin[i] = 0;
		for (i=0; i<100; i++) {
			end_cycle = rdtsc() + 150000000;
        	        ioctl(fd1, PERF_EVENT_IOC_RESET, 0);
                	ioctl(fd2, PERF_EVENT_IOC_RESET, 0);
	                ioctl(fd1, PERF_EVENT_IOC_ENABLE, 0);
        	        ioctl(fd2, PERF_EVENT_IOC_ENABLE, 0);

			stream_access(10, i/15);

	                ioctl(fd1, PERF_EVENT_IOC_DISABLE, 0);
	                ioctl(fd2, PERF_EVENT_IOC_DISABLE, 0);
	                read(fd1, &misses, sizeof(long long));
	                read(fd2, &access, sizeof(long long));
			if (access > 0) {
				int a = (int)(misses*10/access);
				if (a>=10)
					a = 9;
				if (a<0)
					a = 0;
				bin[a] ++;
				sum ++;
			}
			while(rdtsc()<end_cycle);
		}

//		for (i=0; i<10; i++) {
//			printf("%f\n", bin[i]*1.0/sum);
//		}

		double bin_ref[10];
		bin_ref[0] = 0.977011;
		bin_ref[1] = 0.000000;
		bin_ref[2] = 0.000000;
		bin_ref[3] = 0.011494;
		bin_ref[4] = 0.000000;
		bin_ref[5] = 0.011494;
		bin_ref[6] = 0.000000;
		bin_ref[7] = 0.000000;
		bin_ref[8] = 0.000000;
		bin_ref[9] = 0.000000;

		double his = 0;
		double his_ref = 0;
		double max = 0;
		for (i=0; i<10; i++) {
			his += 1.0*bin[i]/sum;
			his_ref += bin_ref[i];
			if (max < his_ref-his)
				max = his_ref-his;
			if (max < his-his_ref)
				max = his-his_ref;
		}
		FILE *pf = fopen("result", "a");
		fprintf(pf, "%f\n", max);
		fclose(pf);
	}


        close(fd1);
        close(fd2);
        return 0;
}
