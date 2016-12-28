#include "vmi.h"

int introspect_procfs_check(char *name)
{
    vmi_instance_t vmi;
    addr_t init_net_addr, pde_addr, name_addr, tcp_addr, show_addr;
    addr_t stext, etext;
    int count_ops = 0;
    char *filename = NULL;
    int got_tcp = 0;

    if (vmi_init(&vmi, VMI_XEN | VMI_INIT_COMPLETE, name) == VMI_FAILURE) {
        printf("Failed to init LibVMI library.\n");
        return -1;
    }

    /**
     * /proc/network structure offsets. 
     * These offset values can be retrieved by running findproc tool inside the guest OS
     */

    unsigned long procnet_offset, subdir_offset, name_offset, next_offset, data_offset, show_offset;

    char _line[256];
    char _name[256];
    char _offset[256];

    char file_address[256];
    strcpy(file_address, SYMBOL_LOCATION);
    strcat(file_address, "/metadata");
    FILE *_file = fopen(file_address, "r");

    while(fgets(_line, sizeof(_line), _file) != NULL){
        sscanf(_line, "%s\t%s", _name, _offset);
        if (strcmp(_name, "procnet_offset") == 0)
            procnet_offset = (unsigned long)strtol(_offset, NULL, 0);
        if (strcmp(_name, "subdir_offset") == 0)
            subdir_offset = (unsigned long)strtol(_offset, NULL, 0);
        if (strcmp(_name, "name_offset") == 0)
            name_offset = (unsigned long)strtol(_offset, NULL, 0);
        if (strcmp(_name, "next_offset") == 0)
            next_offset = (unsigned long)strtol(_offset, NULL, 0);
        if (strcmp(_name, "data_offset") == 0)
            data_offset = (unsigned long)strtol(_offset, NULL, 0);
        if (strcmp(_name, "show_offset") == 0)
            show_offset = (unsigned long)strtol(_offset, NULL, 0);

    }
    fclose(_file);

    /**
     * get init_net address
     */
    init_net_addr = vmi_translate_ksym2v(vmi, "init_net");

    /**
     * get /proc/network address
     */
    vmi_read_addr_va(vmi, init_net_addr+procnet_offset, 0, &pde_addr);
    vmi_read_addr_va(vmi, pde_addr + subdir_offset, 0, &pde_addr);

    /**
     * interate all the directories inside the /proc/network until getting the tcp directory
     */
    do {
        vmi_read_addr_va(vmi, pde_addr + name_offset, 0, &name_addr);
        filename = vmi_read_str_va(vmi, name_addr, 0);
        if (!strncmp(filename, "tcp", sizeof("tcp"))) {
                got_tcp = 1;
                break;
        }
        vmi_read_addr_va(vmi, pde_addr + next_offset, 0, &pde_addr);
    } while (pde_addr);

    if (!got_tcp)
        goto exit;

    /**
     * get the show function address
     */
    vmi_read_addr_va(vmi, pde_addr + data_offset, 0, &tcp_addr);
    vmi_read_addr_va(vmi, tcp_addr + show_offset, 0, &show_addr);
    
    /**
     * get the kernel function boundary
     */
    stext = vmi_translate_ksym2v(vmi, "_stext");
    etext = vmi_translate_ksym2v(vmi, "_etext");
    
    if (show_addr < stext || show_addr > etext) {
        /**
         * show address is not exported to symbol file. 
         * The cloud provider needs to add the following entry (for Ubuntu 10.10):
         * ffffffff814ff470 T seq_ops_show
         */
        show_addr = vmi_translate_ksym2v(vmi, "seq_ops_show");
        vmi_write_addr_va(vmi, tcp_addr+show_offset, 0, &show_addr);
        count_ops ++;
    }

exit:
    vmi_destroy(vmi);
    write_pcr(3, count_ops);
    return count_ops;
}
