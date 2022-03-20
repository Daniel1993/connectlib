DEBUG ?= 0
SRCS := \
	src/connectlib.c \
	src/util_gnu.c \
#

OBJS := $(SRCS:.c=.o)

INCS := \
	-I ./include \
	-I ./src \
#

AR := ar rcs
CC := gcc -c
LD := gcc

STDC11 := -std=c11
GNUC11 := -std=gnu11
CFLAGS := $(INCS)

ifeq ($(DEBUG),1)
CFLAGS += -g -O0
else
CFLAGS += -O3
endif

libconn.a: $(OBJS)
	$(AR) $@ $^

test: libconn.a tests/main.c
	$(LD) -g -I ./include tests/main.c -o $@ -L . -l conn -pthread

ping: libconn.a apps/ping.c
	$(LD) -g -I ./include apps/ping.c -o $@ -L . -l conn -pthread

pong: libconn.a apps/pong.c
	$(LD) -g -I ./include apps/pong.c -o $@ -L . -l conn -pthread

%.o : %.c
	$(CC) $(CFLAGS) $(STDC11) -o $@ $^

%_gnu.o : %_gnu.c
	$(CC) $(CFLAGS) $(GNUC11) -o $@ $^

clean:
	rm -f $(OBJS) test ping pong libconn.a

