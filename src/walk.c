#include "entry.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * Count direct child files (not dirs) in a directory.
 * Applies ignore rules for consistency with main listing.
 */
static int count_dir_files(int parent_fd, const char *name, int show_all)
{
    int fd = openat(parent_fd, name, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return -1;

    DIR *d = fdopendir(fd);
    if (!d) {
        close(fd);
        return -1;
    }

    int count = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;
        if (should_ignore(de->d_name, show_all))
            continue;

        /* Only count files (use d_type for speed, fallback to stat) */
        if (de->d_type == DT_REG) {
            count++;
        } else if (de->d_type == DT_LNK) {
            count++;
        } else if (de->d_type == DT_UNKNOWN) {
            /* filesystem doesn't support d_type, use fstatat */
            struct stat st;
            if (fstatat(fd, de->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0) {
                if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))
                    count++;
            }
        }
        /* DT_DIR is intentionally not counted */
    }
    closedir(d); /* also closes fd */
    return count;
}

/* Build git-relative path: prefix/name */
static char *make_git_path(const char *prefix, const char *name)
{
    if (!prefix)
        return NULL;
    if (prefix[0] == '\0')
        return strdup(name);

    size_t plen = strlen(prefix);
    size_t nlen = strlen(name);
    char *buf = malloc(plen + 1 + nlen + 1);
    if (!buf)
        return NULL;
    memcpy(buf, prefix, plen);
    buf[plen] = '/';
    memcpy(buf + plen + 1, name, nlen);
    buf[plen + 1 + nlen] = '\0';
    return buf;
}

const char *errno_str(int err)
{
    switch (err) {
    case EACCES:  return "permission_denied";
    case ENOENT:  return "not_found";
    case ENOTDIR: return "not_a_directory";
    case ELOOP:   return "symlink_loop";
    default:      return "error";
    }
}

int walk_directory(const char *path, int depth, int show_all,
                   const char *git_prefix,
                   struct entry_list *list, struct dir_summary *summary)
{
    (void)depth; /* TODO: recursive walk in v0.2 */
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return -1;

    DIR *d = fdopendir(fd);
    if (!d) {
        close(fd);
        return -1;
    }

    summary->total_files = 0;
    summary->total_dirs = 0;
    summary->total_size = 0;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        /* skip . and .. */
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;

        /* check ignore rules */
        if (should_ignore(de->d_name, show_all))
            continue;

        struct stat st;
        struct entry e = {0};

        if (fstatat(fd, de->d_name, &st, AT_SYMLINK_NOFOLLOW) < 0) {
            /* stat failed — record with error */
            e.name = strdup(de->d_name);
            if (!e.name)
                continue;
            e.type = ENTRY_UNKNOWN;
            e.error = errno;
            if (entry_list_push(list, &e) < 0)
                free(e.name);
            continue;
        }

        e.name = strdup(de->d_name);
        if (!e.name)
            continue;

        e.mtime = st.st_mtime;

        if (S_ISDIR(st.st_mode)) {
            e.type = ENTRY_DIR;
            e.dir_files = count_dir_files(fd, de->d_name, show_all);
            summary->total_dirs++;
        } else if (S_ISLNK(st.st_mode)) {
            e.type = ENTRY_SYMLINK;
            e.size = st.st_size;
            summary->total_files++;
            summary->total_size += st.st_size;

            /* read link target */
            char lbuf[4096];
            ssize_t llen = readlinkat(fd, de->d_name, lbuf, sizeof(lbuf) - 1);
            if (llen > 0) {
                lbuf[llen] = '\0';
                e.link_target = strdup(lbuf);
            }
        } else {
            e.type = ENTRY_FILE;
            e.size = st.st_size;
            summary->total_files++;
            summary->total_size += st.st_size;
        }

        /* git status lookup using repo-relative path */
        if (git_prefix) {
            char *gpath = make_git_path(git_prefix, de->d_name);
            if (gpath) {
                if (e.type == ENTRY_DIR)
                    e.git = git_dir_status(gpath);
                else
                    e.git = git_file_status(gpath);
                free(gpath);
            }
        }

        if (entry_list_push(list, &e) < 0) {
            free(e.name);
            free(e.link_target);
        }
    }

    closedir(d); /* also closes fd */
    return 0;
}
