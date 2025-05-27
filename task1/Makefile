CC = gcc
CFLAGS = -O2 -Wall
LDLIBS = -lpthread

all: multiclient stockclient stockserver

multiclient: multiclient.c csapp.c csapp.h
	$(CC) $(CFLAGS) -o multiclient multiclient.c csapp.c $(LDLIBS)
stockclient: stockclient.c csapp.c csapp.h
	$(CC) $(CFLAGS) -o stockclient stockclient.c csapp.c $(LDLIBS)
stockserver: stockserver.c echo.c csapp.c csapp.h
	$(CC) $(CFLAGS) -o stockserver stockserver.c echo.c csapp.c $(LDLIBS)

clean:
	rm -rf *~ multiclient stockclient stockserver *.o
