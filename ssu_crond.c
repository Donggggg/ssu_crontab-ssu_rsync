#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "ssu_crontab.h"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
FILE *lfp; // 로그 파일 포인터

int main(void)
{
	int count;
	char tempbuf[BUFLEN][BUFLEN];
	time_t now;
	FILE *fp;
	struct stat statbuf;
	pthread_t tid[BUFLEN];

	if(access("ssu_crontab_log", F_OK) < 0)
		lfp = fopen("ssu_crontab_log", "w+");
	else
		lfp = fopen("ssu_crontab_log", "r+");

	while(1){ // 주기적으로 실행
		if((fp = fopen("ssu_crontab_file", "r")) == NULL)
			continue;

		fstat(fp->_fileno, &statbuf);
		now = statbuf.st_mtime;
		count = 0;
		fseek(fp, 0, SEEK_SET);

		// 명령어별 쓰레드 생성
		while(fscanf(fp, "%[^\n]", tempbuf[count]) > 0){ 
			pthread_create(&tid[count], NULL, execute_command, (void *)tempbuf[count]);
			fgetc(fp);
			count++;
		}

		while(1){ // crontab_file이 변경되는지 검사
			stat("ssu_crontab_file", &statbuf);
			if(now != statbuf.st_mtime){ // 변경이 감지된 경우
				while(count--) // 쓰레드 회수
					pthread_cancel(tid[count]);
				break;
			}
		}
	}
}

void *execute_command(void *arg) // 쓰레드 함수
{
	int i;
	int *time_table[5];
	char *command_line, execute_line[BUFLEN];
	char *tmp, *time_str;
	time_t now, exe_time;
	struct tm tm;

	// 타임테이블 동적할당
	time_table[0] = malloc(sizeof(int) * 60); // 분
	time_table[1] = malloc(sizeof(int) * 24); // 시
	time_table[2] = malloc(sizeof(int) * 32); // 일
	time_table[3] = malloc(sizeof(int) * 13); // 월
	time_table[4] = malloc(sizeof(int) * 8); // 요일

	tmp = (char*)arg;
	command_line = tmp;

	for(i = 0; i < MAX_CYCLE; i++){ // 실행 주기와 명령어 분리
		command_line = strchr(command_line, ' ');
		command_line++;
	}
	memcpy(execute_line, tmp, strlen(tmp) - strlen(command_line));

	make_time_table(execute_line, time_table); // 타임 테이블 생성

	// 현재 시간 세팅
	now = time(NULL);
	tm = *localtime(&now);
	tm.tm_sec = 0;

	while(1) // 주기에 따른 실행 시간에 명령어 실행
	{
		tm.tm_min++;
		exe_time = mktime(&tm);
		tm = *localtime(&exe_time);

		if(time_table[0][tm.tm_min] && time_table[1][tm.tm_hour] && time_table[2][tm.tm_mday] && time_table[3][tm.tm_mon+1] && time_table[4][tm.tm_wday]) // 실행되야 하는 시간을 찾으면
		{
			now = time(NULL);
			if(exe_time-now < 0){
				tm = *localtime(&now);
				tm.tm_sec = 0;
				continue;
			}
			sleep(exe_time - now); // 대기 시간
			system(command_line); // 명령 실행

			pthread_mutex_lock(&mutex);
			time_str = asctime(&tm);
			time_str[24] = '\0';
			fseek(lfp, 0, SEEK_END);
			fprintf(lfp, "[%s] run %s\n", time_str, tmp);
			fflush(lfp);
			pthread_mutex_unlock(&mutex);
		}
	}
}

void make_time_table(char *str, int **time_table)
{
	int i = 0;
	char exe_cycle[MAX_CYCLE][MAXNUM];
	char *tmp;

	// 실행 주기 항목별 분리
	tmp = strtok(str, " ");
	while(tmp != NULL){
		strcpy(exe_cycle[i++], tmp);
		tmp = strtok(NULL, " ");
	}

	for(i = 0; i < 5; i++) // 각 항목에 대한 테이블 세팅
		set_time_table(exe_cycle[i], i, time_table);
}

void set_time_table(char *cycle, int level, int *table[5])
{
	int high, low, i = 0, j, k, count, skip;
	char op, exep1[MAX_CYCLE+1], exep2[MAX_CYCLE+1], exe_tmp[MAX_CYCLE+1];
	char *token[MAXNUM/2];

	switch(level){ // 항목별 최대, 최소 설정
		case 0 :
			high = 59;
			low = 0;
			break;
		case 1 :
			high = 23;
			low = 0;
			break;
		case 2 :
			high = 31;
			low = 1;
			break;
		case 3 : 
			high = 12;
			low = 1;
			break;
		case 4 :
			high = 6;
			low = 0;
			break;
	}

	for(j = low; j <= high; j++) // 테이블 초기화
		table[level][j] = FALSE;

	// ','를 기준으로 토크나이징
	token[i] = strtok(cycle, ",");
	while(token[i] != NULL)
		token[++i] = strtok(NULL, ",");

	// 모든 토큰에 대한 정보 table에 세팅
	for(j = 0; j < i; j++){
		memset(exep1, '\0', MAX_CYCLE+1);
		memset(exep2, '\0', MAX_CYCLE+1);
		memset(exe_tmp, '\0', MAX_CYCLE+1);
		op = '\0';

		sscanf(token[j], "%[^/]%c%s", exep1, &op, exep2);

		if(op != '/') // '/'이 없는 경우
		{
			sscanf(token[j], "%[^-]%c%s", exep1, &op, exep2);

			if(op != '-') // '-'이 없는 경우 >> '*'이나 숫자
			{
				if(!strcmp(exep1, "*")) // '*'이면 해당 항목 전부 체크
					for(k = low; k <= high; k++)
						table[level][k] = TRUE;
				else // 숫자면 해당 숫자만 체크
					table[level][atoi(exep1)] = TRUE;
			}
			else if(op == '-') // '-'이 있는 경우 
				for(k = atoi(exep1); k <= atoi(exep2); k++) // 해당 범위 전부 체크 
					table[level][k] = TRUE;
		}
		else if(op == '/') // '/'이 있는 경우
		{
			strcpy(exe_tmp, exep1);
			skip = atoi(exep2); // skip값 세팅
			count = 1;
			sscanf(exe_tmp, "%[^-]%c%s", exep1, &op, exep2);

			if(op != '-') // '-'이 없는 경우
			{
				for(k = low; k <= high; k++){ // 무조건 '*'
					if(count % skip == 0)
						table[level][k] = TRUE;
					count++;
				}
			}
			else if(op == '-') // '-'이 있는 경우
			{
				for(k = atoi(exep1); k <= atoi(exep2); k++){
					if(count % skip == 0)
						table[level][k] = TRUE;
					count++;
				}
			}
		}
	}
}
