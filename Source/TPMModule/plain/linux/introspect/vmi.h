#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <openssl/md5.h>
#include <libvmi/libvmi.h>
#include <libvmi/events.h>

/**
 * default is using INT 3 for event notification
 * if MEM_EVENT is defined, then using EPT violation
 */

//#define MEM_EVENT


#define FILE_LOCATION "/root/tpm-emulator/PCR_VALUE"
#define IMAGE_LOCATION "/opt/stack/data/nova/instances/"
#define SYMBOL_LOCATION "/home/palms_admin/VMI"

/* task_struct offsets */
unsigned long tasks_offset;
unsigned long pid_offset;
unsigned long name_offset;


static int set_breakpoint(vmi_instance_t vmi, addr_t addr, pid_t pid) {

    uint32_t data;
    if (VMI_FAILURE == vmi_read_32_va(vmi, addr, pid, &data)) {
        printf("failed to read memory.\n");
        return -1;
    }
    data = (data & 0xFFFFFF00) | 0xCC;
    if (VMI_FAILURE == vmi_write_32_va(vmi, addr, pid, &data)) {
        printf("failed to write memory.\n");
        return -1;
    }
    return 0;
}


/**
 * Write the results to the PCR
 */
static int write_pcr(int pcr_index, int result) {
    FILE *pcr_file = fopen(FILE_LOCATION, "r+");
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

static int read_pcr(int pcr_index) {
    FILE *pcr_file = fopen(FILE_LOCATION, "r+");
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

static int interrupted = 0;

static void close_handler(int sig){
    interrupted = sig;
}

int introspect_process_list(char *name);

int introspect_syscall_check(char *name);

int introspect_idt_check(char *name);

int introspect_procfs_check(char *name);

int introspect_socket_trace(char *name);

int introspect_driver_trace(char *name);

int introspect_process_block(char *name);

int introspect_process_kill(vmi_instance_t vmi, char *name, vmi_pid_t);
