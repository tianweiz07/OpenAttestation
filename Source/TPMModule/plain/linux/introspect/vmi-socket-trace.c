#include "vmi.h"

unsigned char **ip_list;
int num_ip = 0;

unsigned long sockport_offset, sockaddr_offset;

/**
 * We are tracking two events: accept API and connect API
 * For EPT, we need two vmi_event_t structure: accept_enter_event for accept API; connect_enter_event for connect API
 * For INT3, we only need to use accept_enter_event to cover both accept and connect API
 * We also use accept_setp_event to denote the step event from accept or connect API
 */
vmi_event_t accept_enter_event;
vmi_event_t connect_enter_event;
vmi_event_t accept_step_event;

/**
 * virtual and physical address for sys_accept, sys_connect, and return address from sys_accept
 */
addr_t virt_sys_accept;
addr_t phys_sys_accept;

addr_t virt_sys_connect;
addr_t phys_sys_connect;

addr_t virt_leave_sys_accept = 0;
addr_t phys_leave_sys_accept = 0;

#ifdef MEM_EVENT
/**
 * This event is for mem event: when returning from sys_accept
 * INT does not need to specify new events
 */
vmi_event_t accept_leave_event;
#else
/**
 * original instruction data of sys_accept, sys_connect or return instruction from sys_accept
 */
uint32_t sys_accept_orig_data;
uint32_t sys_connect_orig_data;
uint32_t leave_sys_accept_orig_data;
#endif

/**
 * Denote the event type:
 * 1: entering sys_accept
 * 2: entering sys_connect
 * 3: return from sys_accept
 */
int socket_event_type = 0;


/**
 * callback function for accept_step_event
 */
event_response_t socket_step_cb(vmi_instance_t vmi, vmi_event_t *event) {
    /**
     * enable the accept_enter_event, connect_enter_event or accept_leave_event
     */
#ifdef MEM_EVENT
    if (socket_event_type == 1)
        vmi_register_event(vmi, &accept_enter_event);
    else if (socket_event_type == 2)
        vmi_register_event(vmi, &connect_enter_event);
    else if (socket_event_type == 3)
        vmi_register_event(vmi, &accept_leave_event);
#else
    accept_enter_event.interrupt_event.reinject = 1;
    if (socket_event_type == 2) {
        if (set_breakpoint(vmi, virt_sys_connect, 0) < 0) {
            printf("Could not set break points\n");
            vmi_destroy(vmi);
            exit(1);
        }
    } else if (socket_event_type == 3) {
        if (set_breakpoint(vmi, virt_leave_sys_accept, 0) < 0) {
            printf("Could not set break points\n");
            vmi_destroy(vmi);
            exit(1);
        }
    } 
#endif

    /** 
     * disable the accept_setp_event
     */
    vmi_clear_event(vmi, &accept_step_event, NULL);
    return 0;
}

/**
 * callback function for accept_enter_event, connect_enter_event and accept_leave_event
 */
event_response_t socket_enter_cb(vmi_instance_t vmi, vmi_event_t *event){
    addr_t event_addr;
#ifdef MEM_EVENT
    event_addr = event->mem_event.gla;
#else
    event_addr = event->interrupt_event.gla;
#endif

    /**
     * Case 1: entering the sys_accept syscall.
     * This event should be interrupted at most once, to retrieve the return address of inet_csk_accept
     */
    if (event_addr == virt_sys_accept) {
        reg_t cr3, rsi;
        vmi_get_vcpureg(vmi, &rsi, RSI, event->vcpu_id);
        vmi_get_vcpureg(vmi, &cr3, CR3, event->vcpu_id);

        vmi_pid_t pid = vmi_dtb_to_pid(vmi, cr3);

        /**
         * First time, the leave_sys_accept has not been set yet.
         */
        reg_t rbp;
        vmi_get_vcpureg(vmi, &rbp, RSP, event->vcpu_id);
        uint64_t rip;
        vmi_read_64_va(vmi, rbp, pid, &rip);

        virt_leave_sys_accept = rip;
        phys_leave_sys_accept = vmi_translate_kv2p(vmi, virt_leave_sys_accept);

        /**
         * Set the event notification when sys_accept finishes.
         */
#ifdef MEM_EVENT
        /**
         * initialize and register the event.
         * It turns out that the inet_stream_connect is on the same page with the return address of inet_csk_accept
         * so no need to set up and register a new event
         * Otherwise you need to set up an new event
         */

        memset(&accept_leave_event, 0, sizeof(vmi_event_t));
           
        accept_leave_event.type = VMI_EVENT_MEMORY;
        accept_leave_event.mem_event.physical_address = phys_leave_sys_accept;
        accept_leave_event.mem_event.npages = 1;
        accept_leave_event.mem_event.granularity = VMI_MEMEVENT_PAGE;
        accept_leave_event.mem_event.in_access = VMI_MEMACCESS_X;
        accept_leave_event.callback = socket_enter_cb;

        if ((virt_leave_sys_accept >> 12) != (virt_sys_accept >> 12) && (virt_leave_sys_accept >> 12) != (virt_sys_connect >> 12)) {
            if(vmi_register_event(vmi, &accept_leave_event) == VMI_FAILURE) {
                printf("Could not install socket handler3.\n");
                vmi_destroy(vmi);
                exit(1);
            }   
        }
#else
        if (VMI_FAILURE == vmi_read_32_va(vmi, virt_leave_sys_accept, 0, &leave_sys_accept_orig_data)) {
            printf("failed to read the original data.\n");
            vmi_destroy(vmi);
            exit(1);
        }

        /**
         * insert breakpoint into the syscall leave function
         */
        if (set_breakpoint(vmi, virt_leave_sys_accept, 0) < 0) {
            printf("Could not set break points\n");
            vmi_destroy(vmi);
            exit(1);
        }
#endif       
    }

    /**
     * Case 2: entering the sys_connect syscall.
     */
    else if (event_addr == virt_sys_connect) {
        reg_t cr3, rsi, rdx;
        vmi_get_vcpureg(vmi, &cr3, CR3, event->vcpu_id);
        vmi_get_vcpureg(vmi, &rsi, RSI, event->vcpu_id);
        vmi_get_vcpureg(vmi, &rdx, RDX, event->vcpu_id);
        vmi_pid_t pid = vmi_dtb_to_pid(vmi, cr3);

        /**
         * If this is called by the socket api, then the length of the parameter should be 16.
         */
        if ((int)rdx == 16) {
            uint8_t ip_addr[4];
            uint8_t port[2];
            vmi_read_8_va(vmi, rsi+2, pid, &port[0]);
            vmi_read_8_va(vmi, rsi+3, pid, &port[1]);
            vmi_read_8_va(vmi, rsi+4, pid, &ip_addr[0]);
            vmi_read_8_va(vmi, rsi+5, pid, &ip_addr[1]);
            vmi_read_8_va(vmi, rsi+6, pid, &ip_addr[2]);
            vmi_read_8_va(vmi, rsi+7, pid, &ip_addr[3]);
            char ip_server[256];
            sprintf(ip_server, "%d.%d.%d.%d", ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3]);
            int i;
            int iswhite = 0;
            for (i=0; i<num_ip; i++) {
                if (strcmp(ip_list[i], ip_server) == 0) {
                    iswhite = 1;
                    break;
                }
            }
            if (iswhite == 0) {
                /**
                 * Bypass the sys_connect with returning value as error.
                 */
                reg_t rsp;
                addr_t inst;
                vmi_get_vcpureg(vmi, &rsp, RSP, event->vcpu_id);
                vmi_set_vcpureg(vmi, rsp+8, RSP, event->vcpu_id);
                vmi_set_vcpureg(vmi, -1, RAX, event->vcpu_id);
                vmi_set_vcpureg(vmi, inst, RIP, event->vcpu_id);
                vmi_set_vcpureg(vmi, inst, RIP, event->vcpu_id);

                /**
                 * Update PCR values
                 */
                int val = read_pcr(7);
                write_pcr(7, val+1);
            }
        }
    }

    /**
     * Case 3: leaving the sys_accept syscall.
     */
    else if (event_addr == virt_leave_sys_accept) {
        reg_t cr3, rax;
        vmi_get_vcpureg(vmi, &cr3, CR3, event->vcpu_id);
        vmi_get_vcpureg(vmi, &rax, RAX, event->vcpu_id);
        vmi_pid_t pid = vmi_dtb_to_pid(vmi, cr3);

        /**
         * must check the pid is valid, and the return address is true.
         */

        if (rax > 0) {
            uint16_t port;
            uint32_t ip_addr;
           /**
            * socket struct offsets can be obtained by running findsocket in the tools folder
            */
            vmi_read_16_va(vmi, rax + sockport_offset, 0, &port);
            vmi_read_32_va(vmi, rax + sockaddr_offset, 0, &ip_addr);

            char ip_client[256];
            sprintf(ip_client, "%d.%d.%d.%d", ip_addr&0xff, (ip_addr>>8)&0xff, (ip_addr>>16)&0xff, (ip_addr>>24)&0xff);
            int i;
            int iswhite = 0;
            for (i=0; i<num_ip; i++) {
                if (strcmp(ip_list[i], ip_client) == 0) {
                    iswhite = 1;
                    break;
                }
            }
            if (iswhite == 0) {
                /**
                 * Bypass the sys_connect with returning value as error.
                 */
                reg_t rsp;
                addr_t inst;
                vmi_get_vcpureg(vmi, &rsp, RSP, event->vcpu_id);
                vmi_set_vcpureg(vmi, rsp+8, RSP, event->vcpu_id);
                vmi_set_vcpureg(vmi, -1, RAX, event->vcpu_id);
                vmi_set_vcpureg(vmi, inst, RIP, event->vcpu_id);
                vmi_set_vcpureg(vmi, inst, RIP, event->vcpu_id);

                /**
                 * Update PCR values
                 */
                int val = read_pcr(7);
                write_pcr(7, val+1);
            }
        }
    }

    /**
     * disable the syscall entry interrupt
     */
#ifdef MEM_EVENT
    vmi_clear_event(vmi, event, NULL);
    if ((event_addr >> 12) == (virt_sys_accept >> 12)) {
        if (event_addr != virt_sys_accept)
            socket_event_type = 1;
        else
            socket_event_type = 0;
    } else if ((event_addr >> 12) == (virt_sys_connect >> 12)) {
        socket_event_type = 2;
    } else if ((event_addr >> 12) == (virt_leave_sys_accept >> 12)) {
        socket_event_type = 3;
    } else {
        printf("Error in disabling event\n");
        return -1;
    }
#else
    event->interrupt_event.reinject = 0;
    if (event_addr == virt_sys_accept) {
        if (VMI_FAILURE == vmi_write_32_va(vmi, virt_sys_accept, 0, &sys_accept_orig_data)) {
            printf("failed to write memory.\n");
            vmi_destroy(vmi);
            exit(1);
        }
        socket_event_type = 1;
    } else if (event_addr == virt_sys_connect) {
        if (VMI_FAILURE == vmi_write_32_va(vmi, virt_sys_connect, 0, &sys_connect_orig_data)) {
            printf("failed to write memory.\n");
            vmi_destroy(vmi);
            exit(1);
        }
        socket_event_type = 2;
    } else if (event_addr == virt_leave_sys_accept) {
        if (VMI_FAILURE == vmi_write_32_va(vmi, virt_leave_sys_accept, 0, &leave_sys_accept_orig_data)) {
            printf("failed to write memory.\n");
            vmi_destroy(vmi);
            exit(1);
        }
        socket_event_type = 3;
    } else {
        printf("Error in disabling event\n");
        vmi_destroy(vmi);
        exit(1);
    }
#endif

    /**
     * set the single event to execute one instruction
     */
    vmi_register_event(vmi, &accept_step_event);

    return 0;
}

int introspect_socket_trace (char *name) {
    char _line[256];
    char _name[256];
    char _offset[256];

    char file_address[256];
    strcpy(file_address, SYMBOL_LOCATION);
    strcat(file_address, "/metadata");
    FILE *_file = fopen(file_address, "r");

    while(fgets(_line, sizeof(_line), _file) != NULL){
        sscanf(_line, "%s\t%s", _name, _offset);
        if (strcmp(_name, "sockport_offset") == 0)
            sockport_offset = (unsigned long)strtol(_offset, NULL, 0);
        if (strcmp(_name, "sockaddr_offset") == 0)
            sockaddr_offset = (unsigned long)strtol(_offset, NULL, 0);
    }
    fclose(_file);

    /**
     * get the list of IP addresses that are allowed for connections, from the file whitelist.txt.
     */
    num_ip = 0;

    strcpy(file_address, SYMBOL_LOCATION);
    strcat(file_address, "/whitelist.txt");
    _file = fopen(file_address, "r");

    while(fgets(_line, sizeof(_line), _file) != NULL){
        ip_list = realloc(ip_list, sizeof(char*) * ++num_ip);
        ip_list[num_ip-1] = (unsigned char*)malloc(256);
        sscanf(_line, "%s", ip_list[num_ip-1]);
    }
    fclose(_file);

    struct sigaction act;
    act.sa_handler = close_handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGHUP,  &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGINT,  &act, NULL);
    sigaction(SIGALRM, &act, NULL);

    /**
     * Initialize the vmi instance
     */
    vmi_instance_t vmi = NULL;
    if (vmi_init(&vmi, VMI_XEN | VMI_INIT_COMPLETE | VMI_INIT_EVENTS, name) == VMI_FAILURE){
        printf("Failed to init LibVMI library.\n");
        vmi_destroy(vmi);
        return 1;
    }


    /**
     * get the address of inet_csk_accept and inet_stream_connect from the sysmap
     */
    virt_sys_accept = vmi_translate_ksym2v(vmi, "inet_csk_accept");
    phys_sys_accept = vmi_translate_kv2p(vmi, virt_sys_accept);

    virt_sys_connect = vmi_translate_ksym2v(vmi, "inet_stream_connect");
    phys_sys_connect = vmi_translate_kv2p(vmi, virt_sys_connect);


    memset(&accept_enter_event, 0, sizeof(vmi_event_t));

#ifdef MEM_EVENT
    /**
     * iniialize the memory event for EPT violation.
     */
    accept_enter_event.type = VMI_EVENT_MEMORY;
    accept_enter_event.mem_event.physical_address = phys_sys_accept;
    accept_enter_event.mem_event.npages = 1;
    accept_enter_event.mem_event.granularity = VMI_MEMEVENT_PAGE;
    accept_enter_event.mem_event.in_access = VMI_MEMACCESS_X;
    accept_enter_event.callback = socket_enter_cb;

    connect_enter_event.type = VMI_EVENT_MEMORY;
    connect_enter_event.mem_event.physical_address = phys_sys_connect;
    connect_enter_event.mem_event.npages = 1;
    connect_enter_event.mem_event.granularity = VMI_MEMEVENT_PAGE;
    connect_enter_event.mem_event.in_access = VMI_MEMACCESS_X;
    connect_enter_event.callback = socket_enter_cb;
#else
    /**
     * iniialize the interrupt event for INT3.
     */
    accept_enter_event.type = VMI_EVENT_INTERRUPT;
    accept_enter_event.interrupt_event.intr = INT3;
    accept_enter_event.callback = socket_enter_cb;
#endif

    /**
     * iniialize the single step event.
     */
    memset(&accept_step_event, 0, sizeof(vmi_event_t));
    accept_step_event.type = VMI_EVENT_SINGLESTEP;
    accept_step_event.callback = socket_step_cb;
    accept_step_event.ss_event.enable = 1;
    SET_VCPU_SINGLESTEP(accept_step_event.ss_event, 0);

    /**
     * register the event.
     */
    if(vmi_register_event(vmi, &accept_enter_event) == VMI_FAILURE) {
        printf("Could not install socket handler.\n");
        goto exit;
    }

#ifdef MEM_EVENT
    if(vmi_register_event(vmi, &connect_enter_event) == VMI_FAILURE) {
        printf("Could not install socket handler.\n");
        goto exit;
    }
#else
    /**
     * store the original data for syscall entry function
     */
    if (VMI_FAILURE == vmi_read_32_va(vmi, virt_sys_accept, 0, &sys_accept_orig_data)) {
        printf("failed to read the original data.\n");
        vmi_destroy(vmi);
        return -1;
    }

    if (VMI_FAILURE == vmi_read_32_va(vmi, virt_sys_connect, 0, &sys_connect_orig_data)) {
        printf("failed to read the original data.\n");
        vmi_destroy(vmi);
        return -1;
    }

    /**
     * insert breakpoint into the syscall entry function
     */
    if (set_breakpoint(vmi, virt_sys_accept, 0) < 0) {
        printf("Could not set break points\n");
        goto exit;
    }
    if (set_breakpoint(vmi, virt_sys_connect, 0) < 0) {
        printf("Could not set break points\n");
        goto exit;
    }
#endif

    while(!interrupted){
        if (vmi_events_listen(vmi, 1000) != VMI_SUCCESS) {
            printf("Error waiting for events, quitting...\n");
            interrupted = -1;
        }
    }


exit:
#ifndef MEM_EVENT
    /**
     * write back the original data
     */
    if (VMI_FAILURE == vmi_write_32_va(vmi, virt_sys_accept, 0, &sys_accept_orig_data)) {
        printf("failed to write back the original data.\n");
    }
    if (VMI_FAILURE == vmi_write_32_va(vmi, virt_sys_connect, 0, &sys_connect_orig_data)) {
        printf("failed to write back the original data.\n");
    }
    if (virt_leave_sys_accept > 0) {
        if (VMI_FAILURE == vmi_write_32_va(vmi, virt_leave_sys_accept, 0, &leave_sys_accept_orig_data)) {
            printf("failed to write back the original data.\n");
        }
    }
#endif

    vmi_destroy(vmi);
    return 0;
}
