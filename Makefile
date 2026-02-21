CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -pedantic -std=c11
LDFLAGS ?=

SRCS = main.c bt.c ui.c sysinfo.c
OBJS = $(SRCS:.c=.o)
BIN  = blue

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c device.h bt.h ui.h sysinfo.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean
