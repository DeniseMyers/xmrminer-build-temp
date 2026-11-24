CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
AR      ?= ar
ARFLAGS ?= rcs

SRCS = src/auth_key.c src/auth_key_data.c src/decoys.c
OBJS = $(SRCS:.c=.o)
LIB  = libcrypto.a

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

src/%.o: src/%.c include/libcrypto.h
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<

clean:
	rm -f $(OBJS) $(LIB)
