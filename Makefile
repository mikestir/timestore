TARGET = tsdb

OBJS = main.o tsdb.o 
OBJS += http.o http_tsdb.o cJSON/cJSON.o

CC = gcc
CFLAGS = -Wall -DVERBOSITY=9 -g
LDFLAGS =-g
LIBS = -lmicrohttpd -lm

all:	$(TARGET)

$(TARGET):	$(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS) 

%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -rf $(TARGET) $(OBJS)

.PHONY:	clean


