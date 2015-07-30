#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include <string.h>

#define DEST_FILE_LOCATION "/root/tpm-emulator/PCR_VALUE"
#define TEMP_FILE_LOCATION "/root/temp_log"

#define MEASURE_CONCURRENCY	4
#define MEASURE_INTERVAL	"100"
#define MEASURE_TIME		"1"
#define MEASURE_OPTION		":uk"
#define MEASURE_NUM		10

#define EVENT_NUM		9

char* event_list[EVENT_NUM];
uint64_t event_value[EVENT_NUM][MEASURE_NUM];

void init_event() {
	event_list[0] = "instructions";
	event_list[1] = "L1-dcache-loads";
	event_list[2] = "L1-dcache-stores";
	event_list[3] = "L1-icache-loads";
	event_list[4] = "L1-icache-stores";
	event_list[5] = "LLC-loads";
	event_list[6] = "LLC-stores";
	event_list[7] = "LLC-load-misses";
	event_list[8] = "LLC-store-misses";

	int i, j;
	for (i=0; i<EVENT_NUM; i++)
		for (j=0; j<MEASURE_NUM; j++)
			event_value[i][j] = 0;

	return;
}

pid_t map_uuid_pid(char* uuid) {
	pid_t vm_pid = -1;
	char cmd[256] = "";
	strcat(cmd, "ps aux | grep ");
	strcat(cmd, uuid);
	strcat(cmd, ">");
	strcat(cmd, TEMP_FILE_LOCATION);
	system(cmd);

	FILE* fp;
	char* line = NULL;
	char* content; 
	size_t len = 0;
	ssize_t read;

	fp = fopen(TEMP_FILE_LOCATION, "r");
	if (fp == NULL)
		return vm_pid;        

	while ((read = getline(&line, &len, fp)) != -1) {
		content = strtok(line, " ");
		if (!strcmp(content, "libvirt+")) {
			content = strtok(NULL, " ");
			vm_pid = atoi(content);
			break;
		}
	}
	
	fclose(fp);
	remove(TEMP_FILE_LOCATION);
	if (line)
		free(line);

	return vm_pid;
}

void collect_data(char* event_name, pid_t pid, char* output) {
	char cmd[256] = "";
	char pid_str[256];
	sprintf(pid_str, "%d", pid);

	strcat(cmd, "perf stat -e ");
	strcat(cmd, event_name);
	strcat(cmd, " -I ");
	strcat(cmd, MEASURE_INTERVAL);
	strcat(cmd, " -p ");
	strcat(cmd, pid_str);
	strcat(cmd, " sleep ");
	strcat(cmd, MEASURE_TIME);
	strcat(cmd, " 2>>");
	strcat(cmd, output);

	system(cmd);
	return;
}

uint64_t format_number(char* data) {
	uint64_t ret = 0;
	uint64_t index = 1;

	int i = 2;
	int j;
	if (*(data-i)== 62)
		return ret;

	while ((j=*(data-i))!=32) {
		if (j != 44 ) {
			ret += index*(*(data-i)-48);
			index *= 10;
		}
		i++;
	}
	return ret;
}

int compare (const void* a, const void* b){
  return (*(int*)a - *(int*)b);
}

uint64_t median(uint64_t* data_array) {
	int length = 0;
	while (length < MEASURE_NUM) {
		if (data_array[length] == 0)
			break;
		length ++;
	}
	qsort(data_array, length, sizeof(uint64_t), compare);
	return data_array[length/2];
}

void process_data(char* output) {
        FILE* fp;
        char* line = NULL;
        char* content; 
        size_t len = 0;
        ssize_t read;


        fp = fopen(output, "r");
        if (fp == NULL)
                return;

	int i, j;
	char* ret;
        while ((read = getline(&line, &len, fp)) != -1) {
		for (i = 0; i<EVENT_NUM; i++) {
			if ((ret=strstr(line, event_list[i]))!=NULL) {
				uint64_t value = format_number(ret);
				for (j=0; j<MEASURE_NUM; j++) {
					if (event_value[i][j] == 0) {
						event_value[i][j] = value;
						break;
					}
				}
				break;
			}
		}
        }

        fclose(fp);
        if (line)
                free(line);
	return;
}

int main(int argc, char *argv[])
{
	pid_t vm_pid = map_uuid_pid(argv[1]);
	if (vm_pid == -1) 
		return -1;

	init_event();

	int perf_mask = strtol(argv[2], NULL, 16);
	int i, j;
	int index = 0;

	char measure_event[256];
	memset(measure_event, 0, sizeof(measure_event));


	for (i=0; i<EVENT_NUM; i++) {
		if ((perf_mask>>i) & 0x1) {
			if (index == 0) {
				strcat(measure_event, event_list[i]);
				strcat(measure_event, MEASURE_OPTION);
				index ++;
			}
			else if (index < MEASURE_CONCURRENCY - 1) {
				strcat(measure_event, ",");
				strcat(measure_event, event_list[i]);
				strcat(measure_event, MEASURE_OPTION);
				index ++;
			}
			else {
				strcat(measure_event, ",");
				strcat(measure_event, event_list[i]);
				strcat(measure_event, MEASURE_OPTION);

				collect_data(measure_event, vm_pid, TEMP_FILE_LOCATION);

				index = 0;
				memset(measure_event, 0, sizeof(measure_event));
			}
		}
	}
	if (index != 0) {
		collect_data(measure_event, vm_pid, TEMP_FILE_LOCATION);
	}

	process_data(TEMP_FILE_LOCATION);

	FILE *report = fopen(DEST_FILE_LOCATION, "r+");
	assert(report);
	char cursor = fgetc(report);
	
	char per_value;
	uint64_t median_value;
	for (i=0; i<EVENT_NUM; i++) {
		while (cursor != 58)
			cursor = fgetc(report);

		median_value = median(event_value[i]);
		for (j=0; j<16; j ++) {
			sprintf(&per_value, "%d", (unsigned int)(median_value%10));
			fputc(per_value, report);
			if((j%2) == 1)
				fputc(' ', report);
			median_value = median_value/10;
		} 
	}

	fclose(report);
	return 0;
}
