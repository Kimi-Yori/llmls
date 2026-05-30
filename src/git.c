#include "entry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>

extern char **environ;

/* Hash map entry for git status */
struct git_entry {
    char            *path;
    enum git_status  status;
    struct git_entry *next;
};

#define GIT_HASH_SIZE 256

static struct git_entry *git_table[GIT_HASH_SIZE];
static int git_loaded = 0;

static unsigned hash_str(const char *s)
{
    unsigned h = 5381;
    for (; *s; s++)
        h = h * 33 + (unsigned char)*s;
    return h % GIT_HASH_SIZE;
}

static void git_table_insert(const char *path, enum git_status status)
{
    unsigned h = hash_str(path);
    struct git_entry *e = malloc(sizeof(*e));
    if (!e)
        return;
    e->path = strdup(path);
    if (!e->path) {
        free(e);
        return;
    }
    e->status = status;
    e->next = git_table[h];
    git_table[h] = e;
}

static enum git_status parse_porcelain(char x, char y)
{
    /* Untracked */
    if (x == '?' && y == '?')
        return GIT_UNTRACKED;

    /* Conflict markers */
    if (x == 'U' || y == 'U')
        return GIT_CONFLICTED;
    if (x == 'A' && y == 'A')
        return GIT_CONFLICTED;
    if (x == 'D' && y == 'D')
        return GIT_CONFLICTED;

    /* Index (staged) statuses — check X first */
    if (x == 'A')
        return GIT_ADDED;
    if (x == 'R')
        return GIT_RENAMED;
    if (x == 'D')
        return GIT_DELETED;

    /* Working tree changes — check Y */
    if (y == 'M')
        return GIT_MODIFIED;
    if (y == 'D')
        return GIT_DELETED;

    /* Staged modification */
    if (x == 'M')
        return GIT_MODIFIED;

    return GIT_NONE;
}

/*
 * Spawn a command via posix_spawnp, capture stdout into *out_buf.
 * On success, *out_buf is always a non-NULL, NUL-terminated allocation
 * of length *out_len (caller must free). Empty stdout returns len=0
 * with a 1-byte buffer holding '\0' — distinguishable from spawn/exec
 * failure (returns -1). stderr is redirected to /dev/null.
 */
static ssize_t spawn_command(char *const argv[], char **out_buf, size_t *out_len)
{
    *out_buf = NULL;
    *out_len = 0;

    int pipefd[2];
    if (pipe(pipefd) < 0)
        return -1;

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    int rc = 0;
    rc |= posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    rc |= posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    rc |= posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    rc |= posix_spawn_file_actions_addopen(&actions, STDERR_FILENO,
                                           "/dev/null", O_WRONLY, 0);
    if (rc != 0) {
        posix_spawn_file_actions_destroy(&actions);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    pid_t pid;
    int err = posix_spawnp(&pid, argv[0], &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);

    close(pipefd[1]);

    if (err != 0) {
        close(pipefd[0]);
        return -1;
    }

    /* Read all output */
    char *buf = NULL;
    size_t bufsz = 0, bufcap = 0;
    char tmp[4096];
    ssize_t n;
    while ((n = read(pipefd[0], tmp, sizeof(tmp))) > 0) {
        if (bufsz + (size_t)n > bufcap) {
            bufcap = (bufsz + (size_t)n) * 2;
            if (bufcap < 4096) bufcap = 4096;
            char *nb = realloc(buf, bufcap);
            if (!nb) {
                free(buf);
                close(pipefd[0]);
                waitpid(pid, NULL, 0);
                return -1;
            }
            buf = nb;
        }
        memcpy(buf + bufsz, tmp, (size_t)n);
        bufsz += (size_t)n;
    }
    close(pipefd[0]);

    int wstatus;
    waitpid(pid, &wstatus, 0);

    if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
        free(buf);
        return -1;
    }

    /* Guarantee a non-NULL, NUL-terminated buffer of size bufsz+1.
     * Without this, callers writing buf[bufsz] = '\0' would clobber the
     * heap when read sizes happen to fill bufcap exactly. */
    char *nb = realloc(buf, bufsz + 1);
    if (!nb) {
        free(buf);
        return -1;
    }
    nb[bufsz] = '\0';

    *out_buf = nb;
    *out_len = bufsz;
    return (ssize_t)bufsz;
}

/*
 * Find git repo root by running: git -C <path> rev-parse --show-toplevel
 * Returns allocated string (caller frees) or NULL if not in a repo.
 */
char *git_find_root(const char *path)
{
    char *argv[] = {"git", "-C", (char *)path,
                    "rev-parse", "--show-toplevel", NULL};

    char *buf = NULL;
    size_t len = 0;
    if (spawn_command(argv, &buf, &len) < 0)
        return NULL;

    /* strip trailing newline (spawn_command guarantees buf[len] is already '\0') */
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
        buf[--len] = '\0';

    if (len == 0) {
        free(buf);
        return NULL;
    }

    return buf;
}

/*
 * Run: git -C <repo_root> status --porcelain=v1 -z
 * Parse NUL-delimited output.
 */
int git_load_status(const char *repo_root)
{
    if (git_loaded)
        return 0;

    char *argv[] = {"git", "-C", (char *)repo_root,
                    "status", "--porcelain=v1", "-z", NULL};

    char *buf = NULL;
    size_t bufsz = 0;
    if (spawn_command(argv, &buf, &bufsz) < 0)
        return -1;
    /* Empty output is valid: clean repo with no porcelain entries. */

    /* Parse NUL-delimited entries.
     * Format: XY filename\0  (or XY oldname\0newname\0 for renames)
     */
    size_t pos = 0;
    while (pos < bufsz) {
        if (pos + 3 >= bufsz)
            break;

        char x = buf[pos];
        char y = buf[pos + 1];
        /* buf[pos+2] should be space */
        pos += 3;

        /* find NUL terminator */
        size_t start = pos;
        while (pos < bufsz && buf[pos] != '\0')
            pos++;

        if (pos <= start)
            break;

        char *path = strndup(buf + start, pos - start);
        pos++; /* skip NUL */

        /* For renames, skip the second path */
        if (x == 'R' || x == 'C') {
            while (pos < bufsz && buf[pos] != '\0')
                pos++;
            pos++; /* skip NUL */
        }

        if (path) {
            enum git_status st = parse_porcelain(x, y);
            if (st != GIT_NONE)
                git_table_insert(path, st);
            free(path);
        }
    }

    free(buf);
    git_loaded = 1;
    return 0;
}

enum git_status git_file_status(const char *relpath)
{
    if (!git_loaded)
        return GIT_NONE;

    unsigned h = hash_str(relpath);
    for (struct git_entry *e = git_table[h]; e; e = e->next) {
        if (strcmp(e->path, relpath) == 0)
            return e->status;
    }
    return GIT_NONE;
}

/*
 * Check if any git-changed file exists under dir_prefix/.
 * Returns the "most notable" status found.
 */
enum git_status git_dir_status(const char *dir_name)
{
    if (!git_loaded)
        return GIT_NONE;

    size_t dlen = strlen(dir_name);
    enum git_status best = GIT_NONE;

    for (int i = 0; i < GIT_HASH_SIZE; i++) {
        for (struct git_entry *e = git_table[i]; e; e = e->next) {
            /* check if path starts with "dir_name/" */
            if (strncmp(e->path, dir_name, dlen) == 0 && e->path[dlen] == '/') {
                /* prefer more "urgent" statuses */
                if (best == GIT_NONE || e->status < best)
                    best = e->status;
            }
        }
    }
    return best;
}

void git_cleanup(void)
{
    for (int i = 0; i < GIT_HASH_SIZE; i++) {
        struct git_entry *e = git_table[i];
        while (e) {
            struct git_entry *next = e->next;
            free(e->path);
            free(e);
            e = next;
        }
        git_table[i] = NULL;
    }
    git_loaded = 0;
}
