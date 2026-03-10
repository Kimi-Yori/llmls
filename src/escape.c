#include "entry.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Escape control characters in filenames.
 * \n → \\n, \t → \\t, \r → \\r, other ctrl → \\xNN
 * Returns allocated string (caller frees) or NULL on alloc failure.
 */
char *escape_filename(const char *name)
{
    /* worst case: every byte becomes 4 chars (\\xNN) */
    size_t len = strlen(name);
    char *buf = malloc(len * 4 + 1);
    if (!buf)
        return NULL;

    char *p = buf;
    for (const unsigned char *s = (const unsigned char *)name; *s; s++) {
        if (*s == '\n') {
            *p++ = '\\'; *p++ = 'n';
        } else if (*s == '\t') {
            *p++ = '\\'; *p++ = 't';
        } else if (*s == '\r') {
            *p++ = '\\'; *p++ = 'r';
        } else if (*s < 0x20 || *s == 0x7f) {
            /* other control chars */
            static const char hex[] = "0123456789abcdef";
            *p++ = '\\';
            *p++ = 'x';
            *p++ = hex[*s >> 4];
            *p++ = hex[*s & 0x0f];
        } else {
            *p++ = (char)*s;
        }
    }
    *p = '\0';
    return buf;
}

void write_escaped(int fd, const char *s)
{
    char *esc = escape_filename(s);
    if (esc) {
        ssize_t r = write(fd, esc, strlen(esc));
        (void)r;
        free(esc);
    }
}
