#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include <string.h>

#define DEST_FILE_LOCATION "/root/tpm-emulator/PCR_VALUE"
#define TEMP_FILE_LOCATION "/root/temp_log"

#define MEASURE_TIME		"0.1"
#define MEASURE_OPTION		":uk"
#define MEASURE_NUM		100

#define INTERVAL_NUM		10

#define EVENT_NUM		4

#define BANDWIDTH		10000000

double prob[INTERVAL_NUM];

char* event_list[EVENT_NUM];
uint64_t event_value[EVENT_NUM][MEASURE_NUM];

void init_event() {
	event_list[0] = "LLC-loads";
	event_list[1] = "LLC-stores";
	event_list[2] = "LLC-load-misses";
	event_list[3] = "LLC-store-misses";

	int i, j;
	for (i=0; i<EVENT_NUM; i++)
		for (j=0; j<MEASURE_NUM; j++)
			event_value[i][j] = 0;

	return;
}

pid_t map_uuid_pid(char* uuid) {
	pid_t vm_pid = -1;
	char cmd[256] = "";
	strcat(cmd, "ps aux | grep kvm | grep ");
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

		if (len>400) {
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

void process_data(char* output, int perf_mask) {
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
		for (i = 0; i<perf_mask; i++) {
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


	int prob_num[INTERVAL_NUM];
	for (i=0; i<INTERVAL_NUM; i++) {
		prob_num[i] = 0;
	}

	int sum = 0;

	if (perf_mask == 4) {
		int access_num, miss_num, prob_index;
		for (i=0; i<MEASURE_NUM; i++) {
			access_num = event_value[0][i] + event_value[1][i];
			miss_num = event_value[2][i] + event_value[3][i];
			if (event_value[0][i]*event_value[1][i]*event_value[2][i]*event_value[3][i] > 0) {
				if (miss_num<access_num) {
					sum ++;
					prob_index = miss_num*INTERVAL_NUM/access_num;
					prob_num[prob_index] ++;
				}
			}
		}
	}

	if (perf_mask == 1) {
		int access_num;
		int prob_index;
		for (i=0; i<MEASURE_NUM; i++) {
			access_num = event_value[0][i];
			if (access_num > 0) {
				sum ++;
				prob_index = access_num*INTERVAL_NUM/BANDWIDTH;
				prob_num[prob_index] ++;
			}
		}

	}

	if (sum == 0) {
		for (i=0; i<INTERVAL_NUM; i++)
			prob[i] = 0;
	}
	else {
		int cum_sum = 0;
		for (i=0; i<INTERVAL_NUM; i++) {
			cum_sum += prob_num[i];
			prob[i] = cum_sum*1.0/sum;
		}
	}
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

	char measure_event[256];
	memset(measure_event, 0, sizeof(measure_event));


	for (i=0; i<perf_mask; i++) {
		if (i != 0)
			strcat(measure_event, ",");

		strcat(measure_event, event_list[i]);
		strcat(measure_event, MEASURE_OPTION);
	}

	for (j=0; j<MEASURE_NUM; j++) {
		collect_data(measure_event, vm_pid, TEMP_FILE_LOCATION);
	}

	process_data(TEMP_FILE_LOCATION, perf_mask);

	FILE *report = fopen(DEST_FILE_LOCATION, "r+");
	assert(report);
	char cursor = fgetc(report);
	
	char per_value;
	double temp_value;
	for (i=0; i<INTERVAL_NUM; i++) {
		while (cursor != 58)
			cursor = fgetc(report);

		temp_value = prob[i];
		for (j=0; j<7; j ++) {
			sprintf(&per_value, "%d", (int)(temp_value));
			fputc(per_value, report);
			if((j%2) == 1)
				fputc(' ', report);
			temp_value = (temp_value-(int)(temp_value))*10;
		} 
	}

	fclose(report);

	return 0;
}
