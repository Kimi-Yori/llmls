#include "entry.h"
#include <stdio.h>
#include <string.h>

/* Write a JSON-safe string (escape control chars and quotes) */
static void json_string(const char *s)
{
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout);  break;
        case '\t': fputs("\\t", stdout);  break;
        case '\r': fputs("\\r", stdout);  break;
        default:
            if (*p < 0x20 || *p == 0x7f) {
                printf("\\u%04x", *p);
            } else if (*p >= 0x80) {
                /* Non-ASCII: could be valid UTF-8 or arbitrary bytes.
                 * Escape as \uXXXX to guarantee valid JSON. */
                printf("\\u%04x", *p);
            } else {
                putchar(*p);
            }
        }
    }
    putchar('"');
}

static const char *type_json(enum entry_type t)
{
    switch (t) {
    case ENTRY_FILE:    return "file";
    case ENTRY_DIR:     return "dir";
    case ENTRY_SYMLINK: return "symlink";
    default:            return "unknown";
    }
}

static void json_long_fields(const struct entry *e)
{
    char modebuf[8];
    char ownerbuf[64];
    char groupbuf[64];

    format_mode(e->mode, modebuf, sizeof(modebuf));
    format_owner(e->uid, ownerbuf, sizeof(ownerbuf));
    format_group(e->gid, groupbuf, sizeof(groupbuf));

    printf(",\"mode\":");
    json_string(modebuf);
    printf(",\"owner\":");
    json_string(ownerbuf);
    printf(",\"group\":");
    json_string(groupbuf);
}

void render_json(const struct dir_summary *summary,
                 const struct entry_list *list, time_t now, int show_long)
{
    printf("{\"path\":");
    json_string(summary->path);
    printf(",\"files\":%d,\"dirs\":%d,\"size\":%lld,\"entries\":[",
           summary->total_files, summary->total_dirs,
           (long long)summary->total_size);

    for (size_t i = 0; i < list->count; i++) {
        const struct entry *e = &list->entries[i];

        if (i > 0)
            putchar(',');

        printf("{\"type\":\"%s\",\"name\":", type_json(e->type));
        json_string(e->name);

        if (e->error) {
            printf(",\"error\":\"%s\"", errno_str(e->error));
        } else if (e->type == ENTRY_DIR) {
            if (e->dir_files >= 0)
                printf(",\"files\":%d", e->dir_files);
        } else {
            printf(",\"size\":%lld", (long long)e->size);
            long age_sec = (long)(now - e->mtime);
            if (age_sec < 0) age_sec = 0;
            printf(",\"age_seconds\":%ld", age_sec);

            if (show_long && e->type == ENTRY_SYMLINK && e->link_target) {
                printf(",\"target\":");
                json_string(e->link_target);
            }
        }

        if (show_long && !e->error)
            json_long_fields(e);

        /* git status — only include if not clean */
        const char *gs = git_status_str(e->git);
        if (gs) {
            printf(",\"git\":\"%s\"", gs);
        }

        putchar('}');
    }

    puts("]}");
}
