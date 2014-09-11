#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <openssl/md5.h>

#define FILE_LOCATION "/root/tpm-emulator/PCR_VALUE"
#define IMAGE_LOCATION "/opt/stack/data/nova/instances/"

// Compile: gcc -o attestation_kernel attestation_kernel.c -lcrypto -lssl

int main(int argc, char *argv[])
{
    int pcr_index = atoi(argv[2]);

    char file_address[256];
    strcpy(file_address, IMAGE_LOCATION);
    strcat(file_address, argv[1]);
    strcat(file_address, "/kernel");

    FILE *image_file = fopen(file_address, "rb");
    assert(image_file);

    MD5_CTX mdContext;
    int bytes;
    char image_value[1024];
    unsigned char hash_value[MD5_DIGEST_LENGTH];
    char per_value;

    MD5_Init(&mdContext);
    while ((bytes = fread(image_value, 1, 1024, image_file))!=0)
        MD5_Update(&mdContext, image_value, bytes);

    fclose(image_file);
    MD5_Final(hash_value, &mdContext);

    int i;
    FILE *report = fopen(FILE_LOCATION, "r+");
    assert(report);

    char cursor;
    int j = 0;
    while (j<= pcr_index) {
        cursor = fgetc(report);
        if (cursor == 58)
            j++;
    }

    for (i=0; i<MD5_DIGEST_LENGTH; i++) {
        sprintf(&per_value, "%02x", hash_value[i]);
        fputc(per_value, report);
        if ((i%2) == 1)
        fputc(' ', report);
    }
    return 0;
}
