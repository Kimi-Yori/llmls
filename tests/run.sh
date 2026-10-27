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

# --- single file target ---
SINGLE="$LONGDIR/main.c"

out=$("$BIN" "$SINGLE")
assert_contains "$out" "^file $SINGLE size:[^ ]+ age:[^ ]+$"
assert_not_contains "$out" '^dir '

out=$("$BIN" -l "$SINGLE")
assert_contains "$out" "^file $SINGLE size:[^ ]+ age:[^ ]+ mode:644 owner:[^ ]+ group:[^ ]+$"

out=$("$BIN" -d "$SINGLE")
assert_contains "$out" "^f \.\. $SINGLE [^ ]+ [^ ]+$"
assert_not_contains "$out" '^\. d='

# JSON keeps the container shape so consumers do not need a special case
out=$("$BIN" -j "$SINGLE")
assert_contains "$out" '"files":1'
assert_contains "$out" '"dirs":0'
assert_contains "$out" '"type":"file"'

# a symlink target is listed as the link itself, not as what it points to
out=$("$BIN" -l "$LONGDIR/link")
assert_contains "$out" "^symlink $LONGDIR/link size:[^ ]+ age:[^ ]+ target:main\.c mode:[0-7]{3,4}"

# a broken symlink exists even though stat() fails on it
ln -s /nonexistent/target "$TMP/broken"
out=$("$BIN" "$TMP/broken")
assert_contains "$out" "^symlink $TMP/broken size:[^ ]+ age:[^ ]+$"

# a missing path is still an error
if "$BIN" "$TMP/no_such_file" >/dev/null 2>&1; then
    fail "missing path should exit non-zero"
fi

# a trailing slash on a file is rejected as not-a-directory
if out=$("$BIN" "$SINGLE/" 2>&1); then
    fail "trailing slash on a file should exit non-zero"
fi
assert_contains "$out" 'not a directory'

# --- git status for a single file ---
# The symlink cases guard against resolving the target instead of the link.
REPO="$TMP/repo"
mkdir "$REPO"
(cd "$REPO" && git init -q >/dev/null 2>&1 &&
 git config user.email t@example.com && git config user.name tester) || REPO=""

if [ -n "$REPO" ]; then
    printf 'x\n' > "$REPO/tracked.txt"
    (cd "$REPO" && git add tracked.txt)
    ln -s tracked.txt "$REPO/link"
    ln -s /nonexistent/target "$REPO/broken"
    mkdir "$REPO/sub"
    printf 'y\n' > "$REPO/sub/nested.txt"
    # tracked: git reports an untracked directory as "sub/", never its members
    (cd "$REPO" && git add sub/nested.txt)

    out=$("$BIN" "$REPO/tracked.txt")
    assert_contains "$out" 'git:added'

    # a nested path exercises the prefix built from the parent directory
    out=$("$BIN" "$REPO/sub/nested.txt")
    assert_contains "$out" 'git:added'

    # the link's own status, not the target's
    out=$("$BIN" "$REPO/link")
    assert_contains "$out" 'git:untracked'
    assert_not_contains "$out" 'git:added'

    # a broken link still has a repo-relative path
    out=$("$BIN" "$REPO/broken")
    assert_contains "$out" 'git:untracked'

    # relative path from inside the repo
    out=$(cd "$REPO" && "$BIN" tracked.txt)
    assert_contains "$out" 'git:added'
else
    # Announce the gap: a silent skip is indistinguishable from a pass.
    printf 'SKIP: git fixture unavailable, git status cases not verified\n' >&2
fi

printf 'ok\n'
