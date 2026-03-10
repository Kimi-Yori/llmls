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
            if (*p < 0x20) {
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

void render_json(const struct dir_summary *summary,
                 const struct entry_list *list, time_t now)
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
            printf(",\"error\":\"permission_denied\"");
        } else if (e->type == ENTRY_DIR) {
            if (e->dir_files >= 0)
                printf(",\"files\":%d", e->dir_files);
        } else {
            printf(",\"size\":%lld", (long long)e->size);
            long age_sec = (long)(now - e->mtime);
            if (age_sec < 0) age_sec = 0;
            printf(",\"age_seconds\":%ld", age_sec);
        }

        /* git status — only include if not clean */
        const char *gs = git_status_str(e->git);
        if (gs) {
            printf(",\"git\":\"%s\"", gs);
        }

        putchar('}');
    }

    puts("]}");
}
