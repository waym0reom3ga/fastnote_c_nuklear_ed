/* FastNote C editions — export writers (HTML FR-7, PDF FR-8). */

#include "app.h"
#include "export.h"
#include "pdfwriter.h"
#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *build_save_path(const char *path, const char *ext) {
    size_t n = strlen(path);
    if (n >= 4 && strcmp(path + n - 4, ext) == 0)
        return strdup(path);
    size_t len = n + 4 + 1;
    char *out = malloc(len);
    if (!out)
        return NULL;
    snprintf(out, len, "%s%s", path, ext);
    return out;
}

char *write_pdf_export(const char *md_text, const char *path) {
    char *plain = render_plain(md_text);
    if (!plain)
        return "pdf: allocation failed";
    size_t len = 0;
    unsigned char *blob = pdf_from_lines(plain, 11, &len);
    free(plain);
    if (!blob || len == 0)
        return "pdf: allocation failed";
    char *out_path = build_save_path(path, ".pdf");
    if (!out_path) {
        free(blob);
        return "pdf: allocation failed";
    }
    FILE *f = fopen(out_path, "wb");
    if (!f) {
        free(blob);
        free(out_path);
        return "pdf: cannot open output file";
    }
    size_t wrote = fwrite(blob, 1, len, f);
    int rc = fclose(f);
    free(blob);
    free(out_path);
    if (wrote != len || rc != 0)
        return "pdf: disk write failed";
    return NULL;
}

char *write_html_export(const char *md_text, const char *title,
                        const char *theme, const char *custom_css,
                        const char *path) {
    char *html = render_page(md_text, title, theme, custom_css);
    if (!html)
        return "html: allocation failed";
    char *out_path = build_save_path(path, ".html");
    if (!out_path) {
        free(html);
        return "html: allocation failed";
    }
    FILE *f = fopen(out_path, "wb");
    if (!f) {
        free(html);
        free(out_path);
        return "html: cannot open output file";
    }
    size_t len = strlen(html);
    size_t wrote = fwrite(html, 1, len, f);
    int rc = fclose(f);
    free(html);
    free(out_path);
    if (wrote != len || rc != 0)
        return "html: disk write failed";
    return NULL;
}