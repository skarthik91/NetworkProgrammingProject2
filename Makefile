CC = gcc

LIBS =  -lsocket\
	/home/courses/cse533/Stevens/unpv13e_solaris2.10/libunp.a

FLAGS =  -g -O2
CFLAGS = ${FLAGS} -I/home/courses/cse533/Stevens/unpv13e_solaris2.10/lib

all: udprtt.o get_ifi_info_plus.o client.o server.o
	${CC} -o client client.o get_ifi_info_plus.o udprtt.o -lm ${LIBS}
        ${CC} -o server server.o get_ifi_info_plus.o  udprtt.o -lm ${LIBS}

get_ifi_info_plus.o: get_ifi_info_plus.c
	${CC} ${CFLAGS} -c get_ifi_info_plus.c


server.o: server.c 
	${CC} ${CFLAGS} -c server.c 

client.o: client.c 
	${CC} ${CFLAGS} -c client.c	

udprtt.o: udprtt.c 
	${CC} ${CFLAGS} -c udprtt.c 	

clean:
	rm server client client.o get_ifi_info_plus.o udprtt.o
