CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -pedantic -std=c11
SRCS = main.c bt.c ui.c sysinfo.c printer.c network.c speedtest.c health.c
OBJS = $(SRCS:.c=.o)
BIN  = blue

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c config.h device.h bt.h ui.h sysinfo.h printer.h network.h speedtest.h health.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean
