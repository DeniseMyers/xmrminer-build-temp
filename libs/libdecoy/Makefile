FEATURE_TLS ?= 1

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
AR      ?= ar
ARFLAGS ?= rcs

CFLAGS += -DFEATURE_TLS=$(FEATURE_TLS)

SRCS = src/decoy.c
OBJS = $(SRCS:.c=.o)
LIB  = libdecoy.a

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

src/%.o: src/%.c include/libdecoy.h
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<

clean:
	rm -f $(OBJS) $(LIB)
