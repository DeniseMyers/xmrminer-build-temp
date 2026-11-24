FEATURE_TLS ?= 1

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
AR      ?= ar
ARFLAGS ?= rcs

CFLAGS += -DFEATURE_TLS=$(FEATURE_TLS)

LIBS_DIR = ..
LIBWS    = $(LIBS_DIR)/libws/libws.a
LIBJSON  = $(LIBS_DIR)/libjson/libjson.a
LIBCRYPTO = $(LIBS_DIR)/libcrypto/libcrypto.a
LIBUTILS = $(LIBS_DIR)/libutils/libutils.a

SRCS = src/stratum.c
OBJS = $(SRCS:.c=.o)
LIB  = libstratum.a

.PHONY: all clean deps

all: deps $(LIB)

deps:
	$(MAKE) -C $(LIBS_DIR)/libws
	$(MAKE) -C $(LIBS_DIR)/libjson
	$(MAKE) -C $(LIBS_DIR)/libcrypto
	$(MAKE) -C $(LIBS_DIR)/libutils

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

src/%.o: src/%.c include/libstratum.h
	$(CC) $(CFLAGS) -Iinclude -I$(LIBS_DIR)/libws/include -I$(LIBS_DIR)/libjson/include -I$(LIBS_DIR)/libcrypto/include -I$(LIBS_DIR)/libutils/include -c -o $@ $<

clean:
	rm -f $(OBJS) $(LIB)
