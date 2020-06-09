ssu_crontab : ssu_crontab.o ssu_crond.o ssu_rsync.o
	gcc ssu_crontab.o -o ssu_crontab -lpthread
	gcc ssu_crond.o -o ssu_crond -lpthread
	gcc ssu_rsync.o -o ssu_rsync

ssu_crontab.o : ssu_crontab.c ssu_crontab.h
	gcc -c -W -Wall -Wextra ssu_crontab.c

ssu_crond.o : ssu_crond.c ssu_crontab.h
	gcc -c -W -Wall -Wextra ssu_crond.c

ssu_rsync.o : ssu_rsync.c ssu_rsync.h
	gcc -c -W -Wall -Wextra ssu_rsync.c 

clean :
	rm *.o
	rm ssu_crontab
	rm ssu_crond
	rm ssu_rsync

