TARGET = tsdb

OBJS = main.o tsdb.o 
OBJS += http.o http_tsdb.o http_csv.o cJSON/cJSON.o

# use -pg for profiling, -g for debug only
DEBUG_FLAGS = -g
CC = gcc
CFLAGS = -Wall -DVERBOSITY=9 $(DEBUG_FLAGS)
LDFLAGS = $(DEBUG_FLAGS)
LIBS = -lmicrohttpd -lm

all:	$(TARGET)

$(TARGET):	$(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS) 

%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -rf $(TARGET) $(OBJS)

.PHONY:	clean


