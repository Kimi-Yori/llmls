#include "entry.h"
#include <string.h>

/* Built-in ignore list */
static const char *ignore_dirs[] = {
    ".git",
    "node_modules",
    "__pycache__",
    ".venv",
    ".idea",
    ".vscode",
    NULL,
};

static const char *ignore_files[] = {
    ".DS_Store",
    NULL,
};

static const char *ignore_suffixes[] = {
    ".pyc",
    NULL,
};

int should_ignore(const char *name, int show_all)
{
    if (show_all)
        return 0;

    /* skip dotfiles starting with . (hidden) — except . and .. which
       the caller already skips */
    /* We don't skip all dotfiles; only the specific ones in the ignore list.
       Dotfiles like .env, .bashrc may be relevant. */

    /* check against ignore dirs/files lists */
    for (const char **p = ignore_dirs; *p; p++) {
        if (strcmp(name, *p) == 0)
            return 1;
    }
    for (const char **p = ignore_files; *p; p++) {
        if (strcmp(name, *p) == 0)
            return 1;
    }

    /* check suffixes */
    size_t nlen = strlen(name);
    for (const char **p = ignore_suffixes; *p; p++) {
        size_t slen = strlen(*p);
        if (nlen >= slen && strcmp(name + nlen - slen, *p) == 0)
            return 1;
    }

    return 0;
}
