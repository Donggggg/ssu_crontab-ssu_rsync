#define BUFLEN 1024
#define MAXNUM 100
#define MAX_CYCLE 5
#define TRUE 1
#define FALSE 0
#define SECOND_TO_MICRO 1000000

int check_exe_cycle(char *cycle, int level);
int check_wrong_character(char* str);
void *execute_command(void *arg);
void make_time_table(char *str, int **time_table);
void set_time_table(char *cycle, int level, int *table[5]);
void ssu_runtime(struct timeval *begin_t, struct timeval *end_t);
