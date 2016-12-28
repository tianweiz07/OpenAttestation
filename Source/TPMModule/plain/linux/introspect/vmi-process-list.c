#include <sys/resource.h>

#include "vmi.h"

#define IPv4 "192.168.1.0/24"

int get_ip_addr(char *name, char *ip_addr) {
    char mac_addr[256];
    char cmd_line[256];
    FILE *output;

    strcpy(cmd_line, "xl network-list ");
    strcat(cmd_line, name);
    strcat(cmd_line, " | sed -n '2p' | awk '{print $3}'");
    output = popen(cmd_line, "r");
    fscanf(output, "%s", mac_addr);
    pclose(output);


    /* first, check if the ip address is in the ARP cache*/
    strcpy(cmd_line, "arp -n | grep -i ");
    strcat(cmd_line, mac_addr);
    strcat(cmd_line, " | awk '{print $1}'");

    output = popen(cmd_line, "r");
    fscanf(output, "%s", ip_addr);
    pclose(output);

    if (strlen(ip_addr) < 7) {

        /* not in the ARP cache. scanning the local networking for all up machines */
        strcpy(cmd_line, "nmap -sP ");
        strcat(cmd_line, IPv4);
        strcat(cmd_line, " | grep -B 2 -i ");
        strcat(cmd_line, mac_addr);
        strcat(cmd_line, " | grep Nmap | awk '{print $5}'");

        output = popen(cmd_line, "r");
        fscanf(output, "%s", ip_addr);
        pclose(output);

    }

    return 0;
}

int introspect_process_list (char *name) {
    vmi_instance_t vmi;
    addr_t list_head = 0, next_list_entry = 0, current_process = 0;
    vmi_pid_t pid = 0;
    char *procname = NULL;

    if (vmi_init(&vmi, VMI_XEN | VMI_INIT_COMPLETE | VMI_INIT_EVENTS, name) == VMI_FAILURE) {
        printf("Failed to init LibVMI library.\n");
        return 1;
    }


    char ip_addr[256];
    get_ip_addr(name, ip_addr);

    /**
     * Retrieve the untrusted process list from ssh
     * This requires the guest VM has host OS's public key
     * So the host OS can access guest OS without authentication
     */

    struct rlimit rlp;
    getrlimit(RLIMIT_NPROC, &rlp);
    char *pid_dict = (char *)calloc((int)rlp.rlim_max, sizeof(char));

    char cmd_line[256];
    char pid_str[256];
    FILE *output;
    strcpy(cmd_line, "ssh ");
    strcat(cmd_line, ip_addr);
    strcat(cmd_line, " ps aux | awk '{print $2}'");
    output = popen(cmd_line, "r");
    while (fscanf(output, "%s\n", pid_str) == 1){
        pid_dict[(atoi(pid_str))] = 1;
    }
    pclose(output);

    vmi_pause_vm(vmi);

    /**
     * get offsets of the kernel data structures
     * get the head of the task_struct 
     */

    char _line[256];
    char _name[256];
    char _offset[256];
    char file_address[256];
    strcpy(file_address, SYMBOL_LOCATION);
    strcat(file_address, "/metadata");
    FILE *_file; 
    switch(vmi_get_ostype(vmi)) {
        case VMI_OS_LINUX:
            _file = fopen(file_address, "r");
            while(fgets(_line, sizeof(_line), _file) != NULL){
                sscanf(_line, "%s\t%s", _name, _offset);
                if (strcmp(_name, "tasks_offset") == 0)
                    tasks_offset = (unsigned long)strtol(_offset, NULL, 0);
                if (strcmp(_name, "names_offset") == 0)
                    name_offset = (unsigned long)strtol(_offset, NULL, 0);
                if (strcmp(_name, "pid_offset") == 0)
                    pid_offset = (unsigned long)strtol(_offset, NULL, 0);
            }
            fclose(_file);


            list_head = vmi_translate_ksym2v(vmi, "init_task") + tasks_offset;

            break;
        case VMI_OS_WINDOWS:
            tasks_offset = vmi_get_offset(vmi, "win_tasks");
            name_offset = vmi_get_offset(vmi, "win_pname");
            pid_offset = vmi_get_offset(vmi, "win_pid");

            list_head = vmi_translate_ksym2v(vmi, "PsActiveProcessHead");

            break;
        default:
            vmi_resume_vm(vmi);
            goto exit;
    }


    if (tasks_offset == 0 || pid_offset == 0 || name_offset == 0) {
        printf("Failed to find offsets\n");
        vmi_resume_vm(vmi);
        goto exit;
    }

    next_list_entry = list_head;

    /** 
     * Retrieve the trusted process list from the kernel task_struct list
     * traverse the task lists and print out each process 
     * Check if each process pid is within the untrusted list
     * If not, then this process is hidden
     */
    do {
        current_process = next_list_entry - tasks_offset;
        vmi_read_32_va(vmi, current_process + pid_offset, 0, (uint32_t*)&pid);
        procname = vmi_read_str_va(vmi, current_process + name_offset, 0);
        if (!procname) {
            printf("Failed to find procname\n");
            vmi_resume_vm(vmi);
            goto exit;
        }

        if (pid_dict[pid] == 0) {
            pid_dict[pid] = -1;
        }

        free(procname);
        procname = NULL;

        if (vmi_read_addr_va(vmi, next_list_entry, 0, &next_list_entry) == VMI_FAILURE) {
            printf("Failed to read next pointer in loop at %"PRIx64"\n", next_list_entry);
            vmi_resume_vm(vmi);
            goto exit;
        }

    } while(next_list_entry != list_head);

    vmi_resume_vm(vmi);

    /**
     * Kill the hidden processes
     */
    int i;
    int count_proc = 0;
    for (i=0; i<(int)rlp.rlim_max; i++) {
        if (pid_dict[i] == -1) {
            introspect_process_kill(vmi, name, i);
            count_proc ++;
        }
    }

exit:
    vmi_destroy(vmi);
    write_pcr(4, count_proc);

    return 0;
}
