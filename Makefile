TARGET = tsdb

OBJS = main.o tsdb.o 
OBJS += http.o http_tsdb.o http_csv.o cJSON/cJSON.o

MICROHTTPD=libmicrohttpd-0.9.24
MICROHTTPD-INCDIR=$(MICROHTTPD)/image/include
MICROHTTPD-LIBS=-lpthread -lrt

# use -pg for profiling, -g for debug only
DEBUG_FLAGS = -g
CC = gcc
CFLAGS = -Wall -DVERBOSITY=9 $(DEBUG_FLAGS)
CFLAGS += -I$(MICROHTTPD-INCDIR)
LDFLAGS = $(DEBUG_FLAGS)
LIBS = -lm $(MICROHTTPD)/image/lib/libmicrohttpd.a $(MICROHTTPD-LIBS)

all:	$(TARGET)

$(TARGET):	$(MICROHTTPD)/image/lib/libmicrohttpd.a $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS) 

%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@

# For shipped version of microhttpd
$(MICROHTTPD):	$(MICROHTTPD).tar.gz
	tar xzf $(MICROHTTPD).tar.gz
$(MICROHTTPD)/image/lib/libmicrohttpd.a:	$(MICROHTTPD)
	cd $(MICROHTTPD); ./configure --prefix=/ --disable-https
	make -C $(MICROHTTPD)
	DESTDIR=$(PWD)/$(MICROHTTPD)/image make -C $(MICROHTTPD) install

clean:
	rm -rf $(TARGET) $(OBJS) $(MICROHTTPD)

.PHONY:	clean


