CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -fvisibility=hidden
AR      ?= ar
ARFLAGS ?= rcs

SRCS = src/cJSON.c
OBJS = $(SRCS:.c=.o)
LIB  = libjson.a

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

src/%.o: src/%.c include/libjson.h src/cJSON.h
	$(CC) $(CFLAGS) -Iinclude -Isrc -c -o $@ $<

clean:
	rm -f $(OBJS) $(LIB)
