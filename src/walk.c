#include "entry.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/attr.h>
#include <sys/vnode.h>
#endif

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

/* Add entry with git status lookup. Counters are updated only after a
 * successful push so the summary never overcounts a dropped entry. */
static void add_entry(struct entry *e, int dirfd, int show_all,
                      const char *git_prefix, struct entry_list *list,
                      struct dir_summary *summary)
{
    if (e->type == ENTRY_DIR) {
        e->dir_files = count_dir_files(dirfd, e->name, show_all);
    } else if (e->type == ENTRY_SYMLINK) {
        char lbuf[4096];
        ssize_t llen = readlinkat(dirfd, e->name, lbuf, sizeof(lbuf) - 1);
        if (llen > 0) {
            lbuf[llen] = '\0';
            e->link_target = strdup(lbuf);
        } else {
            fprintf(stderr, "llmls: readlinkat('%s') failed: %s\n",
                    e->name, strerror(errno));
        }
    }

    /* git status lookup using repo-relative path */
    if (git_prefix) {
        char *gpath = make_git_path(git_prefix, e->name);
        if (gpath) {
            if (e->type == ENTRY_DIR)
                e->git = git_dir_status(gpath);
            else
                e->git = git_file_status(gpath);
            free(gpath);
        }
    }

    if (entry_list_push(list, e) < 0) {
        fprintf(stderr, "llmls: out of memory adding entry '%s'\n", e->name);
        free(e->name);
        free(e->link_target);
        return;
    }

    if (e->type == ENTRY_DIR) {
        summary->total_dirs++;
    } else {
        summary->total_files++;
        summary->total_size += e->size;
    }
}

#ifdef __APPLE__
/*
 * macOS fast path: use getattrlistbulk() to batch-fetch metadata for all
 * directory entries in fewer syscalls than individual readdir()+fstatat().
 * Returns 0 on success, -1 on failure (caller falls back to POSIX path).
 */
static int walk_directory_darwin(int dirfd, int show_all,
                                const char *git_prefix,
                                struct entry_list *list,
                                struct dir_summary *summary)
{
    struct attrlist al;
    memset(&al, 0, sizeof(al));
    al.bitmapcount = ATTR_BIT_MAP_COUNT;
    al.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME |
                    ATTR_CMN_OBJTYPE | ATTR_CMN_MODTIME |
                    ATTR_CMN_OWNERID | ATTR_CMN_GRPID |
                    ATTR_CMN_ACCESSMASK | ATTR_CMN_ERROR;
    al.fileattr = ATTR_FILE_DATALENGTH;

    _Alignas(8) char attrBuf[16384]; /* handles ~200 entries per call */
    char * const attrBufEnd = attrBuf + sizeof(attrBuf);
    int saw_data = 0;

    for (;;) {
        int count = getattrlistbulk(dirfd, &al, attrBuf, sizeof(attrBuf), 0);
        if (count < 0) {
            int saved_errno = errno;
            if (saw_data) {
                fprintf(stderr,
                        "llmls: getattrlistbulk failed mid-enumeration: %s "
                        "(%zu entries discarded)\n",
                        strerror(saved_errno), list->count);
            }
            errno = saved_errno;
            return -1;
        }
        if (count == 0)
            break;
        saw_data = 1;

        char *p = attrBuf;
        for (int i = 0; i < count; i++) {
            /* Validate entry length: must hold at least the length prefix +
             * the returned-attribute bitmap, and must fit in the remaining
             * buffer. Guards against malformed kernel responses. */
            if ((size_t)(attrBufEnd - p) < sizeof(uint32_t)) {
                fprintf(stderr, "llmls: malformed getattrlistbulk buffer\n");
                return -1;
            }
            uint32_t length = *(uint32_t *)p;
            if (length < sizeof(uint32_t) + sizeof(attribute_set_t) ||
                (size_t)(attrBufEnd - p) < length) {
                fprintf(stderr,
                        "llmls: malformed getattrlistbulk entry (length=%u)\n",
                        length);
                return -1;
            }
            char *field = p + sizeof(uint32_t);
            char * const entry_end = p + length;
            p = entry_end; /* advance to next entry */

            attribute_set_t returned = *(attribute_set_t *)field;
            field += sizeof(attribute_set_t);

            /* getattrlistbulk packs attributes in ascending bit-value order,
             * so they must be parsed in that order regardless of how they were
             * requested. For the requested common attrs the order is:
             *   NAME < OBJTYPE < MODTIME < OWNERID < GRPID < ACCESSMASK < ERROR
             * (ATTR_CMN_ERROR = 0x20000000 has the highest bit, so it is last),
             * followed by the file attrs (ATTR_FILE_DATALENGTH). */
            uint32_t error = 0;

            const char *name = NULL;
            if (returned.commonattr & ATTR_CMN_NAME) {
                if (field + sizeof(attrreference_t) > entry_end) goto malformed;
                attrreference_t *ref = (attrreference_t *)field;
                char *name_ptr = (char *)ref + ref->attr_dataoffset;
                /* The name string must lie entirely within this entry and be
                 * NUL-terminated. */
                if (ref->attr_length == 0 ||
                    name_ptr < (entry_end - length) ||
                    name_ptr + ref->attr_length > entry_end ||
                    name_ptr[ref->attr_length - 1] != '\0') {
                    fprintf(stderr,
                            "llmls: getattrlistbulk name reference out of bounds\n");
                    return -1;
                }
                name = name_ptr;
                field += sizeof(attrreference_t);
            }

            fsobj_type_t obj_type = 0;
            if (returned.commonattr & ATTR_CMN_OBJTYPE) {
                if (field + sizeof(fsobj_type_t) > entry_end) goto malformed;
                obj_type = *(fsobj_type_t *)field;
                field += sizeof(fsobj_type_t);
            }

            struct timespec modtime = {0, 0};
            if (returned.commonattr & ATTR_CMN_MODTIME) {
                if (field + sizeof(struct timespec) > entry_end) goto malformed;
                modtime = *(struct timespec *)field;
                field += sizeof(struct timespec);
            }

            uid_t owner_id = 0;
            if (returned.commonattr & ATTR_CMN_OWNERID) {
                if (field + sizeof(uid_t) > entry_end) goto malformed;
                owner_id = *(uid_t *)field;
                field += sizeof(uid_t);
            }

            gid_t group_id = 0;
            if (returned.commonattr & ATTR_CMN_GRPID) {
                if (field + sizeof(gid_t) > entry_end) goto malformed;
                group_id = *(gid_t *)field;
                field += sizeof(gid_t);
            }

            uint32_t access_mask = 0;
            if (returned.commonattr & ATTR_CMN_ACCESSMASK) {
                if (field + sizeof(uint32_t) > entry_end) goto malformed;
                access_mask = *(uint32_t *)field;
                field += sizeof(uint32_t);
            }

            if (returned.commonattr & ATTR_CMN_ERROR) {
                if (field + sizeof(uint32_t) > entry_end) goto malformed;
                error = *(uint32_t *)field;
                field += sizeof(uint32_t);
            }

            off_t data_length = 0;
            if (returned.fileattr & ATTR_FILE_DATALENGTH) {
                if (field + sizeof(off_t) > entry_end) goto malformed;
                data_length = *(off_t *)field;
                field += sizeof(off_t);
            }

            /* Skip . and .. */
            if (!name)
                continue;
            if (name[0] == '.' &&
                (name[1] == '\0' ||
                 (name[1] == '.' && name[2] == '\0')))
                continue;

            if (should_ignore(name, show_all))
                continue;

            struct entry e = {0};

            if (error) {
                e.name = strdup(name);
                if (!e.name)
                    continue;
                e.type = ENTRY_UNKNOWN;
                e.error = (int)error;
                if (entry_list_push(list, &e) < 0)
                    free(e.name);
                continue;
            }

            e.name = strdup(name);
            if (!e.name)
                continue;
            e.mtime = modtime.tv_sec;
            e.mtime_nsec = modtime.tv_nsec;
            e.mode = (mode_t)access_mask;
            e.uid = owner_id;
            e.gid = group_id;

            if (obj_type == VDIR) {
                e.type = ENTRY_DIR;
            } else if (obj_type == VLNK) {
                e.type = ENTRY_SYMLINK;
                e.size = data_length;
            } else {
                e.type = ENTRY_FILE;
                e.size = data_length;
            }

            add_entry(&e, dirfd, show_all, git_prefix, list, summary);
        }
    }
    return 0;

malformed:
    fprintf(stderr, "llmls: getattrlistbulk entry overran declared length\n");
    return -1;
}
#endif /* __APPLE__ */

int walk_directory(const char *path, int depth, int show_all,
                   const char *git_prefix,
                   struct entry_list *list, struct dir_summary *summary)
{
    (void)depth; /* TODO: recursive walk in v0.2 */
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return -1;

    summary->total_files = 0;
    summary->total_dirs = 0;
    summary->total_size = 0;

#ifdef __APPLE__
    {
        int darwin_rc = walk_directory_darwin(fd, show_all, git_prefix,
                                             list, summary);
        if (darwin_rc == 0) {
            close(fd);
            return 0;
        }
        if (list->count > 0) {
            /* Partial enumeration — cannot safely fall back to POSIX */
            close(fd);
            return -1;
        }
        /* First call failed (e.g., unsupported FS) — fall through to POSIX
         * path. Re-open so the directory iterator state is fresh; the kernel
         * may have advanced the dirfd offset before failing. */
        close(fd);
        fd = open(path, O_RDONLY | O_DIRECTORY);
        if (fd < 0)
            return -1;
        summary->total_files = 0;
        summary->total_dirs = 0;
        summary->total_size = 0;
    }
#endif

    DIR *d = fdopendir(fd);
    if (!d) {
        close(fd);
        return -1;
    }

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
#ifdef __APPLE__
        e.mtime_nsec = st.st_mtimespec.tv_nsec;
#else
        e.mtime_nsec = st.st_mtim.tv_nsec;
#endif
        e.mode = st.st_mode;
        e.uid = st.st_uid;
        e.gid = st.st_gid;

        if (S_ISDIR(st.st_mode)) {
            e.type = ENTRY_DIR;
        } else if (S_ISLNK(st.st_mode)) {
            e.type = ENTRY_SYMLINK;
            e.size = st.st_size;
        } else {
            e.type = ENTRY_FILE;
            e.size = st.st_size;
        }

        add_entry(&e, fd, show_all, git_prefix, list, summary);
    }

    closedir(d); /* also closes fd */
    return 0;
}
