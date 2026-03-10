#include "entry.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void format_size(off_t bytes, char *buf, size_t bufsz)
{
    if (bytes < 1024) {
        snprintf(buf, bufsz, "%dB", (int)bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, bufsz, "%dK", (int)(bytes / 1024));
    } else if (bytes < (off_t)1024 * 1024 * 1024) {
        snprintf(buf, bufsz, "%dM", (int)(bytes / (1024 * 1024)));
    } else {
        snprintf(buf, bufsz, "%dG", (int)(bytes / ((off_t)1024 * 1024 * 1024)));
    }
}

void format_age(time_t mtime, time_t now, char *buf, size_t bufsz)
{
    long diff = (long)(now - mtime);

    if (diff < 0)
        diff = 0;

    if (diff < 60) {
        snprintf(buf, bufsz, "%lds", diff);
    } else if (diff < 3600) {
        snprintf(buf, bufsz, "%ldm", diff / 60);
    } else if (diff < 86400) {
        snprintf(buf, bufsz, "%ldh", diff / 3600);
    } else if (diff < 86400 * 7) {
        snprintf(buf, bufsz, "%ldd", diff / 86400);
    } else if (diff < 86400 * 30) {
        snprintf(buf, bufsz, "%ldw", diff / (86400 * 7));
    } else if (diff < 86400 * 365) {
        snprintf(buf, bufsz, "%ldmo", diff / (86400 * 30));
    } else {
        snprintf(buf, bufsz, "%ldy", diff / (86400 * 365));
    }
}

const char *git_status_str(enum git_status s)
{
    switch (s) {
    case GIT_MODIFIED:   return "modified";
    case GIT_ADDED:      return "added";
    case GIT_UNTRACKED:  return "untracked";
    case GIT_RENAMED:    return "renamed";
    case GIT_DELETED:    return "deleted";
    case GIT_CONFLICTED: return "conflicted";
    default:             return NULL;
    }
}

const char *git_status_porcelain(enum git_status s)
{
    switch (s) {
    case GIT_MODIFIED:   return "M.";
    case GIT_ADDED:      return "A.";
    case GIT_UNTRACKED:  return "??";
    case GIT_RENAMED:    return "R.";
    case GIT_DELETED:    return "D.";
    case GIT_CONFLICTED: return "UU";
    default:             return "..";
    }
}

int entry_list_init(struct entry_list *list)
{
    list->count = 0;
    list->capacity = 64;
    list->entries = malloc(sizeof(struct entry) * list->capacity);
    return list->entries ? 0 : -1;
}

int entry_list_push(struct entry_list *list, struct entry *e)
{
    if (list->count >= list->capacity) {
        size_t newcap = list->capacity * 2;
        struct entry *tmp = realloc(list->entries, sizeof(struct entry) * newcap);
        if (!tmp)
            return -1;
        list->entries = tmp;
        list->capacity = newcap;
    }
    list->entries[list->count++] = *e;
    return 0;
}

void entry_list_free(struct entry_list *list)
{
    for (size_t i = 0; i < list->count; i++) {
        free(list->entries[i].name);
        free(list->entries[i].link_target);
    }
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
}
