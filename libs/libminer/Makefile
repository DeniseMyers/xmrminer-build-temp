FEATURE_TLS ?= 1
FEATURE_FULL_MEM ?= 1
FEATURE_JIT ?= 1
FEATURE_HARD_AES ?= 1
FEATURE_LARGE_PAGES ?= 0

CC      ?= gcc
CFLAGS  ?= -O1 -Wall -Wextra
AR      ?= ar
ARFLAGS ?= rcs

CFLAGS += -DFEATURE_TLS=$(FEATURE_TLS) -DFEATURE_FULL_MEM=$(FEATURE_FULL_MEM) -DFEATURE_JIT=$(FEATURE_JIT) -DFEATURE_HARD_AES=$(FEATURE_HARD_AES) -DFEATURE_LARGE_PAGES=$(FEATURE_LARGE_PAGES)

LIBS_DIR = ..
RANDOMX_DIR = $(LIBS_DIR)/../lib/randomx
RANDOMX_INC = $(RANDOMX_DIR)/src

SRCS = src/miner.c
OBJS = $(SRCS:.c=.o)
LIB  = libminer.a

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

src/%.o: src/%.c include/libminer.h
	$(CC) $(CFLAGS) -Iinclude -I$(RANDOMX_INC) -I$(LIBS_DIR)/libstratum/include -I$(LIBS_DIR)/libutils/include -I$(LIBS_DIR)/libws/include -I$(LIBS_DIR)/libjson/include -I$(LIBS_DIR)/libcrypto/include -c -o $@ $<

clean:
	rm -f $(OBJS) $(LIB)
