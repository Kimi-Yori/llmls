#include "entry.h"
#include <stdio.h>
#include <string.h>

static const char *type_str(enum entry_type t)
{
    switch (t) {
    case ENTRY_FILE:    return "file";
    case ENTRY_DIR:     return "dir";
    case ENTRY_SYMLINK: return "symlink";
    default:            return "unknown";
    }
}

/*
 * Default self-explanatory format:
 *
 * dir claude/ files:14 dirs:3 size:180K
 *   file CLAUDE.md size:43K age:2d git:modified
 *   dir Manual/ files:28
 */
void render_default(const struct dir_summary *summary,
                    const struct entry_list *list, time_t now, int indent)
{
    (void)indent;
    char sizebuf[32];

    /* header line */
    format_size(summary->total_size, sizebuf, sizeof(sizebuf));
    printf("dir %s files:%d dirs:%d size:%s\n",
           summary->path, summary->total_files, summary->total_dirs, sizebuf);

    /* entries */
    for (size_t i = 0; i < list->count; i++) {
        const struct entry *e = &list->entries[i];
        char *esc = escape_filename(e->name);
        const char *name = esc ? esc : e->name;

        printf("  %s %s", type_str(e->type), name);

        if (e->error) {
            printf(" error:%s", errno_str(e->error));
        } else if (e->type == ENTRY_DIR) {
            if (e->dir_files >= 0)
                printf(" files:%d", e->dir_files);
        } else {
            /* size */
            format_size(e->size, sizebuf, sizeof(sizebuf));
            printf(" size:%s", sizebuf);

            /* age */
            char agebuf[32];
            format_age(e->mtime, now, agebuf, sizeof(agebuf));
            printf(" age:%s", agebuf);
        }

        /* git status */
        const char *gs = git_status_str(e->git);
        if (gs)
            printf(" git:%s", gs);

        printf("\n");

        if (esc)
            free(esc);
    }
}

/*
 * Dense format:
 *
 * . d=3 f=14 sz=180K
 * f M. CLAUDE.md 43K 2d
 * d .. Manual/ f=28
 */
void render_dense(const struct dir_summary *summary,
                  const struct entry_list *list, time_t now, int indent)
{
    (void)indent;
    char sizebuf[32];

    format_size(summary->total_size, sizebuf, sizeof(sizebuf));
    printf(". d=%d f=%d sz=%s\n",
           summary->total_dirs, summary->total_files, sizebuf);

    for (size_t i = 0; i < list->count; i++) {
        const struct entry *e = &list->entries[i];
        char *esc = escape_filename(e->name);
        const char *name = esc ? esc : e->name;

        /* type char */
        char tc = 'f';
        if (e->type == ENTRY_DIR) tc = 'd';
        else if (e->type == ENTRY_SYMLINK) tc = 'l';

        /* git porcelain */
        const char *gp = git_status_porcelain(e->git);

        if (e->type == ENTRY_DIR) {
            if (e->dir_files >= 0)
                printf("%c %s %s f=%d\n", tc, gp, name, e->dir_files);
            else
                printf("%c %s %s\n", tc, gp, name);
        } else {
            format_size(e->size, sizebuf, sizeof(sizebuf));
            char agebuf[32];
            format_age(e->mtime, now, agebuf, sizeof(agebuf));
            printf("%c %s %s %s %s\n", tc, gp, name, sizebuf, agebuf);
        }

        if (esc)
            free(esc);
    }
}
