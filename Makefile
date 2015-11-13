CC = gcc -Zomf -Zmt -O
CFLAGS = -funsigned-char

mmv.exe: mmv.c
	$(CC) $(CFLAGS) -DOS2 -o $@ mmv.c mmv.def -los2
