UNAME_S := $(shell uname -s)

CC ?= cc
VERSION = 0.2.0
CFLAGS = -O2 -Wall -Wextra -std=c11
CPPFLAGS += -DLLMLS_VERSION=\"$(VERSION)\"

SRC = src/main.c src/walk.c src/ignore.c src/git.c src/sort.c \
      src/render_text.c src/render_json.c src/escape.c src/util.c

# Platform-specific flags
ifeq ($(UNAME_S),Darwin)
  LDFLAGS = -Wl,-dead_strip
  CFLAGS += -flto
else
  # Linux / other POSIX
  CFLAGS += -D_GNU_SOURCE
  LDFLAGS = -s
  MUSL_CC := $(shell command -v musl-gcc 2>/dev/null)
  ifdef MUSL_CC
    STATIC_CC = musl-gcc
  else
    STATIC_CC = $(CC)
  endif
  STATIC_LDFLAGS = -s -static
endif

.PHONY: all clean static install universal test

all: llmls

llmls: $(SRC) src/entry.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)
ifeq ($(UNAME_S),Darwin)
	strip $@
endif

static: $(SRC) src/entry.h
ifeq ($(UNAME_S),Linux)
	$(STATIC_CC) $(CPPFLAGS) $(CFLAGS) $(STATIC_LDFLAGS) -o llmls $(SRC)
else
	@echo "Error: Static builds require Linux with musl-gcc"
	@exit 1
endif

universal: $(SRC) src/entry.h
ifeq ($(UNAME_S),Darwin)
	$(CC) $(CPPFLAGS) $(filter-out -flto,$(CFLAGS)) $(LDFLAGS) -arch arm64 -arch x86_64 -o llmls $(SRC)
	strip llmls
else
	@echo "Error: Universal builds are macOS-only"
	@exit 1
endif

clean:
	rm -f llmls

install: llmls
	install -m 755 llmls /usr/local/bin/

test: llmls
	sh tests/run.sh
