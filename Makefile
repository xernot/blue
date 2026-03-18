CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -pedantic -std=c11 -Iinclude

SRCDIR = src
SRCS   = $(SRCDIR)/main.c $(SRCDIR)/bt.c $(SRCDIR)/ui.c $(SRCDIR)/sysinfo.c \
         $(SRCDIR)/printer.c $(SRCDIR)/network.c $(SRCDIR)/speedtest.c $(SRCDIR)/health.c
OBJS   = $(SRCS:.c=.o)
BIN    = blue

HDRS   = $(wildcard include/*.h)

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean
