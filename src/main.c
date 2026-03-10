#include "entry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>

static void usage(void)
{
    fprintf(stderr,
        "llmls %s - LLM-optimized file listing\n"
        "\n"
        "Usage: llmls [options] [path]\n"
        "\n"
        "Options:\n"
        "  -a, --all      Show all files (include ignored)\n"
        "  -d, --dense    Dense output format\n"
        "  -j, --json     JSON output format\n"
        "  --depth N      Listing depth (default: 1)\n"
        "  -h, --help     Show this help\n"
        "  -v, --version  Show version\n",
        LLMLS_VERSION);
}

static int parse_options(int argc, char **argv, struct options *opts)
{
    opts->path = ".";
    opts->depth = 1;
    opts->show_all = 0;
    opts->mode = OUTPUT_DEFAULT;
    opts->show_help = 0;
    opts->show_version = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) {
            opts->show_all = 1;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dense") == 0) {
            opts->mode = OUTPUT_DENSE;
        } else if (strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--json") == 0) {
            opts->mode = OUTPUT_JSON;
        } else if (strcmp(argv[i], "--depth") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "llmls: --depth requires a number\n");
                return -1;
            }
            opts->depth = atoi(argv[++i]);
            if (opts->depth < 1) opts->depth = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            opts->show_help = 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            opts->show_version = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "llmls: unknown option: %s\n", argv[i]);
            return -1;
        } else {
            opts->path = argv[i];
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    struct options opts;
    if (parse_options(argc, argv, &opts) < 0)
        return 1;

    if (opts.show_version) {
        printf("llmls %s\n", LLMLS_VERSION);
        return 0;
    }

    if (opts.show_help) {
        usage();
        return 0;
    }

    /* Determine display path */
    const char *display_path = opts.path;
    /* Add trailing slash for readability */
    size_t plen = strlen(display_path);
    char *display_buf = NULL;
    if (plen > 0 && display_path[plen - 1] != '/') {
        display_buf = malloc(plen + 2);
        if (display_buf) {
            memcpy(display_buf, display_path, plen);
            display_buf[plen] = '/';
            display_buf[plen + 1] = '\0';
            display_path = display_buf;
        }
    }

    /* Load git status if in a repo */
    char *git_root = git_find_root(opts.path);
    char *git_prefix = NULL; /* relative path from repo root to target dir */
    if (git_root) {
        git_load_status(git_root);

        /* Compute relative prefix: realpath(opts.path) minus git_root */
        char abspath[PATH_MAX];
        if (realpath(opts.path, abspath)) {
            size_t root_len = strlen(git_root);
            if (strncmp(abspath, git_root, root_len) == 0) {
                if (abspath[root_len] == '/') {
                    git_prefix = strdup(abspath + root_len + 1);
                } else if (abspath[root_len] == '\0') {
                    git_prefix = strdup("");
                }
            }
        }
    }

    /* Walk directory */
    struct entry_list list;
    struct dir_summary summary = {0};
    summary.path = display_path;

    if (entry_list_init(&list) < 0) {
        fprintf(stderr, "llmls: out of memory\n");
        free(display_buf);
        return 1;
    }

    if (walk_directory(opts.path, opts.depth, opts.show_all, git_prefix, &list, &summary) < 0) {
        fprintf(stderr, "llmls: cannot open '%s'\n", opts.path);
        entry_list_free(&list);
        free(display_buf);
        free(git_root);
        free(git_prefix);
        git_cleanup();
        return 1;
    }

    /* Sort */
    sort_entries(&list);

    /* Render */
    time_t now = time(NULL);

    switch (opts.mode) {
    case OUTPUT_JSON:
        render_json(&summary, &list, now);
        break;
    case OUTPUT_DENSE:
        render_dense(&summary, &list, now, 0);
        break;
    default:
        render_default(&summary, &list, now, 0);
        break;
    }

    /* Cleanup */
    entry_list_free(&list);
    free(display_buf);
    free(git_root);
    free(git_prefix);
    git_cleanup();

    return 0;
}
