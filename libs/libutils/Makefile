CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
AR      ?= ar
ARFLAGS ?= rcs

SRCS = src/hex.c src/cpu.c
OBJS = $(SRCS:.c=.o)
LIB  = libutils.a

.PHONY: all clean test

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

src/%.o: src/%.c include/libutils.h
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<

test: $(LIB)
	$(CC) $(CFLAGS) -Iinclude -o test_utils test/test_utils.c -L. -lutils
	./test_utils

clean:
	rm -f $(OBJS) $(LIB) test_utils
