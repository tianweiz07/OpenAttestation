#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "vmi.h"

#define FILE_LOCATION "/root/tpm-emulator/PCR_VALUE"


int main (int argc, char *argv[]) {
    if (strncmp(argv[2], "FFFFFFFF", 100) == 0)
        return 0;

    int pcr_index = atoi(argv[2]);

    char *instance_name = argv[1];

    int result = -1;
    pid_t pid;
    char cmd_line[256];
    FILE *output;
    char _line[256];
    char arg1[256];
    char arg2[256];
    switch(pcr_index) {
        case 0:
            /* just read PCR values. do nothing*/
            break;
        case 1: 
            /* syscall integrity checking */
            result = introspect_syscall_check(instance_name);
            break;
        case 2: 
            /* interrupt table integrity checking */
            result = introspect_idt_check(instance_name);
            break;
        case 3:
            /* procfs integrity checking */
            result = introspect_procfs_check(instance_name);
            break;
        case 4:
            /* hidden process detection */
            result = introspect_process_list(instance_name);
        case 51:
            /* start process launch checking */
            pid = fork();
            if (pid == 0) {
                result = introspect_process_block(instance_name);
            }
            break;
        case 52:
            /* stop process launch checking */
            strcpy(cmd_line, "ps ax | grep attestation_kernel | awk '{print $1\" \"$6\" \"$7}'");
            output = popen(cmd_line, "r");
            while(fgets(_line, sizeof(_line), output) != NULL) {
                sscanf(_line, "%d %s %s", &pid, arg1, arg2);
                if (strcmp(arg1, instance_name) == 0 && strcmp(arg2, "51") == 0) {
                    kill(pid, SIGTERM);
                    break;
                }
            }
            /* reset the PCR values */
            write_pcr(5, 0);
            break;
        case 61:
            /* start kernel driver checking */
            pid = fork();
            if (pid == 0) {
                result = introspect_driver_trace(instance_name);
            }
            break;
        case 62:
            /* stop kernel driver checking */
            strcpy(cmd_line, "ps ax | grep attestation_kernel | awk '{print $1\" \"$6\" \"$7}'");
            output = popen(cmd_line, "r");
            while(fgets(_line, sizeof(_line), output) != NULL) {
                sscanf(_line, "%d %s %s", &pid, arg1, arg2);
                if (strcmp(arg1, instance_name) == 0 && strcmp(arg2, "61") == 0) {
                    kill(pid, SIGTERM);
                    break;
                }
            }
            /* reset the PCR values */
            write_pcr(6, 0);
            break;
        case 71:
            /* start network socket checking */
            pid = fork();
            if (pid == 0) {
                result = introspect_socket_trace(instance_name);
            }
            break;
        case 72:
            /* stop network socket checking */
            strcpy(cmd_line, "ps ax | grep attestation_kernel | awk '{print $1\" \"$6\" \"$7}'");
            output = popen(cmd_line, "r");
            while(fgets(_line, sizeof(_line), output) != NULL) {
                sscanf(_line, "%d %s %s", &pid, arg1, arg2);
                if (strcmp(arg1, instance_name) == 0 && strcmp(arg2, "71") == 0) {
                    kill(pid, SIGTERM);
                    break;
                }
            }
            /* reset the PCR values */
            write_pcr(7, 0);
            break;
    }

    return 0;
}
