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
#include <libvirt/libvirt.h>

/**
 * default is using INT 3 for event notification
 * if MEM_EVENT is defined, then using EPT violation
 */

//#define MEM_EVENT


#define FILE_LOCATION "/root/tpm-emulator/PCR_VALUE"
#define IMAGE_LOCATION "/opt/stack/data/nova/instances/"

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

static int convert_name(char *uuid, char *name) {
  /*
   * This is the old method: get name from the libvirt.
   * New openstack does not have the libvirt xml file
   * so we have to use a new method: get name from libvirt C API
   */
/*
    char cursor;
    char file_address[256];

    strcpy(file_address, IMAGE_LOCATION);
    strcat(file_address, uuid);
    strcat(file_address, "/libvirt.xml");

    FILE *image_file = fopen(file_address, "rb");
    assert(image_file);

    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    char *ret;

    while ((read = getline(&line, &len, image_file)) != -1) {
        if (line=strstr(line, "<name>")) {
            name = strtok(line, ">");
            name = strtok(NULL, ">");
            name = strtok(name, "<");
            break;
        }
    }

    fclose(image_file);

    return 0;
*/

    virConnectPtr conn = NULL;
    virDomainPtr dom = NULL;

    conn = virConnectOpenReadOnly(NULL);
    if (conn == NULL) {
        fprintf(stderr, "Failed to connect to hypervisor\n");
        goto error;
    }

    dom = virDomainLookupByUUIDString(conn, uuid);
    if (dom == NULL) {
        fprintf(stderr, "Failed to find Domain %s\n", uuid);
        goto error;
    }

    strcpy(name, virDomainGetName(dom));
    return 0;
error:
    if (dom != NULL)
        virDomainFree(dom);
    if (conn != NULL)
        virConnectClose(conn);
    return -1;
}

static int interrupted = 0;

static void close_handler(int sig){
    interrupted = sig;
}

int introspect_process_list(char *uuid);

int introspect_syscall_check(char *uuid);

int introspect_idt_check(char *uuid);

int introspect_procfs_check(char *uuid);

int introspect_socket_trace(char *uuid);

int introspect_driver_trace(char *uuid);

int introspect_process_block(char *uuid);

int introspect_process_kill(vmi_instance_t vmi, char *uuid, vmi_pid_t);
