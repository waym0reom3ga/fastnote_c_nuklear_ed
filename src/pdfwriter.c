#include "pdfwriter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* simple local byte buffer */
typedef struct {
    unsigned char *data;
    size_t len, cap;
} PB;

static void pb_addn(PB *b, const void *src, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->len + n + 1)
            cap *= 2;
        b->data = realloc(b->data, cap);
        b->cap = cap;
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void pb_add(PB *b, const char *s) { pb_addn(b, s, strlen(s)); }

unsigned char *pdf_from_lines(const char *text, int font_pt, size_t *out_len) {
    if (font_pt <= 0)
        font_pt = 11;
    const double page_height = 842.0, page_width = 595.0;
    const double line_h = font_pt * 1.32, margin = 56.0;
    const double usable = page_height - 2 * margin;

    /* split + truncate lines */
    char **lines = NULL;
    size_t n = 0, cap = 0;
    const char *p = text, *e;
    while (*p) {
        e = strchr(p, '\n');
        if (!e)
            e = p + strlen(p);
        size_t len = (size_t)(e - p);
        if (len > 96)
            len = 96;
        if (n >= cap) {
            cap = cap ? cap * 2 : 64;
            lines = realloc(lines, cap * sizeof(char *));
        }
        char *line = malloc(len + 1);
        memcpy(line, p, len);
        line[len] = '\0';
        if (line[0] == '\0' || strspn(line, " ") == len)
            line[0] = ' ';
        lines[n++] = line;
        p = *e ? e + 1 : e;
    }
    if (!n) {
        lines = malloc(sizeof(char *));
        lines[0] = strdup(" ");
        n = 1;
    }

    /* paginate */
    char ***pages = malloc(sizeof(char **));
    size_t *pg_n = malloc(sizeof(size_t));
    size_t npages = 0;
    double used = 0.0;
    pages[npages] = malloc(n * sizeof(char *));
    pg_n[npages] = 0;
    for (size_t i = 0; i < n; i++) {
        if (used + line_h > usable) {
            npages++;
            pages = realloc(pages, (npages + 1) * sizeof(char **));
            pg_n = realloc(pg_n, (npages + 1) * sizeof(size_t));
            pages[npages] = malloc(n * sizeof(char *));
            pg_n[npages] = 0;
            used = 0.0;
        }
        pages[npages][pg_n[npages]++] = lines[i];
        used += line_h;
    }
    npages++;

    /* content streams */
    PB *objects = malloc(npages * sizeof(PB));
    for (size_t pgi = 0; pgi < npages; pgi++) {
        memset(&objects[pgi], 0, sizeof(PB));
        pb_add(&objects[pgi], "stream\n");
        double y = page_height - margin;
        for (size_t k = 0; k < pg_n[pgi]; k++) {
            char buf[4096];
            int bi = snprintf(buf, sizeof(buf), "BT /F1 %d Tf %.1f %.1f Td (",
                              font_pt, margin, y);
            for (const char *q = pages[pgi][k]; *q; q++) {
                if (bi >= (int)sizeof(buf) - 8)
                    break;
                if (*q == '\\' || *q == '(' || *q == ')')
                    buf[bi++] = '\\';
                buf[bi++] = *q;
            }
            bi += snprintf(buf + bi, sizeof(buf) - bi, ") Tj ET\n");
            pb_add(&objects[pgi], buf);
            y -= line_h;
        }
        pb_add(&objects[pgi], "endstream");
    }

    size_t font_obj = 3 + npages;
    PB out; memset(&out, 0, sizeof(out));
    size_t *xref_offs = malloc((3 + npages * 2 + 2) * sizeof(size_t));
    size_t xref_n = 0;

    pb_add(&out, "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n");
    xref_offs[xref_n++] = out.len;
    pb_add(&out, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    xref_offs[xref_n++] = out.len;
    {
        char kids[512] = "";
        size_t koff = 0;
        for (size_t i = 0; i < npages; i++)
            koff += snprintf(kids + koff, sizeof(kids) - koff, "%zu 0 R ", 3 + i);
        char head[600];
        snprintf(head, sizeof(head),
                 "2 0 obj\n<< /Type /Pages /Kids [%s] /Count %zu >>\nendobj\n",
                 kids, npages);
        pb_add(&out, head);
    }
    for (size_t i = 0; i < npages; i++) {
        size_t obj = 3 + i;
        xref_offs[xref_n++] = out.len;
        char head[512];
        snprintf(head, sizeof(head),
                 "%zu 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.0f %.0f] "
                 "/Resources << /Font << /F1 %zu 0 R >> >> /Contents %zu 0 R >>\nendobj\n",
                 obj, page_width, page_height, font_obj, obj + npages);
        pb_add(&out, head);
        xref_offs[xref_n++] = out.len;
        PB *co = &objects[i];
        char sh[128];
        int sl = snprintf(sh, sizeof(sh), "%zu 0 obj\n<< /Length %zu >>\n",
                          obj + npages, co->len);
        pb_addn(&out, sh, (size_t)sl);
        pb_addn(&out, co->data, co->len);
        pb_add(&out, "\nendobj\n");
        free(pages[i]);
    }
    free(pages);
    xref_offs[xref_n++] = out.len;
    {
        char fh[256];
        snprintf(fh, sizeof(fh),
                 "%zu 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n",
                 font_obj);
        pb_add(&out, fh);
    }
    size_t xref_pos = out.len;
    {
        char xh[64];
        snprintf(xh, sizeof(xh), "xref\n0 %zu\n", font_obj + 1);
        pb_add(&out, xh);
    }
    pb_add(&out, "0000000000 65535 f \n");
    for (size_t i = 0; i < xref_n; i++) {
        char ent[32];
        snprintf(ent, sizeof(ent), "%010zu 00000 n \n", xref_offs[i]);
        pb_add(&out, ent);
    }
    {
        char th[256];
        snprintf(th, sizeof(th), "trailer\n<< /Size %zu /Root 1 0 R >>\nstartxref\n%zu\n%%EOF\n",
                 font_obj + 1, xref_pos);
        pb_add(&out, th);
    }

    for (size_t i = 0; i < npages; i++) {
        free(objects[i].data);
    }
    free(objects);
    free(pg_n);
    for (size_t i = 0; i < n; i++)
        free(lines[i]);
    free(lines);
    free(xref_offs);
    *out_len = out.len;
    return out.data;
}