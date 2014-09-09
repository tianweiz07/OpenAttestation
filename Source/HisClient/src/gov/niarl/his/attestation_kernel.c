#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int main(int argc, char *argv[])
{
    FILE *log = fopen("/root/log", "wr");
    assert(log);
    fprintf(log, "%s\n", argv[1]);
    fprintf(log, "%s\n", argv[2]);
    fclose(log);
    return 0;
}
