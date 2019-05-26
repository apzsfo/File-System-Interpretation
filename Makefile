# NAME: Ryan Nemiroff, Andy Zeff
# EMAIL: ryguyn@gmail.com
# ID: 304903942

lab3a:
	gcc lab3a.c -o lab3a -Wall -Wextra

dist:
	tar -zcf lab3a-304903942.tar.gz Makefile README lab3a.c ext2_fs.h

clean:
	rm -f lab3a-304903942.tar.gz lab3a

