#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <openssl/md5.h>

#define FILE_LOCATION "/root/tpm-emulator/PCR_VALUE"

// Compile: gcc -o attestation_kernel attestation_kernel.c -lcrypto -lssl

int main(int argc, char *argv[])
{
    int pcr_index = atoi(argv[2]);
    printf("index is: %d\n", pcr_index);

    char file_address[256];
    strcpy(file_address, "/opt/stack/data/nova/instances/");
    strcat(file_address, argv[1]);
    strcat(file_address, "/kernel");
    printf("file address: %s\n", file_address);

    FILE *image_file = fopen(file_address, "rb");
    assert(image_file);

    MD5_CTX mdContext;
    int bytes;
    char image_value[1024];
    unsigned char hash_value[MD5_DIGEST_LENGTH];
    char hash_value1[MD5_DIGEST_LENGTH];

    MD5_Init(&mdContext);
    while ((bytes = fread(image_value, 1, 1024, image_file))!=0)
        MD5_Update(&mdContext, image_value, bytes);

    fclose(image_file);
    MD5_Final(hash_value, &mdContext);

    int i;
    for (i=0; i<MD5_DIGEST_LENGTH; i++)
        sprintf(hash_value1+i, "%02x", hash_value[i]);
    printf("Hash Value: %s\n", hash_value1);

    return 0;
