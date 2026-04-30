#!/bin/sh
set -eu

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

assert_contains() {
    text=$1
    pattern=$2
    printf '%s\n' "$text" | grep -Eq "$pattern" || fail "missing pattern: $pattern"
}

assert_not_contains() {
    text=$1
    pattern=$2
    if printf '%s\n' "$text" | grep -Eq "$pattern"; then
        fail "unexpected pattern: $pattern"
    fi
}

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN="$ROOT/llmls"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

LONGDIR="$TMP/long"
mkdir "$LONGDIR"
printf 'hi\n' > "$LONGDIR/main.c"
printf 'secret\n' > "$LONGDIR/.env"
mkdir "$LONGDIR/src"
mkdir "$LONGDIR/sticky"
ln -s main.c "$LONGDIR/link"
chmod 0644 "$LONGDIR/main.c"
chmod 0755 "$LONGDIR/src"
chmod 1755 "$LONGDIR/sticky"

out=$("$BIN" "$LONGDIR")
assert_not_contains "$out" ' mode:'
assert_not_contains "$out" ' owner:'
assert_not_contains "$out" ' group:'
assert_not_contains "$out" ' target:'
assert_not_contains "$out" '\.env'

out=$("$BIN" -l "$LONGDIR")
assert_contains "$out" 'file main\.c size:[^ ]+ age:[^ ]+ mode:644 owner:[^ ]+ group:[^ ]+'
assert_contains "$out" 'dir src files:[0-9]+ mode:755 owner:[^ ]+ group:[^ ]+'
assert_contains "$out" 'dir sticky files:[0-9]+ mode:1755 owner:[^ ]+ group:[^ ]+'
assert_contains "$out" 'symlink link size:[^ ]+ age:[^ ]+ target:main\.c mode:[0-7]{3,4} owner:[^ ]+ group:[^ ]+'

out=$("$BIN" -la "$LONGDIR")
assert_contains "$out" 'file \.env size:[^ ]+ age:[^ ]+ mode:[0-7]{3,4} owner:[^ ]+ group:[^ ]+'

out=$("$BIN" -dl "$LONGDIR")
assert_contains "$out" '^f \.\. main\.c [^ ]+ [^ ]+ 644 [^ ]+:[^ ]+$'
assert_contains "$out" '^d \.\. src f=[0-9]+ 755 [^ ]+:[^ ]+$'
assert_contains "$out" '^d \.\. sticky f=[0-9]+ 1755 [^ ]+:[^ ]+$'
assert_contains "$out" '^l \.\. link [^ ]+ [^ ]+ ->main\.c [0-7]{3,4} [^ ]+:[^ ]+$'

out=$("$BIN" --json -l "$LONGDIR")
assert_contains "$out" '"mode":"644"'
assert_contains "$out" '"owner":"[^"]+"'
assert_contains "$out" '"group":"[^"]+"'
assert_contains "$out" '"target":"main.c"'

SORTDIR="$TMP/sort"
mkdir "$SORTDIR"
printf 'old\n' > "$SORTDIR/old.txt"
printf 'mid\n' > "$SORTDIR/mid.txt"
printf 'new\n' > "$SORTDIR/new.txt"
touch -t 202001010101 "$SORTDIR/old.txt"
touch -t 202101010101 "$SORTDIR/mid.txt"
touch -t 202201010101 "$SORTDIR/new.txt"

out=$("$BIN" -t "$SORTDIR")
first=$(printf '%s\n' "$out" | sed -n '2p')
case "$first" in
    *new.txt*) ;;
    *) fail "-t did not sort newest entry first: $first" ;;
esac

out=$("$BIN" -ltd "$SORTDIR")
first=$(printf '%s\n' "$out" | sed -n '2p')
case "$first" in
    *new.txt*) ;;
    *) fail "-ltd did not parse cluster or sort by time: $first" ;;
esac
assert_contains "$out" '^f \.\. new\.txt [^ ]+ [^ ]+ [0-7]{3,4} [^ ]+:[^ ]+$'

printf 'ok\n'
