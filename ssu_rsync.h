#define TRUE 1
#define FALSE 0

#define BUFLEN 1024
#define MAXNUM 100
#define TIMESTRLEN 26
#define SECOND_TO_MICRO 1000000

typedef struct _target{
	char name[BUFLEN];
	size_t size;
	int isCreatDir;
	struct _target *next;
}Target; 

int synchronize(char *src_path, char *dst_path);
void synchronize_by_tar(char *src_path, char *dst_path);
Target *synchronize_target(char *taget, struct dirent **namelist, int count);
void synchronize_recursive(char *src_path, char *dst_path, int level);
void do_rOption(char *src_path, char *dst_path);
void synchronize_file(char *target, char *copyfile);
void write_log();
void do_mOption(struct dirent **src_list, int src_count, struct dirent **dst_list, int dst_count);
void redirection(char *command, int new, int old);
void recover_Targets(int signo);
void rmdirs(const char *path);
void ssu_runtime(struct timeval *begin_t, struct timeval *end_t);
