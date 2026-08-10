/* FastNote C editions — document model implementation. */

#include "app.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char errbuf[512];

const char *fn_error(void) { return errbuf[0] ? errbuf : NULL; }

void fn_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(errbuf, sizeof(errbuf), fmt, ap);
    va_end(ap);
}

char *xstrdup(const char *s) {
    char *p = malloc(strlen(s) + 1);
    if (p)
        strcpy(p, s);
    return p;
}

char *xstrndup(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p)
        return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *read_file_all(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len)
        *out_len = got;
    return buf;
}

static int write_file_all(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return (w == len) ? 0 : -1;
}

char *doc_open(Document *d, const char *path) {
    size_t len = 0;
    char *text = read_file_all(path, &len);
    if (!text) {
        fn_set_error("cannot open %s: no such file or permission denied", path);
        return errbuf;
    }
    free(d->text);
    free(d->path);
    d->text = text;
    d->path = xstrdup(path);
    d->dirty = false;
    return NULL;
}

void doc_set_text(Document *d, const char *text) {
    if (strcmp(d->text, text) != 0) {
        free(d->text);
        d->text = xstrdup(text);
        d->dirty = true;
    }
}

void doc_insert_text(Document *d, const char *text) {
    size_t old = strlen(d->text);
    size_t add = strlen(text);
    char *nbuf = malloc(old + add + 1);
    if (!nbuf)
        return;
    memcpy(nbuf, d->text, old);
    memcpy(nbuf + old, text, add + 1);
    free(d->text);
    d->text = nbuf;
    d->dirty = true;
}

char *doc_save(Document *d) {
    if (!d->path) {
        fn_set_error("no file name: use save-as (FR-6)");
        return errbuf;
    }
    if (write_file_all(d->path, d->text, strlen(d->text)) != 0) {
        fn_set_error("cannot save %s: write failed", d->path);
        return errbuf;
    }
    d->dirty = false;
    return NULL;
}

char *doc_save_as(Document *d, const char *path) {
    if (write_file_all(path, d->text, strlen(d->text)) != 0) {
        fn_set_error("cannot save %s: write failed", path);
        return errbuf;
    }
    free(d->path);
    d->path = xstrdup(path);
    d->dirty = false;
    return NULL;
}

void doc_free(Document *d) {
    free(d->path);
    free(d->text);
    d->path = NULL;
    d->text = NULL;
}

AppState *app_state_new(const char *notes_dir) {
    AppState *s = calloc(1, sizeof(AppState));
    if (!s)
        return NULL;
    s->doc.text = xstrdup("");
    if (notes_dir && *notes_dir) {
        s->notes_dir = xstrdup(notes_dir);
    } else {
        const char *home = getenv("HOME");
        s->notes_dir = xstrdup(home && *home ? home : "/tmp");
    }
    return s;
}

void app_state_free(AppState *s) {
    if (!s)
        return;
    doc_free(&s->doc);
    free(s->notes_dir);
    free(s);
}