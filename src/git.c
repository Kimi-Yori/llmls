#include "entry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

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

int git_is_repo(const char *path)
{
    char gitdir[4096];
    snprintf(gitdir, sizeof(gitdir), "%s/.git", path);
    return access(gitdir, F_OK) == 0;
}

/*
 * Run: git -C <repo_root> status --porcelain=v1 -z
 * Parse NUL-delimited output.
 * Uses pipe+fork+execvp (no shell injection).
 */
int git_load_status(const char *repo_root)
{
    if (git_loaded)
        return 0;

    int pipefd[2];
    if (pipe(pipefd) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        /* child */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        /* redirect stderr to /dev/null */
        int devnull = open("/dev/null", 0);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execlp("git", "git", "-C", repo_root,
               "status", "--porcelain=v1", "-z", (char *)NULL);
        _exit(127);
    }

    /* parent */
    close(pipefd[1]);

    /* read all output */
    char *buf = NULL;
    size_t bufsz = 0, bufcap = 0;
    char tmp[4096];
    ssize_t n;
    while ((n = read(pipefd[0], tmp, sizeof(tmp))) > 0) {
        if (bufsz + (size_t)n > bufcap) {
            bufcap = (bufsz + (size_t)n) * 2;
            char *nb = realloc(buf, bufcap);
            if (!nb) { free(buf); close(pipefd[0]); return -1; }
            buf = nb;
        }
        memcpy(buf + bufsz, tmp, (size_t)n);
        bufsz += (size_t)n;
    }
    close(pipefd[0]);

    int wstatus;
    waitpid(pid, &wstatus, 0);

    if (!buf || !WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
        free(buf);
        return -1;
    }

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
