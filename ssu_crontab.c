#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/types.h>
#include "ssu_crontab.h"

int main(void)
{
	int i, file_num, remove_num;
	long int e_point, f_point, l_point;
	char command_line[BUFLEN], execute_line[BUFLEN];
	char *tmp, *backup_file1, *backup_file2, tempbuf[BUFLEN*3];
	char exe_cycle[MAX_CYCLE][MAXNUM], exe_command[BUFLEN];
	time_t tm;
	FILE *fp, *lfp;
	struct timeval begin_t, end_t;

	gettimeofday(&begin_t, NULL);

	while(1)
	{
		// file 생성 및 오픈
		if(access("ssu_crontab_file", F_OK) < 0) 
			fp = fopen("ssu_crontab_file", "w+");
		else 
			fp = fopen("ssu_crontab_file", "r+");

		if(access("ssu_crontab_log", F_OK) < 0) 
			lfp = fopen("ssu_crontab_log", "w+");
		else 
			lfp = fopen("ssu_crontab_log", "r+");

		// 현재 ssu_crontab_file에 있는 내용 출력 
		file_num = 0;
		while(fscanf(fp, "%[^\n]", tempbuf) > 0){
			printf("%d. %s\n", file_num++, tempbuf);
			fgetc(fp);
		}
		printf("\n");

		printf("20162443>");

		// 명령어 라인
		memset(command_line, '\0', BUFLEN);
		fgets(command_line, BUFLEN, stdin);
		command_line[strlen(command_line)-1] = '\0';

		if(!strcmp(command_line, "")) // 엔터 입력 시 
			continue;

		if(!strncmp(command_line, "add", 3)) // add 명령어 처리
		{
			if(strlen(command_line) == 3){ // 예외 처리
				printf("Usage : add [EXECUTE CYLCE] [COMMAND]\n");
				continue;
			}

			// 실행 주기 분리
			tmp = strstr(command_line, " ");
			memset(execute_line, '\0', BUFLEN);
			strcpy(execute_line, ++tmp);
			execute_line[strlen(execute_line)] = '\n';

			tmp = strtok(command_line, " "); // 명령어만 분리
			for(i = 0; i < MAX_CYCLE; i++){ // 실행 주기 파싱 
				if((tmp = strtok(NULL, " ")) == NULL){ // 입력 개수 모자랄 경우
					printf("Wrong Execute Cycle format!!\n");
					break;
				}

				strcpy(exe_cycle[i], tmp);
			}

			if(i != MAX_CYCLE) // 실행 주기 예외 처리
				continue;

			if((tmp = strtok(NULL, "\0")) == NULL){ // 명령어 파싱
				printf("You didn't input command!!\n"); // 명령어 없을 시
				continue;
			}

			strcpy(exe_command, tmp);

			for(i = 0; i < MAX_CYCLE; i++){ // 실행 주기 파싱 
				if(check_exe_cycle(exe_cycle[i], i) < 0){ // 입력 형식 잘못된 경우 검사
					printf("Wrong Execute Cycle format!\n");
					break;
				} 
				if(check_wrong_character(exe_cycle[i]) < 0){ 
					printf("Wrong Execute Cycle format!!\n");
					break;
				} 
			}

			if(i != MAX_CYCLE) // 실행 주기 예외 처리
				continue;

			// 명령어 파일에 기록
			fseek(fp, 0, SEEK_END);
			fwrite(execute_line, strlen(execute_line), 1, fp);

			// log 파일에 기록
			time(&tm);
			tmp = ctime(&tm);
			tmp[strlen(tmp)-1] = '\0';
			sprintf(tempbuf, "[%s] %s %s", tmp, command_line, execute_line);
			fseek(lfp, 0, SEEK_END);
			fwrite(tempbuf, strlen(tempbuf), 1, lfp);
		}
		else if(!strncmp(command_line, "remove", 6)) // remove 명령어 처리
		{
			if(strlen(command_line) == 6){ // 인자 부족
				printf("Usage : remove [COMMAND_NUMBER]\n");
				continue;
			}

			tmp = strstr(command_line, " ");
			remove_num = atoi(++tmp);

			if(remove_num >= file_num || remove_num < 0){ // 잘못된 넘버 입력
				printf("Wrong COMMAND NUMBER\n");
				continue;
			}

			fseek(fp, 0, SEEK_END);
			l_point = ftell(fp); // 파일의 끝 오프셋 값
			fseek(fp, 0, SEEK_SET);

			for(i = 0; i < remove_num; i++){
				fscanf(fp, "%[^\n]", tempbuf);
				fgetc(fp);
			}

			e_point = ftell(fp); // 삭제 시작 오프셋 값
			fscanf(fp, "%[^\n]", tempbuf);
			fgetc(fp);
			f_point = ftell(fp); // 백업 파일의 시작 오프셋 값

			fseek(fp, 0, SEEK_SET);
			backup_file1 = malloc(sizeof(char) * e_point);
			fread(backup_file1, e_point, 1, fp);

			fseek(fp, f_point, SEEK_SET);
			backup_file2 = malloc(sizeof(char) * (l_point - f_point));
			fread(backup_file2, l_point-f_point, 1, fp);

			fp = freopen("ssu_crontab_file", "w+", fp);
			fwrite(backup_file1, e_point, 1, fp);
			fwrite(backup_file2, l_point-f_point, 1, fp);

			// log 파일에 기록
			time(&tm);
			tmp = ctime(&tm);
			tmp[strlen(tmp)-1] = '\0';
			fseek(lfp, 0, SEEK_END);
			fprintf(lfp, "[%s] remove %s\n", tmp, tempbuf);
		}
		else if(!strncmp(command_line, "exit", 4)) { break; } // exit 명령 처리
		else // 이 외의 명령어
		{
			printf("Wrong command!!\n");
			continue;
		}

		fclose(fp);
		fclose(lfp);
	}

	gettimeofday(&end_t, NULL);

	ssu_runtime(&begin_t, &end_t);

	exit(0);
}

int check_exe_cycle(char *cycle, int level)
{
	int high, low, i = 0, j;
	char op, exep1[MAX_CYCLE+1], exep2[MAX_CYCLE+1], exe_tmp[MAX_CYCLE+1];
	char tmp[MAXNUM], *token[MAXNUM/2];

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

	strcpy(tmp, cycle);

	// ',' 기준으로 토큰 분리
	token[i] = strtok(tmp, ",");
	while(token[i] != NULL)
		token[++i] = strtok(NULL, ",");

	for(j = 0; j < i; j++){
		memset(exep1, '\0', MAX_CYCLE+1);
		memset(exep2, '\0', MAX_CYCLE+1);
		op = '\0';

		sscanf(token[j], "%[^/]%c%s", exep1, &op, exep2);

		if(op != '/') // '/'이 없는 경우
		{
			sscanf(token[j], "%[^-]%c%s", exep1, &op, exep2);

			if(op != '-'){ // '-'이 없는 경우('*' or 숫자가 아니면 예외)
				if(strcmp(exep1, "*"))  
					if(atoi(exep1) > high || atoi(exep1) < low)
						return -1;
			}
			else if(op == '-'){ // '-'이 있는 경우 ('*'가 인자이거나 범위초과시 예외) 
				if(!strcmp(exep1, "*") || !strcmp(exep2, "*")) // '*'이 인자인 경우
					return -1;
				if(atoi(exep1) > high || atoi(exep1) < low || atoi(exep2) > high || atoi(exep2) < low) // 범위 초과한 경우
					return -1;
			}
		}
		else if(op == '/') // '/'이 있는 경우
		{
			if(atoi(exep2) > high || atoi(exep2) < low) // 스킵 값이 범위 초과인 경우
				return -1;

			strcpy(exe_tmp, exep1);
			sscanf(exe_tmp, "%[^-]%c%s", exep1, &op, exep2);

			if(op != '-'){ // '-'이 없으면
				if(strcmp(exep1, "*")) // '*'이 아니면 예외
					return -1;
			}
			else if(op == '-'){ // '-'이 있으면
				if(atoi(exep1) > high || atoi(exep1) < low || atoi(exep2) > high || atoi(exep2) < low) // 범위 초과한 경우
					return -1;
			}
		}
	}
	return 0;
}

int check_wrong_character(char* str)
{
	int i;

	for(i = 0; i < (int)strlen(str); i++)
		if(isdigit(str[i]) != TRUE)
			if(str[i] == ',' && str[i] == '-' && str[i] == '/')
				return -1;

	return 0;
}

void ssu_runtime(struct timeval *begin_t, struct timeval *end_t)
{
	end_t->tv_sec -= begin_t->tv_sec;

	if(end_t->tv_usec < begin_t->tv_usec){
		end_t->tv_sec--;
		end_t->tv_usec += SECOND_TO_MICRO;
	}

	end_t->tv_usec -= begin_t->tv_usec;
	printf("Runtime: %ld:%06ld(sec:usec)\n", end_t->tv_sec, end_t->tv_usec);
}
