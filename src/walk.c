#include "entry.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Count direct child files in a directory */
static int count_dir_files(int parent_fd, const char *name)
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
        count++;
    }
    closedir(d); /* also closes fd */
    return count;
}

int walk_directory(const char *path, int depth, int show_all,
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
            entry_list_push(list, &e);
            continue;
        }

        e.name = strdup(de->d_name);
        if (!e.name)
            continue;

        e.mtime = st.st_mtime;

        if (S_ISDIR(st.st_mode)) {
            e.type = ENTRY_DIR;
            e.dir_files = count_dir_files(fd, de->d_name);
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

        /* git status lookup */
        if (e.type == ENTRY_DIR)
            e.git = git_dir_status(de->d_name);
        else
            e.git = git_file_status(de->d_name);

        entry_list_push(list, &e);
    }

    closedir(d); /* also closes fd */
    return 0;
}
