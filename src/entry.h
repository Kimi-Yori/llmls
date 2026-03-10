#ifndef LLMLS_ENTRY_H
#define LLMLS_ENTRY_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>

#define LLMLS_VERSION "0.1.0"

/* Entry types */
enum entry_type {
    ENTRY_FILE,
    ENTRY_DIR,
    ENTRY_SYMLINK,
    ENTRY_UNKNOWN,
};

/* Git status */
enum git_status {
    GIT_NONE,       /* not in a git repo or clean */
    GIT_MODIFIED,
    GIT_ADDED,
    GIT_UNTRACKED,
    GIT_RENAMED,
    GIT_DELETED,
    GIT_CONFLICTED,
};

/* Sort priority (lower = higher priority) */
enum sort_priority {
    SORT_GIT_CHANGED,
    SORT_IMPORTANT,
    SORT_FILE,
    SORT_DIR,
};

/* Single entry */
struct entry {
    char            *name;          /* filename (allocated) */
    enum entry_type  type;
    off_t            size;          /* file size in bytes */
    time_t           mtime;         /* modification time */
    enum git_status  git;
    enum sort_priority priority;
    int              dir_files;     /* direct child file count (dirs only) */
    int              error;         /* errno if stat failed */
    char            *link_target;   /* symlink target (allocated, nullable) */
};

/* Entry list */
struct entry_list {
    struct entry *entries;
    size_t        count;
    size_t        capacity;
};

/* Directory summary (header line) */
struct dir_summary {
    const char *path;
    int         total_files;
    int         total_dirs;
    off_t       total_size;
};

/* Output mode */
enum output_mode {
    OUTPUT_DEFAULT,
    OUTPUT_DENSE,
    OUTPUT_JSON,
};

/* Options */
struct options {
    const char     *path;       /* target path */
    int             depth;      /* max depth (default 1) */
    int             show_all;   /* --all: show hidden/ignored */
    enum output_mode mode;
    int             show_help;
    int             show_version;
};

/* --- Function declarations --- */

/* walk.c */
int walk_directory(const char *path, int depth, int show_all,
                   const char *git_prefix,
                   struct entry_list *list, struct dir_summary *summary);

/* ignore.c */
int should_ignore(const char *name, int show_all);

/* git.c */
char *git_find_root(const char *path);
int  git_load_status(const char *repo_root);
enum git_status git_file_status(const char *relpath);
enum git_status git_dir_status(const char *dir_name);
void git_cleanup(void);

/* sort.c */
void sort_entries(struct entry_list *list);

/* render_text.c */
void render_default(const struct dir_summary *summary,
                    const struct entry_list *list, time_t now, int indent);
void render_dense(const struct dir_summary *summary,
                  const struct entry_list *list, time_t now, int indent);

/* render_json.c */
void render_json(const struct dir_summary *summary,
                 const struct entry_list *list, time_t now);

/* escape.c */
char *escape_filename(const char *name);
void  write_escaped(int fd, const char *s);

/* util.c */
void format_size(off_t bytes, char *buf, size_t bufsz);
void format_age(time_t mtime, time_t now, char *buf, size_t bufsz);
const char *git_status_str(enum git_status s);
const char *git_status_porcelain(enum git_status s);
const char *errno_str(int err);

/* entry_list helpers */
int  entry_list_init(struct entry_list *list);
int  entry_list_push(struct entry_list *list, struct entry *e);
void entry_list_free(struct entry_list *list);

#endif /* LLMLS_ENTRY_H */
