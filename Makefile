CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -O2
LDFLAGS =

SRCS = main.c asm.c lines.c expr.c macro.c symtab.c segment.c cond.c \
       i8080gen.c z80gen.c relout.c rexout.c listing.c token.c
OBJS = $(SRCS:.c=.o)
TARGET = aim80

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) *.rel *.rex *.prn

.PHONY: all clean
