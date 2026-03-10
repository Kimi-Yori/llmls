CC ?= cc
CFLAGS = -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
LDFLAGS = -s

# Use musl-gcc for static build if available
MUSL_CC := $(shell command -v musl-gcc 2>/dev/null)
ifdef MUSL_CC
  STATIC_CC = musl-gcc
  STATIC_LDFLAGS = -s -static
else
  STATIC_CC = $(CC)
  STATIC_LDFLAGS = -s -static
endif

SRC = src/main.c src/walk.c src/ignore.c src/git.c src/sort.c \
      src/render_text.c src/render_json.c src/escape.c src/util.c

.PHONY: all clean static install

all: llmls

llmls: $(SRC) src/entry.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)

static: $(SRC) src/entry.h
	$(STATIC_CC) $(CFLAGS) $(STATIC_LDFLAGS) -o llmls $(SRC)

clean:
	rm -f llmls

install: llmls
	install -m 755 llmls /usr/local/bin/
