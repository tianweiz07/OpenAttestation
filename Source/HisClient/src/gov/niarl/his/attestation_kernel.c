#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define FILE_LOCATION "/root/tpm-emulator/PCR_VALUE"

int main(int argc, char *argv[])
{
    int pcr_index = 3;
    int mask_index = 2;
    int pcr_value = 240;

    FILE *report = fopen(FILE_LOCATION, "r+");
    assert(report);

    char cursor;
    int i = 0;
    while (i<= pcr_index)
    {
        cursor = fgetc(report);
        if (cursor == 58)
            i++;
    }

    fseek(report, mask_index*3, SEEK_CUR);

    fprintf(report, "%x", pcr_value);
    fclose(report);
    return 0;
}
