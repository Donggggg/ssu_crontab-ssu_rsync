#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <utime.h>
#include "ssu_rsync.h"

int mOption, rOption, tOption; // 옵션 체크용 변수
int isFinished; // 동기화작업 종료 체크용 변수
char saved_path[BUFLEN]; // 실행 디렉토리
char command[BUFLEN]; // 로그 기록용 명령어 변수
Target *head, *tail; // 동기화 파일 리스트 head 및 tail

int main(int argc, char *argv[])
{
	int i;
	char opt;
	char src_path[BUFLEN], dst_path[BUFLEN];
	struct stat statbuf;
	struct timeval begin_t, end_t;

	gettimeofday(&begin_t, NULL);

	if(!(argc == 4 || argc == 3)){ // Wrong arguments
		fprintf(stderr, "usage : %s [option] <src> <dst>\n", argv[0]);
		exit(1);
	}

	// 현재 경로 및 src, dst 절대 경로 저장
	memset(saved_path, 0, BUFLEN);
	getcwd(saved_path, BUFLEN);
	realpath(argv[argc-2], src_path);
	realpath(argv[argc-1], dst_path);

	while((opt = getopt(argc, argv, "mrt")) != -1) // 옵션 파싱
	{
		switch(opt){
			case 'm' :
				mOption = TRUE;
				stat(src_path, &statbuf);

				if(!S_ISDIR(statbuf.st_mode)){ // m 옵션 예외 처리 
					fprintf(stderr, "-m option helps only directory\n");
					exit(1);
				}

				break;
			case 'r' :
				rOption = TRUE;
				stat(src_path, &statbuf);

				if(!S_ISDIR(statbuf.st_mode)){ // r 옵션 예외 처리
					fprintf(stderr, "-r option helps only directory\n");
					exit(1);
				}

				break;
			case 't' :
				tOption = TRUE;
				break;
			default :
				fprintf(stderr, "Wrong option! Only -r -t -m\n");
				exit(1);
				break;
		}
	}

	// 로그 기록용 명령어 병합 작업
	for(i = 0; i < argc; i++){
		strcat(command, argv[i]);
		strcat(command, " ");
	}

	if(access(src_path, F_OK) < 0 || access(dst_path, F_OK)){ // 접근 권한 및 존재 여부 검사
		fprintf(stderr, "%s or %s isn't exist or permission denied\n", src_path, dst_path);
		exit(1);
	}

	stat(dst_path, &statbuf);

	if(!S_ISDIR(statbuf.st_mode)){ // dst가 디렉토리가 아니면 예외 처리
		fprintf(stderr, "%s is not directory\n", dst_path);
		exit(1);
	}

	signal(SIGINT, recover_Targets); // SIGINT 시그널 처리 등록

	//-----------------------------------------------------------//
	isFinished = FALSE; // 동기화 시작
	if(tOption) // t옵션이 설정되면 tar로 동기화 시작
		synchronize_by_tar(src_path, dst_path);
	else if(rOption) // r옵션이 설정되면 재귀 동기화 시작
		do_rOption(src_path, dst_path);
	else
		synchronize(src_path, dst_path); // 동기화 시작
	isFinished = TRUE; // 동기화 종료
	//-----------------------------------------------------------//

	gettimeofday(&end_t, NULL);
	ssu_runtime(&begin_t, &end_t);

	exit(0);
}

int synchronize(char *src_path, char *dst_path)
{
	int i;
	int dst_file_num, src_file_num;
	char src_file_path[BUFLEN+MAXNUM], tempfile[BUFLEN+5];
	Target *tmp;
	struct dirent **dst_namelist, **src_namelist;
	struct stat statbuf;

	// dst 디렉토리 스캔 
	dst_file_num = scandir(dst_path, &dst_namelist, NULL, alphasort);
	stat(src_path, &statbuf);
	chdir(dst_path); // 목적지 디렉토리로 이동

	if(S_ISDIR(statbuf.st_mode)){ // 동기화 대상이 디렉토리인 경우
		src_file_num = scandir(src_path, &src_namelist, NULL, alphasort); // 동기화 대상 파일 리스트 저장

		// 각 파일마다 동기화 작업 수행
		for(i = 0; i < src_file_num; i++){
			if(!strcmp(src_namelist[i]->d_name, ".") || !strcmp(src_namelist[i]->d_name, ".."))
				continue;

			// 해당 파일 절대 경로 저장
			strcpy(src_file_path, src_path);
			strcat(src_file_path, "/");
			strcat(src_file_path, src_namelist[i]->d_name);

			stat(src_file_path, &statbuf);

			if(!S_ISDIR(statbuf.st_mode)){ // 해당 파일이 디렉토리가 아니면 동기화 
				if((tmp = synchronize_target(src_file_path, dst_namelist, dst_file_num)) == NULL) // 동기화 대상이 아닌 경우
					continue;
				else{
					// 링크드리스트 세팅
					tmp->next = NULL;
					if(head == NULL){
						head = tmp;
						tail = head;
					}
					else{
						tail->next = tmp;
						tail = tail->next;
					}
				}
			}
		}
	}
	else{ // 동기화 대상이 디렉토리가 아닌 파일인 경우
		if((head = synchronize_target(src_path, dst_namelist, dst_file_num)) != NULL)
			head->next = NULL;
	}

	// m옵션 처리
	if(mOption)
		do_mOption(src_namelist, src_file_num, dst_namelist, dst_file_num);

	// .tmp파일 삭제 
	tmp = head;
	while(tmp != NULL){
		sprintf(tempfile, "%s.tmp", tmp->name);
		stat(tempfile, &statbuf);

		if(S_ISDIR(statbuf.st_mode))
			rmdirs(tempfile);
		else
			remove(tempfile);

		tmp = tmp->next;
	}

	write_log(); // log파일에 기록

	return 0;
}

Target *synchronize_target(char *target, struct dirent **namelist, int count)
{
	int i;
	char *tmp;
	char target_name[MAXNUM];
	Target *new = malloc(sizeof(Target));
	struct stat tstatbuf, dstatbuf;

	// 동기화될 파일명 추출
	tmp = target;
	while((tmp = strstr(tmp, "/")) != NULL){ 
		tmp++;
		strcpy(target_name, tmp);
	}

	stat(target, &tstatbuf);

	// dst의 파일과 비교하여 동기화 적정 대상 탐색
	for(i = 0; i < count; i++)
	{
		if(!strcmp(target_name, namelist[i]->d_name)){ // 파일명이 같은 경우
			stat(namelist[i]->d_name, &dstatbuf);

			// 수정시간과 사이즈가 같으면 같은 파일으로 처리
			if(tstatbuf.st_mtime == dstatbuf.st_mtime && tstatbuf.st_size == dstatbuf.st_size)
				break; 
		}
	}

	if(i != count) // 같은 파일이 있는 경우 동기화 취소
		return NULL;
	else{ // 같은 파일이 없는 경우 동기화 진행
		synchronize_file(target, target_name);
		strcpy(new->name, target_name);
		new->size = tstatbuf.st_size;
		return new;
	}
}

void do_rOption(char *src_path, char *dst_path)
{
	int i, level = 0;
	char tempfile[BUFLEN+5];
	struct stat statbuf;
	Target *tmp;

	chdir(dst_path);

	// 동기화 경로 단계 탐색
	for(i = 0; i < (int)strlen(dst_path); i++)
		if(dst_path[i] == '/')
			level++;

	synchronize_recursive(src_path, dst_path, level); // 동기화

	// .tmp파일 삭제 
	tmp = head;
	while(tmp != NULL){
		sprintf(tempfile, "%s.tmp", tmp->name);
		stat(tempfile, &statbuf);

		if(S_ISDIR(statbuf.st_mode))
			rmdirs(tempfile);
		else
			remove(tempfile);

		tmp = tmp->next;
	}

	write_log();
}

void synchronize_recursive(char *src_path, char *dst_path, int level)
{
	int i, j;
	int src_count, dst_count;
	char *tmp, abs_path[BUFLEN], tmp_path[BUFLEN+256], dst_file_tmp[BUFLEN+256];
	char src_file[BUFLEN+256], dst_file[BUFLEN+256];
	struct stat statbuf, tstatbuf;
	struct dirent **src_namelist, **dst_namelist;
	Target *new;

	// 상대 경로 추출
	tmp = dst_path;
	for(i = 0; i < level; i++){
		tmp = strstr(tmp, "/");
		tmp++;
		strcpy(abs_path, tmp);
	}

	if((tmp = strstr(abs_path, "/")) == NULL)
		abs_path[0] = '\0';
	else{
		tmp++;
		strcpy(abs_path, tmp);
	}

	src_count = scandir(src_path, &src_namelist, NULL, alphasort);
	dst_count = scandir(dst_path, &dst_namelist, NULL, alphasort);

	for(i = 0; i < src_count; i++){
		if(!strcmp(src_namelist[i]->d_name, ".") || !strcmp(src_namelist[i]->d_name, ".."))
			continue;

		sprintf(src_file, "%s/%s", src_path, src_namelist[i]->d_name);

		if(abs_path[0] == '\0') // 루트 디렉토리인 경우
			strcpy(dst_file, src_namelist[i]->d_name);
		else // 그 외의 경우
			sprintf(dst_file, "%s/%s", abs_path, src_namelist[i]->d_name);

		stat(src_file, &statbuf);

		// src의 파일이 디렉토리면 재귀 호출
		if(S_ISDIR(statbuf.st_mode)){
			if(access(dst_file, F_OK) < 0){
				mkdir(dst_file, statbuf.st_mode);
				new = malloc(sizeof(Target));
				strcpy(new->name, dst_file);
				new->size = statbuf.st_size;
				new->isCreatDir = TRUE;
				new->next = NULL;

				if(head == NULL){
					head = new;
					tail = new;
				}
				else{
					tail->next = new;
					tail = tail->next;
				}
			}

			sprintf(tmp_path, "%s/%s", dst_path, src_namelist[i]->d_name);
			synchronize_recursive(src_file, tmp_path, level);
			continue;
		}

		for(j = 0; j < dst_count; j++){
			if(!strcmp(dst_namelist[j]->d_name, ".") || !strcmp(dst_namelist[j]->d_name, ".."))
				continue;

			if(!strcmp(src_namelist[i]->d_name, dst_namelist[j]->d_name)){
				if(abs_path[0] == '\0') // 루트 디렉토리인 경우
					strcpy(dst_file_tmp, dst_namelist[j]->d_name);
				else // 그 외의 경우 
					sprintf(dst_file_tmp, "%s/%s", abs_path, dst_namelist[j]->d_name);

				stat(dst_file_tmp, &tstatbuf);

				if(tstatbuf.st_mtime == statbuf.st_mtime && tstatbuf.st_size == statbuf.st_size)
					break;
			}
		}

		if(j != dst_count) // 동기화 대상이 아니면 스킵
			continue;
		else{ // 동기화 대상인 경우 동기화
			synchronize_file(src_file, dst_file);
			new = malloc(sizeof(Target));
			strcpy(new->name, dst_file);
			new->size = statbuf.st_size;
			new->next = NULL;

			if(head == NULL){
				head = new;
				tail = new;
			}
			else{
				tail->next = new;
				tail = tail->next;
			}
		}
	}
}

void synchronize_file(char *target, char *copyfile)
{
	char c;
	char tempfile[BUFLEN+5];
	FILE *src_fp, *dst_fp, *tmp_fp;
	mode_t mode;
	struct stat statbuf, copystat;
	struct utimbuf time_buf;

	// 동기화 파일 수정시간 및 접근시간 변경
	stat(target, &statbuf);
	stat(copyfile, &copystat);
	mode = statbuf.st_mode;
	time_buf.actime = statbuf.st_atime;
	time_buf.modtime = statbuf.st_mtime;

	// 동기화 파일이 이미 존재하면 .tmp파일을 생성하여 백업
	if(access(copyfile, F_OK) == 0 && !S_ISDIR(copystat.st_mode)){ // 디렉토리가 아닌 경우
		dst_fp = fopen(copyfile, "r");
		sprintf(tempfile, "%s.tmp", copyfile);
		tmp_fp = fopen(tempfile, "w+");

		while(!feof(dst_fp)){
			c = fgetc(dst_fp);
			if(feof(dst_fp))
				break;
			fputc(c, tmp_fp);
		}

		fclose(dst_fp);
		fclose(tmp_fp);
	}
	else if(access(copyfile, F_OK) == 0 && S_ISDIR(copystat.st_mode)){ // 디렉토리인 경우
		sprintf(tempfile, "%s.tmp", copyfile);
		rename(copyfile, tempfile);
	}

	src_fp = fopen(target, "r");
	dst_fp = fopen(copyfile, "w+");

	if(src_fp == NULL || dst_fp == NULL) // fopen 예외 처리
	{
		fprintf(stderr, "fopen error for %s or %s\n", target, copyfile);
		exit(1);
	}

	// 파일 복사 
	while(!feof(src_fp)){
		c = fgetc(src_fp);
		if(feof(src_fp))
			break;
		fputc(c, dst_fp);
	}
	chmod(copyfile, mode); // 파일 모드 복사 

	fclose(src_fp);
	fclose(dst_fp);

	utime(copyfile, &time_buf); // utime 호출로 수정 및 접근 시간 변경
}

void write_log()
{
	char timestring[TIMESTRLEN];
	FILE *fp;
	time_t now;
	Target *node = head;

	chdir(saved_path);

	if((fp = fopen("ssu_rsync_log", "a")) == NULL){ // 쓰기 모드로 오프셋을 끝으로 옮겨 오픈
		fprintf(stderr, "fopen error for ssu_rsync_log\n");
		exit(1);
	}

	// 시간 포맷 저장
	now = time(NULL);
	strncpy(timestring, ctime(&now), TIMESTRLEN);
	timestring[TIMESTRLEN-2] = '\0';
	fprintf(fp, "[%s] %s\n", timestring, command);

	// 파일 동기화 기록 저장
	while(node != NULL){
		if(node->isCreatDir == TRUE){
			node = node->next;
			continue;
		}

		if(((int)node->size < 0)) // 삭제된 경우 
			fprintf(fp, "	%s delete\n", node->name);
		else
			fprintf(fp, "	%s %ldbytes\n", node->name, node->size);
		node = node->next;
	}

	fclose(fp);
}

void recover_Targets(int signo)
{
	char tempfile[BUFLEN+5];
	struct stat statbuf;
	Target *node = head;

	if(signo == SIGINT)
	{
		if(isFinished) // 동기화 작업이 종료되었으면 복구 안함
			return ;

		if(tOption){ // t옵션이 세팅된 경우의 복구 작업
			remove("bundle.tar");
			chdir(saved_path);
			remove("tempfile");
			exit(1);
		}

		while(node != NULL){
			sprintf(tempfile, "%s.tmp", node->name);
			stat(tempfile, &statbuf);

			if(access(tempfile, F_OK) == 0){ // .tmp파일 있으면 원본 파일에 복원
				if(S_ISDIR(statbuf.st_mode))
					remove(node->name);
				rename(tempfile, node->name);
			}
			else{ // 아닌 경우에는 즉시 제거
				stat(node->name, &statbuf);
				if(S_ISDIR(statbuf.st_mode))
					rmdirs(node->name);
				else
					remove(node->name);
			}

			node = node->next;
		}

		exit(1);
	}
}

void do_mOption(struct dirent **src_list, int src_count, struct dirent **dst_list, int dst_count)
{
	int i, j;
	char c, tempfile[BUFLEN+5];
	FILE *fp, *tmp_fp;
	struct stat statbuf;
	Target *new;

	// 삭제해야할 파일 탐색
	for(i = 0; i < dst_count; i++){
		if(!strcmp(dst_list[i]->d_name, ".") || !strcmp(dst_list[i]->d_name, ".."))
			continue;

		for(j = 0; j < src_count; j++){
			if(!strcmp(src_list[j]->d_name, ".") || !strcmp(src_list[j]->d_name, ".."))
				continue;

			if(!strcmp(src_list[j]->d_name, dst_list[i]->d_name))
				break;
		}

		if(j == src_count){ // dst 디렉토리에 있지만 src 디렉토리에 없는 경우 삭제 
			new = malloc(sizeof(Target));
			strcpy(new->name, dst_list[i]->d_name);
			new->size = -1;
			new->next = NULL;

			if(head == NULL){
				head = new;
				tail = new;
			}
			else{
				tail->next = new;
				tail = tail->next;
			}

			// .tmp 파일 생성
			sprintf(tempfile, "%s.tmp", dst_list[i]->d_name);
			stat(dst_list[i]->d_name, &statbuf);

			if(!S_ISDIR(statbuf.st_mode)){ // 디렉토리가 아닌 경우
				fp = fopen(dst_list[i]->d_name, "r");
				tmp_fp = fopen(tempfile, "w+");

				while(!feof(fp)){
					c = fgetc(fp);
					if(feof(fp))
						break;
					fputc(c, tmp_fp);
				}

				fclose(fp);
				fclose(tmp_fp);
				remove(dst_list[i]->d_name); // 삭제
			}
			else{ // 디렉토리인 경우
				rename(dst_list[i]->d_name, tempfile);
				rmdirs(dst_list[i]->d_name);
			}
		}
	}
}

void synchronize_by_tar(char *src_path, char *dst_path)
{
	int i, j;
	int src_count, dst_count;
	char c, tar_command[BUFLEN];
	char dst_bundle_path[BUFLEN+11];
	char *tmp, str[TIMESTRLEN];
	time_t now = time(NULL);
	FILE *fp, *tmp_fp;
	struct stat statbuf, tstatbuf;
	struct dirent **src_namelist, **dst_namelist;

	strncpy(str, ctime(&now), TIMESTRLEN);
	str[TIMESTRLEN-2] = '\0';

	fp = fopen("ssu_rsync_log", "a");
	tmp_fp = fopen("tempfile", "w+");

	if(fp == NULL && tmp_fp == NULL){
		fprintf(stderr, "fopen error\n");
		exit(1);
	}

	fprintf(fp, "[%s] %s\n", str, command);
	fflush(fp);

	stat(src_path, &statbuf);

	if(S_ISDIR(statbuf.st_mode)){ // 동기화될 파일이 디렉토리인 경우 tar명령어 세팅
		src_count = scandir(src_path, &src_namelist, NULL, alphasort);
		dst_count = scandir(dst_path, &dst_namelist, NULL, alphasort);
		strcpy(tar_command, "tar -cvf bundle.tar ");

		for(i = 0; i < src_count; i++){
			if(!strcmp(src_namelist[i]->d_name, ".") || !strcmp(src_namelist[i]->d_name, ".."))
				continue;
			chdir(src_path);
			stat(src_namelist[i]->d_name, &statbuf);

			for(j = 0; j < dst_count; j++){
				if(!strcmp(dst_namelist[j]->d_name, ".") || !strcmp(dst_namelist[j]->d_name, ".."))
					continue;

				if(!strcmp(src_namelist[i]->d_name, dst_namelist[j]->d_name)){
					chdir(dst_path);
					stat(dst_namelist[j]->d_name, &tstatbuf);

					if(statbuf.st_size == tstatbuf.st_size && statbuf.st_mtime == tstatbuf.st_mtime)
						break;
				}
			}
			if(j == dst_count){
				strcat(tar_command, src_namelist[i]->d_name);
				strcat(tar_command, " ");
			}
		}
	}
	else{ // 동기화될 파일이 디렉토리가 아닌 경우 tar명령어 세팅
		tmp = src_path;
		while((tmp = strstr(tmp, "/")) != NULL){
			tmp++;
			strcpy(src_path, tmp);
		}
		sprintf(tar_command, "tar -cvf bundle.tar %s", src_path);
	}

	if(strlen(tar_command) == strlen("tar -cvf bundle.tar ")) // 동기화할 파일이 없는 경우
		return ;

	// tar 실행 및 디렉토리 작업
	sprintf(dst_bundle_path, "%s/bundle.tar", dst_path);
	chdir(src_path);
	redirection(tar_command, fileno(tmp_fp), 1);
	stat("bundle.tar", &statbuf);
	rename("bundle.tar", dst_bundle_path);
	chdir(dst_path);

	// -t 옵션 전용 log 기록
	fprintf(fp, "	totalSize %ldbytes\n", statbuf.st_size);
	fflush(tmp_fp);
	fseek(tmp_fp, 0, SEEK_SET);
	fputs("	", fp);

	while(!feof(tmp_fp)){
		c = fgetc(tmp_fp);
		if(feof(tmp_fp))
			break;
		fputc(c, fp);
		if(c == '\n'){
			c = getc(tmp_fp);
			if(feof(tmp_fp))
				break;
			ungetc(c, tmp_fp);
			fputs("	", fp);
		}
	}

	// bundle파일 압축해제 및 삭제
	strcpy(tar_command, "tar -xf bundle.tar");
	system(tar_command);
	remove("bundle.tar");

	chdir(saved_path);
	fclose(fp);
	fclose(tmp_fp);
	remove("tempfile");
}

void rmdirs(const char *path)
{
	char tmp[BUFLEN];
	DIR *dp;
	struct stat statbuf;
	struct dirent *dirp;

	if((dp = opendir(path)) == NULL) 
		return;

	while((dirp = readdir(dp)) != NULL)
	{
		if(!strcmp(dirp->d_name, ".") || !strcmp(dirp->d_name, ".."))
			continue;

		sprintf(tmp, "%s/%s", path, dirp->d_name);

		if(lstat(tmp, &statbuf) == -1)
			continue;

		if(S_ISDIR(statbuf.st_mode)) // 파일이 디렉토리면 재귀 호출
			rmdirs(tmp);
		else // 아닌 경우 unlink
			unlink(tmp);
	}

	closedir(dp);
	rmdir(path);
}

void redirection(char *command, int new, int old)
{
	int saved;

	saved = dup(old);
	dup2(new, old);

	system(command);

	dup2(saved, old);
	close(saved);
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
