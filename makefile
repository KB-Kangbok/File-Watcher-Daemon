all: fwd bu restore

fwd: fwd.c
	gcc fwd.c csapp.c -pthread -lrt -o fwd

bu: bu.c
	gcc bu.c csapp.c -pthread -o bu

restore: restore.c
	gcc restore.c csapp.c -pthread -lrt -o restore

install:
	cp fwd.conf /etc/fwd.conf
	cp bu.conf /etc/bu.conf
	rm -rf /var/bu
	mkdir /var/bu
	cp fwd.service /lib/systemd/system/fwd.service
	cp bu.service /lib/systemd/system/bu.service
	cp fwd /usr/sbin/fwd
	cp bu /usr/sbin/bu
