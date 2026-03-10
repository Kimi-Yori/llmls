#include "entry.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>

/* Important filenames (project skeleton markers) */
static const char *important_files[] = {
    "README.md",
    "README",
    "README.txt",
    "CLAUDE.md",
    "AGENTS.md",
    "package.json",
    "pyproject.toml",
    "go.mod",
    "Cargo.toml",
    "Makefile",
    "Dockerfile",
    "Gemfile",
    "composer.json",
    "build.gradle",
    "CMakeLists.txt",
    NULL,
};

static int is_important(const char *name)
{
    for (const char **p = important_files; *p; p++) {
        if (strcasecmp(name, *p) == 0)
            return 1;
    }
    return 0;
}

static int is_git_changed(enum git_status s)
{
    return s == GIT_MODIFIED || s == GIT_ADDED || s == GIT_DELETED
        || s == GIT_RENAMED || s == GIT_CONFLICTED || s == GIT_UNTRACKED;
}

void assign_priority(struct entry *e)
{
    if (is_git_changed(e->git)) {
        e->priority = SORT_GIT_CHANGED;
    } else if (e->type == ENTRY_DIR) {
        e->priority = SORT_DIR;
    } else if (is_important(e->name)) {
        e->priority = SORT_IMPORTANT;
    } else {
        e->priority = SORT_FILE;
    }
}

static int entry_cmp(const void *a, const void *b)
{
    const struct entry *ea = a;
    const struct entry *eb = b;

    /* primary: sort priority */
    if (ea->priority != eb->priority)
        return (int)ea->priority - (int)eb->priority;

    /* secondary: alphabetical (case-insensitive) */
    return strcasecmp(ea->name, eb->name);
}

void sort_entries(struct entry_list *list)
{
    /* assign priorities first */
    for (size_t i = 0; i < list->count; i++)
        assign_priority(&list->entries[i]);

    qsort(list->entries, list->count, sizeof(struct entry), entry_cmp);
}
