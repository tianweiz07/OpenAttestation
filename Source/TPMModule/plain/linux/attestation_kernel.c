#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <inttypes.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <libvirt/libvirt.h>


#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

#define DEST_FILE_LOCATION "/root/tpm-emulator/PCR_VALUE"
#define IMAGE_LOCATION "/opt/stack/data/nova/instances/"


#define WINDOW_SIZE 22
#define constraint WINDOW_SIZE
#define THRESHOLD_V 0.3
#define THRESHOLD_A 100

#define INTERVAL_V 300000
#define INTERVAL_A 3000000

#define NUM_SOCKET 2

#define interval_cycle 2500000
#define windows_a 5
#define boundary_a 30

int terminated;
uint64_t sig_time;

char *uuid;

int *cpu_id[NUM_SOCKET];
int sock_num[NUM_SOCKET];
int core_id;
int num_vm;

uint64_t begin_stamp;
int cal_num;
int cal_lock;

int signature_len;
int **dtw;

struct node {
	long long event_value;
	uint64_t time_value;
	struct node *next;
};

struct node *head;
struct node *end;

int train_set[WINDOW_SIZE];
int nomi_train_set[WINDOW_SIZE];
int nomi_test_set[WINDOW_SIZE];


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
 * Write the results from the PCR
 */
static int read_pcr(int pcr_index) {
    FILE *pcr_file = fopen(DEST_FILE_LOCATION, "r+");
    assert(pcr_file);
    char cursor;
    char value[16] = "";
    int x;

    int i = 0;
    while (i<= pcr_index) {
        cursor = fgetc(pcr_file);
        if (cursor == ':')
            i++;
    }
    for (i=0; i<16; i++) {
        value[i] = fgetc(pcr_file);
        if ((i%2) == 1)
            x=fgetc(pcr_file);
    }
    fclose(pcr_file);

    return (int)strtol(value, NULL, 16);
}

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

uint64_t rdtsc(void) {
    uint64_t a, d;
    __asm__ volatile ("rdtsc" : "=a" (a), "=d" (d));
    return (d<<32) | a;
}

void cal_average(int *data, int *res) {
    int sum = 0;
    int i;
    for (i=0; i<WINDOW_SIZE; i++)
        sum += data[i];
    int average = sum/WINDOW_SIZE;
    for (i=0; i<WINDOW_SIZE; i++)
        res[i] = data[i] - average;

    return;
}

void cal_average1(struct node* head, int *res, int average) {
    int sum = 0;
    int i;
    struct node* cur = head;
    for (i=0; i<WINDOW_SIZE; i++) {
        res[i] = cur->event_value - average;
        cur = cur->next;
    }

    return;
}

int DTW_Distance(int *data1, int *data2, int w) {
    int i, j;
    for (i=0; i<WINDOW_SIZE+1; i++) {
        for (j=0; j<WINDOW_SIZE+1; j++) {
	    dtw[i][j] = INT_MAX;
        }
    }

    dtw[0][0] = 0;

    for (i=1; i<WINDOW_SIZE+1; i++) {
        for (j=MAX(1, i-w); j<MIN(WINDOW_SIZE, i+w)+1; j++) {
            dtw[i][j] = abs(data1[i-1]-data2[j-1]) + MIN(dtw[i-1][j-1], MIN(dtw[i][j-1], dtw[i-1][j]));
        }
    }

    return dtw[WINDOW_SIZE][WINDOW_SIZE];
}

void training() {

    char file_address[256];
    strcpy(file_address, IMAGE_LOCATION);
    strcat(file_address, uuid);
    strcat(file_address, "/training_set");

    FILE *train_file = fopen(file_address, "r");
    int i;
    int time;
    for (i=0; i<WINDOW_SIZE; i++)
        fscanf(train_file, "%d	%d", &time, &train_set[i]);

    cal_average(train_set, nomi_train_set);
    signature_len = 0;
    for (i=0; i<WINDOW_SIZE; i++) 
        signature_len += abs(nomi_train_set[i]);
    close(train_file);
    return;
}

void *cal_data(void *argv) {
    int temp = -1;
    uint64_t next_cycle = 0;
    while (terminated == 0) {
        while (cal_lock || temp == cal_num);
            
        double distance = (double)DTW_Distance(nomi_train_set, nomi_test_set, constraint)/(double)signature_len;
        if (distance < THRESHOLD_V && head->time_value > next_cycle) {
            sig_time = head->time_value;
            next_cycle = head->time_value + interval_cycle;
        }
        temp = cal_num;
    }
}

void *perf_victim(void *argv) {

    /* Training the dataset*/
    training();

    /* Initialize performance counter structures*/
    struct perf_event_attr pe;
    int fd;
    int j;
    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.type = PERF_TYPE_HARDWARE;
    pe.config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS;
    pe.size = sizeof(struct perf_event_attr);
    pe.disabled = 1;
    pe.inherit = 0;
    pe.pinned = 1;
    pe.exclude_kernel = 0;
    pe.exclude_user = 0;
    pe.exclude_hv = 0;
    pe.exclude_host = 0;
    pe.exclude_guest = 0;

    fd = syscall(__NR_perf_event_open, &pe, -1, core_id, -1, 0);

    /* First round */
    struct node *start = (struct node*)malloc(sizeof(struct node));
    struct node *cur = start;

    uint64_t start_cycle;
    while(begin_stamp!= 0);
    begin_stamp = rdtsc();
    for (j=0; j<WINDOW_SIZE; j++) {
        ioctl(fd, PERF_EVENT_IOC_RESET, 0);
        ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);

        start_cycle = rdtsc();
        while(rdtsc()-start_cycle<INTERVAL_V);

        ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
        struct node *item = (struct node*)malloc(sizeof(struct node));
        cur->next = item;
        read(fd, &(item->event_value), sizeof(long long));
        item->time_value = (start_cycle-begin_stamp)/1000;
        cur = item;
    }
    cur->next = start;
    head = start;
    end = cur;

    long long sum = 0;
    cur = head;
    int i;
    for (i = 0; i<WINDOW_SIZE; i++) {
        sum += cur->event_value;
        cur = cur->next;
    }

    int average = sum/WINDOW_SIZE;
    cal_average1(head, nomi_test_set, average);
	
    /* running profiling and calculation*/
    int k;
    while (terminated == 0) {
        ioctl(fd, PERF_EVENT_IOC_RESET, 0);
        ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);

        start_cycle = rdtsc();
        while(rdtsc()-start_cycle<INTERVAL_V);

        ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
		
        cal_lock = 1;
        sum -= head->event_value;
        head = head->next;
        end = end->next;

        read(fd, &(end->event_value), sizeof(long long));
        end->time_value = (start_cycle-begin_stamp)/1000;
        sum += end->event_value;
        average = sum/WINDOW_SIZE;
        cal_average1(head, nomi_test_set, average);
        cal_num ++;
        cal_lock = 0;

    }

    close(fd);

    return;
}

void *perf_attacker(void *argv) {

    int i, j;
    uint64_t value, value1;

    int sock_id = core_id % NUM_SOCKET;
    uint64_t **time_a = (uint64_t **)calloc(sock_num[sock_id], sizeof(uint64_t *));
    long long **event_h = (long long **)calloc(sock_num[sock_id], sizeof(long long *));
    long long **event_m = (long long **)calloc(sock_num[sock_id], sizeof(long long *));
    int *index_a = (int *)calloc(sock_num[sock_id], sizeof(int));
    int *index_e = (int *)calloc(sock_num[sock_id], sizeof(int));

    for (i=0; i<sock_num[sock_id]; i++) {
        time_a[i] = (uint64_t *)calloc(1024, sizeof(uint64_t));
        event_h[i] = (long long *)calloc(1024, sizeof(long long));
        event_m[i] = (long long *)calloc(1024, sizeof(long long));
        index_a[i] = -1;
        index_e[i] = -1;
    }


    struct perf_event_attr pe, pe1;
    int fd, fd1;

    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.type = PERF_TYPE_HW_CACHE;
    pe.config = (PERF_COUNT_HW_CACHE_LL)|(PERF_COUNT_HW_CACHE_OP_READ << 8)|(PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
    pe.size = sizeof(struct perf_event_attr);
    pe.disabled = 1;
    pe.inherit = 0;
    pe.pinned = 1;
    pe.exclude_kernel = 0;
    pe.exclude_user = 0;
    pe.exclude_hv = 0;
    pe.exclude_host = 0;
    pe.exclude_guest = 0;

    memset(&pe1, 0, sizeof(struct perf_event_attr));
    pe1.type = PERF_TYPE_HW_CACHE;
    pe1.config = (PERF_COUNT_HW_CACHE_LL)|(PERF_COUNT_HW_CACHE_OP_READ << 8)|(PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16);
    pe1.size = sizeof(struct perf_event_attr);
    pe1.disabled = 1;
    pe1.inherit = 0;
    pe1.pinned = 1;
    pe1.exclude_kernel = 0;
    pe1.exclude_user = 0;
    pe1.exclude_hv = 0;
    pe1.exclude_host = 0;
    pe1.exclude_guest = 0;

    uint64_t start_cycle;

    begin_stamp = 0;
    while (begin_stamp == 1);
    while (terminated == 0) {
        for (i=0; i<sock_num[sock_id]; i++) {
            fd = syscall(__NR_perf_event_open, &pe, -1, cpu_id[sock_id][i], -1, 0);
            fd1 = syscall(__NR_perf_event_open, &pe1, -1, cpu_id[sock_id][i], -1, 0);

            ioctl(fd, PERF_EVENT_IOC_RESET, 0);
            ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
            ioctl(fd1, PERF_EVENT_IOC_RESET, 0);
            ioctl(fd1, PERF_EVENT_IOC_ENABLE, 0);
      			        
            start_cycle = rdtsc();
            while(rdtsc()-start_cycle<INTERVAL_A);
		                
            ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
            read(fd, &value, sizeof(long long));
            ioctl(fd1, PERF_EVENT_IOC_DISABLE, 0);
            read(fd1, &value1, sizeof(long long));

            close(fd);
            close(fd1);


            index_a[i] = (index_a[i] + 1)%1024;
            event_h[i][index_a[i]] = value1-value;
            event_m[i][index_a[i]] = value;
            time_a[i][index_a[i]] = (start_cycle-begin_stamp)/1000;

            if (sig_time > 0 && time_a[i][index_a[i]] > sig_time) {
                if (index_e[i] == -1) {
                    index_e[i] = index_a[i];
                } else if (index_a[i]-index_e[i] >= windows_a+boundary_a || (index_a[i] < index_e[i] && index_a[i]+1024-index_e[i] >= windows_a+boundary_a)) {
                    long long l_m = INT_MIN;
                    long long r_m = INT_MAX;
                    long long l_h = INT_MIN; 
                    long long r_h = INT_MAX;
                    for (j=0; j<windows_a; j++) {
                       l_m = MAX(l_m, event_m[i][(index_e[i]-j+1024)%1024]);
                       r_m = MIN(r_m, event_m[i][(index_e[i]+boundary_a+j)%1024]);
                       l_h = MAX(l_h, event_h[i][(index_e[i]-j+1024)%1024]);
                       r_h = MIN(r_h, event_h[i][(index_e[i]+boundary_a+j)%1024]);
                    }
                    if (r_m - l_m > THRESHOLD_A || r_h - l_h > THRESHOLD_A) {
                        terminated = 1;
                    } else {
                        index_e[i] = -1;
                        sig_time = -1;
                    }
                }
            }
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    if (strncmp(argv[2], "FFFFFFFF", 100) == 0)
        return 0;

    uuid = argv[1];
    int pcr_index = atoi(argv[2]);

    pid_t pid;
    char cmd_line[256];
    FILE *output;
    char _line[256];
    char arg1[256];
    char arg2[256];

    if (pcr_index == 1) {
	/* start side-channel detection */
	write_pcr(1, 0);
	write_pcr(2, 0);
	pid = fork();
	if (pid == 0) {

            virConnectPtr conn = NULL;
            virDomainPtr dom = NULL;    
            virDomainPtr dom1 = NULL;    
            virVcpuInfoPtr vcpuinfo = (virVcpuInfo *)calloc(1, sizeof(virVcpuInfo));

            conn = virConnectOpen(NULL);
            if (conn == NULL) {
                fprintf(stderr, "Failed to connect to hypervisor\n");
                return -1;;
            }

            dom = virDomainLookupByUUIDString(conn, uuid);
            if (dom == NULL) {
                fprintf(stderr, "Failed to find Domain %s\n", argv[1]);
                virConnectClose(conn);
                return -1;
            }

            char cpumap[1];
            virDomainGetVcpus(dom, vcpuinfo, 1, cpumap, 1);

            core_id = vcpuinfo[0].cpu;
            int vm_id = virDomainGetID(dom);

            num_vm = virConnectNumOfDomains(conn);
            int *ids = (int *)calloc(num_vm, sizeof(int));
            virConnectListDomains(conn, ids, num_vm);

            int i;

            for (i=0; i<NUM_SOCKET; i++)
                sock_num[i] = 0;
            
            for (i=0; i<num_vm; i++) {
                if (ids[i] !=  vm_id) {
                    dom1 = virDomainLookupByID(conn, ids[i]);
                    virDomainGetVcpus(dom1, vcpuinfo, 1, cpumap, 1);
                    int socket_id = vcpuinfo[0].cpu % NUM_SOCKET;
                    cpu_id[socket_id] = realloc(cpu_id[socket_id], sizeof(int) * ++sock_num[socket_id]);
                    cpu_id[socket_id][sock_num[socket_id]-1] = vcpuinfo[0].cpu;
                }
            }


           dtw = (int **)malloc(sizeof(int *)*(WINDOW_SIZE+1));
	   for (i=0; i<WINDOW_SIZE+1; i++)
	       dtw[i] = (int *)malloc(sizeof(int)*(WINDOW_SIZE+1));

            begin_stamp = 0;
            sig_time = -1;
            cal_num = 0;
            cal_lock = 1;
            terminated = 0;


            pthread_t profile_victim, profile_attacker, process_data;

            pthread_create(&profile_victim, NULL, perf_victim, NULL);
            pthread_create(&process_data, NULL, cal_data, NULL);
            pthread_create(&profile_attacker, NULL, perf_attacker, NULL);

            pthread_join(profile_victim, NULL);
            pthread_join(process_data, NULL);
            pthread_join(profile_attacker, NULL);

            if (terminated == 1) {
                write_pcr(1, 1);

                cpumap[0] = (1<<((core_id+1)%NUM_SOCKET));
                virDomainPinVcpu(dom, 0, cpumap, 1);
                
            }
        }

    }
    if (pcr_index == 2) {
            /* stop side-channel detection */
            strcpy(cmd_line, "ps ax | grep attestation_kernel | awk '{print $1\" \"$6\" \"$7}'");
            output = popen(cmd_line, "r");
            while(fgets(_line, sizeof(_line), output) != NULL) {
                sscanf(_line, "%d %s %s", &pid, arg1, arg2);
                if (strcmp(arg1, uuid) == 0 && strcmp(arg2, "1") == 0) {
                    kill(pid, SIGTERM);
                    break;
                }
            }
            /* reset the PCR values */
            write_pcr(1, 0);
    }
    if (pcr_index == 3) {
           /* retrieve result of side-channel detection */
            write_pcr(2, read_pcr(1));
            write_pcr(1, 0);
    }
    
    return 0;
}
