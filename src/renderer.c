/* FastNote C editions — markdown renderer implementation. */

#include "renderer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app.h"

/* ------------------------------------------------------------ strbuf */

typedef struct {
    char *data;
    size_t len, cap;
} SB;

static void sb_init(SB *b) { b->data = NULL; b->len = 0; b->cap = 0; }
static void sb_free(SB *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }

static void sb_addn(SB *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->len + n + 1)
            cap *= 2;
        b->data = realloc(b->data, cap);
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void sb_add(SB *b, const char *s) { sb_addn(b, s, strlen(s)); }

static char *itoa_c(int n) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", n);
    return strdup(buf);
}

/* forward declarations for block helpers */
typedef struct { char **rows; size_t n, cap; } StrVec;
typedef struct { const char *content, *checkbox; size_t indent; } ListItem;

static void sab_flush_list(SB *out, ListItem *items, size_t n, bool ordered,
                           const char *base_dir);
static void sab_flush_quote(SB *out, StrVec *quote, const char *base_dir);
static void sab_flush_table(SB *out, StrVec *table, const char *base_dir);
static char *render_inline(const char *text, const char *base_dir);

/* ------------------------------------------------------------ escape */

static void sb_add_esc(SB *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
        case '&': sb_add(b, "&amp;"); break;
        case '<': sb_add(b, "&lt;"); break;
        case '>': sb_add(b, "&gt;"); break;
        default:  sb_addn(b, s + i, 1); break;
        }
    }
}

static void eb_add_esc(SB *b, const char *s) { sb_add_esc(b, s, strlen(s)); }

/* ------------------------------------------------------------ blocks */

static void sv_push(StrVec *v, const char *s) {
    if (v->n >= v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->rows = realloc(v->rows, v->cap * sizeof(char *));
    }
    v->rows[v->n++] = strdup(s);
}

static void sv_clear(StrVec *v) {
    for (size_t i = 0; i < v->n; i++)
        free(v->rows[i]);
    free(v->rows);
    v->rows = NULL;
    v->n = v->cap = 0;
}

static void split_cells(const char *line, StrVec *cells) {
    const char *s = line;
    while (*s == ' ')
        s++;
    if (*s == '|') s++; /* skip leading pipe */
    char *tmp = strdup(s);
    size_t len = strlen(tmp);
    if (len && tmp[len - 1] == '|')
        tmp[len - 1] = '\0';
    char *p = tmp;
    while (*p) {
        char *start = p;
        while (*p && *p != '|')
            p++;
        char *cell = strndup(start, (size_t)(p - start));
        /* left-trim */
        const char *cptr = cell;
        while (*cptr == ' ') cptr++;
        size_t len_cell = strlen(cptr);
        /* right-trim: find last non-space */
        while (len_cell && cptr[len_cell - 1] == ' ')
            len_cell--;
        char *trimmed = strndup(cptr, len_cell);
        sv_push(cells, trimmed);
        free(cell);
        if (*p == '|')
            p++;
    }
    free(tmp);
}

static void sab_flush_list(SB *out, ListItem *items, size_t n, bool ordered,
                           const char *base_dir) {
    if (!n)
        return;
    sb_add(out, ordered ? "<ol>" : "<ul>");
    for (size_t k = 0; k < n; k++) {
        sb_add(out, "<li>");
        if (items[k].checkbox && *items[k].checkbox)
            sb_add(out, "<input type=\"checkbox\" checked disabled> ");
        char *inner = render_inline(items[k].content, base_dir);
        sb_add(out, inner);
        free(inner);
        sb_add(out, "</li>");
    }
    sb_add(out, ordered ? "</ol>" : "</ul>");
}

static void sab_flush_quote(SB *out, StrVec *quote, const char *base_dir) {
    if (!quote->n)
        return;
    sb_add(out, "<blockquote>");
    for (size_t k = 0; k < quote->n; k++) {
        if (k)
            sb_add(out, "<br>\n");
        char *inl = render_inline(quote->rows[k], base_dir);
        sb_add(out, inl);
        free(inl);
    }
    sb_add(out, "</blockquote>");
}

static void sab_flush_table(SB *out, StrVec *table, const char *base_dir) {
    if (!table->n)
        return;
    sb_add(out, "<table>");
    for (size_t r = 0; r < table->n; r++) {
        StrVec cells = {0};
        split_cells(table->rows[r], &cells);
        sb_add(out, "<tr>");
        for (size_t c = 0; c < cells.n; c++) {
            sb_add(out, r == 0 ? "<th>" : "<td>");
            char *inl = render_inline(cells.rows[c], base_dir);
            sb_add(out, inl);
            free(inl);
            sb_add(out, r == 0 ? "</th>" : "</td>");
        }
        sb_add(out, "</tr>");
        sv_clear(&cells);
    }
    sb_add(out, "</table>");
}

/* ------------------------------------------------------------ inline */

typedef struct {
    int g[20][2];
} InlineMatch;

/* find the leftmost inline construct at or after pos. */
static int find_inline(const char *s, size_t from, InlineMatch *m) {
    memset(m, -1, sizeof(m->g));
    size_t tlen = strlen(s);
    for (size_t i = from; i < tlen; i++) {
        char c = s[i];
        if (c == '`') {
            size_t j = i + 1;
            while (j < tlen && s[j] != '`')
                j++;
            if (j < tlen) {
                m->g[1][0] = (int)i; m->g[1][1] = (int)j + 1;
                return 1;
            }
        }
        if (c == '$') {
            bool d2 = (i + 1 < tlen && s[i + 1] == '$');
            size_t start = i + (d2 ? 2 : 1);
            for (size_t j = start; j < tlen; j++) {
                if (!d2 && s[j] == '\n')
                    break;
                if (s[j] == '$' && d2 == (j + 1 < tlen && s[j + 1] == '$')) {
                    if (d2) {
                        m->g[2][0] = (int)i; m->g[2][1] = (int)j + 2;
                    } else {
                        m->g[3][0] = (int)i; m->g[3][1] = (int)j + 1;
                    }
                    return 1;
                }
            }
        }
        if (c == '[' && i + 1 < tlen && s[i + 1] == '[') {
            for (size_t j = i + 2; j + 1 < tlen; j++) {
                if (s[j] == ']' && s[j + 1] == ']') {
                    m->g[4][0] = (int)i; m->g[4][1] = (int)j + 2;
                    m->g[5][0] = (int)i + 2;
                    m->g[5][1] = (int)j;
                    for (size_t k = i + 2; k < j; k++)
                        if (s[k] == '|') { m->g[5][1] = (int)k; break; }
                    return 1;
                }
            }
        }
        if (c == '!' || c == '[') {
            bool img = (c == '!');
            size_t bpos = i + (img ? 1 : 0);
            if (img && bpos >= tlen)
                continue;
            size_t k = bpos;
            if (s[k] != '[')
                continue;
            size_t close = k + 1;
            while (close < tlen && s[close] != ']')
                close++;
            if (close >= tlen || close + 1 >= tlen || s[close + 1] != '(')
                continue;
            size_t paren = close + 2;
            size_t seg = paren;
            while (seg < tlen && s[seg] != ')' && s[seg] != ' ' && s[seg] != '\n')
                seg++;
            if (seg > paren && s[seg] == ')') {
                int gi = img ? 6 : 9, gal = img ? 7 : 10, gurl = img ? 8 : 11;
                m->g[gi][0] = (int)i; m->g[gi][1] = (int)seg + 1;
                m->g[gal][0] = (int)k + 1; m->g[gal][1] = (int)close;
                m->g[gurl][0] = (int)paren; m->g[gurl][1] = (int)seg;
                return 1;
            }
        }
        if (c == '*' || c == '_' || c == '~') {
            bool bold = (c == '*' && i + 1 < tlen && s[i + 1] == '*');
            bool strike = (c == '~' && i + 1 < tlen && s[i + 1] == '~');
            size_t start = i + (bold || strike ? 2 : 1);
            for (size_t j = start; j < tlen; j++) {
                if (s[j] == c) {
                    if (bold && s[j + 1] == '*') {
                        m->g[12][0] = (int)i; m->g[12][1] = (int)j + 2;
                        m->g[13][0] = (int)i + 2; m->g[13][1] = (int)j;
                        return 1;
                    }
                    if (strike && s[j + 1] == '~') {
                        m->g[14][0] = (int)i; m->g[14][1] = (int)j + 2;
                        m->g[15][0] = (int)i + 2; m->g[15][1] = (int)j;
                        return 1;
                    }
                    if (!bold && !strike) {
                        int gi = (c == '*') ? 16 : 18;
                        m->g[gi][0] = (int)i; m->g[gi][1] = (int)j + 1;
                        m->g[gi + 1][0] = (int)i + 1; m->g[gi + 1][1] = (int)j;
                        return 1;
                    }
                }
                if (s[j] == '\n' && !bold && !strike)
                    break;
            }
        }
    }
    return 0;
}

static void resolve_wiki(const char *target, const char *base_dir, SB *out) {
    char *base = strdup(target);
    size_t bl = strlen(base);
    if (bl > 3 && (strcasecmp(base + bl - 3, ".md") == 0 ||
                   strcasecmp(base + bl - 3, ".txt") == 0))
        base[bl - 3] = '\0';
    else if (bl > 9 && strcasecmp(base + bl - 9, ".markdown") == 0)
        base[bl - 9] = '\0';
    static const char *exts[] = {".md", ".markdown", ".txt", NULL};
    for (int i = 0; exts[i]; i++) {
        char *cand = malloc(strlen(base_dir ? base_dir : "") + strlen(base) +
                            strlen(exts[i]) + 2);
        if (!cand)
            break;
        if (base_dir && *base_dir)
            sprintf(cand, "%s/%s%s", base_dir, base, exts[i]);
        else
            sprintf(cand, "%s%s", base, exts[i]);
        FILE *f = fopen(cand, "rb");
        if (f) {
            fclose(f);
            sb_addn(out, cand, strlen(cand));
            free(cand);
            break;
        }
        free(cand);
    }
    free(base);
}

static char *render_inline(const char *text, const char *base_dir) {
    SB out; sb_init(&out);
    size_t pos = 0;
    size_t tlen = strlen(text);
    while (pos < tlen) {
        InlineMatch m;
        if (!find_inline(text, pos, &m)) {
            sb_add_esc(&out, text + pos, tlen - pos);
            break;
        }
        /* find the leftmost start among matches */
        int best = -1, best_end = -1;
        for (int k = 1; k <= 19; k++) {
            if (m.g[k][0] >= 0 && (best < 0 || m.g[k][0] < best))
                best = m.g[k][0], best_end = m.g[k][1];
        }
        if (best < 0)
            best = (int)pos, best_end = (int)pos + 1;
        if (best > (int)pos)
            sb_add_esc(&out, text + pos, (size_t)(best - pos));
        /* dispatch by group */
        if (m.g[1][0] >= 0 && m.g[1][0] == best) {
            sb_add(&out, "<code>");
            sb_add_esc(&out, text + m.g[1][0] + 1, (size_t)(m.g[1][1] - m.g[1][0] - 2));
            sb_add(&out, "</code>");
        } else if (m.g[2][0] >= 0 && m.g[2][0] == best) {
            sb_add(&out, "<span class=\"math\">\\(");
            sb_add_esc(&out, text + m.g[2][0] + 2, (size_t)(m.g[2][1] - m.g[2][0] - 4));
            sb_add(&out, "\\)</span>");
        } else if (m.g[3][0] >= 0 && m.g[3][0] == best) {
            sb_add(&out, "<span class=\"math\">\\(");
            sb_add_esc(&out, text + m.g[3][0] + 1, (size_t)(m.g[3][1] - m.g[3][0] - 2));
            sb_add(&out, "\\)</span>");
        } else if (m.g[4][0] >= 0 && m.g[4][0] == best) {
            char *target = strndup(text + m.g[5][0], (size_t)(m.g[5][1] - m.g[5][0]));
            sb_add(&out, "<a class=\"wiki\" href=\"");
            resolve_wiki(target, base_dir, &out);
            sb_add(&out, "\">");
            eb_add_esc(&out, target);
            sb_add(&out, "</a>");
            free(target);
        } else if (((m.g[6][0] >= 0 && m.g[6][0] == best) ||
                    (m.g[9][0] >= 0 && m.g[9][0] == best))) {
            bool img = (m.g[6][0] >= 0 && m.g[6][0] == best);
            int gal = img ? 7 : 10, gurl = img ? 8 : 11;
            if (!img)
                sb_add(&out, "<a href=\"");
            else
                sb_add(&out, "<img alt=\"");
            if (!img) {
                sb_add_esc(&out, text + m.g[gurl][0], (size_t)(m.g[gurl][1] - m.g[gurl][0]));
                sb_add(&out, "\">");
                sb_add_esc(&out, text + m.g[gal][0], (size_t)(m.g[gal][1] - m.g[gal][0]));
                sb_add(&out, "</a>");
            } else {
                sb_add_esc(&out, text + m.g[gal][0], (size_t)(m.g[gal][1] - m.g[gal][0]));
                sb_add(&out, "\" src=\"");
                sb_add_esc(&out, text + m.g[gurl][0], (size_t)(m.g[gurl][1] - m.g[gurl][0]));
                sb_add(&out, "\">");
            }
        } else if (m.g[12][0] >= 0 && m.g[12][0] == best) {
            char *inner = strndup(text + m.g[13][0], (size_t)(m.g[13][1] - m.g[13][0]));
            char *rendered = render_inline(inner, base_dir);
            free(inner);
            sb_add(&out, "<strong>");
            sb_add(&out, rendered);
            sb_add(&out, "</strong>");
            free(rendered);
        } else if (m.g[14][0] >= 0 && m.g[14][0] == best) {
            sb_add(&out, "<del>");
            sb_add_esc(&out, text + m.g[15][0], (size_t)(m.g[15][1] - m.g[15][0]));
            sb_add(&out, "</del>");
        } else if (m.g[16][0] >= 0 && m.g[16][0] == best) {
            char *inner = strndup(text + m.g[17][0], (size_t)(m.g[17][1] - m.g[17][0]));
            char *rendered = render_inline(inner, base_dir);
            free(inner);
            sb_add(&out, "<em>");
            sb_add(&out, rendered);
            sb_add(&out, "</em>");
            free(rendered);
        } else if (m.g[18][0] >= 0 && m.g[18][0] == best) {
            char *inner = strndup(text + m.g[19][0], (size_t)(m.g[19][1] - m.g[19][0]));
            char *rendered = render_inline(inner, base_dir);
            free(inner);
            sb_add(&out, "<em>");
            sb_add(&out, rendered);
            sb_add(&out, "</em>");
            free(rendered);
        }
        pos = (size_t)best_end;
    }
    return out.data ? out.data : strdup("");
}

/* ------------------------------------------------------------ slug */

static void slug_of(const char *title, SB *out) {
    for (const char *p = title; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (isalnum(ch) || ch >= 0x80) {
            char c[2] = {(char)tolower(ch), 0};
            sb_add(out, c);
        } else if (out->len && out->data[out->len - 1] != '-') {
            sb_add(out, "-");
        }
    }
    if (!out->len)
        sb_add(out, "section");
}

/* ------------------------------------------------------------ fragment */

typedef struct {
    char *level, *title;
} Heading;

char *render_fragment(const char *text, const char *base_dir) {
    /* split lines */
    size_t nlines = 0;
    const char *p = text, *e;
    char **lines = NULL;
    while (*p) {
        e = strchr(p, '\n');
        if (!e)
            e = p + strlen(p);
        lines = realloc(lines, (nlines + 1) * sizeof(char *));
        lines[nlines++] = strndup(p, (size_t)(e - p));
        p = *e ? e + 1 : e;
    }
    if (!nlines) {
        lines = realloc(lines, sizeof(char *));
        lines[nlines++] = strdup("");
    }

    SB out; sb_init(&out);
    Heading *headings = NULL;
    size_t nh = 0;

    bool in_code = false, in_quote = false, in_table = false;
    bool list_ordered = false;
    size_t n_list = 0, cap_list = 0;
    ListItem *list_items = NULL;
    char code_lang[64] = "";
    StrVec code_buf = {0}, quote_buf = {0}, table_buf = {0};

    for (size_t i = 0; i < nlines; i++) {
        char *raw = lines[i];
        char *stripped = raw;
        while (*stripped == ' ' || *stripped == '\t')
            stripped++;
        size_t slen = strlen(stripped);
        while (slen && (stripped[slen - 1] == ' ' || stripped[slen - 1] == '\t'))
            stripped[--slen] = '\0';

        if (in_code) {
            if (strncmp(stripped, "```", 3) == 0) {
                if (code_buf.n) {
                    sb_add(&out, "<pre><code");
                    if (code_lang[0]) {
                        sb_add(&out, " class=\"language-");
                        eb_add_esc(&out, code_lang);
                        sb_add(&out, "\"");
                    }
                    sb_add(&out, ">");
                    for (size_t k = 0; k < code_buf.n; k++) {
                        if (k)
                            sb_add(&out, "\n");
                        eb_add_esc(&out, code_buf.rows[k]);
                    }
                    sb_add(&out, "</code></pre>");
                }
                sv_clear(&code_buf);
                in_code = false;
                continue;
            }
            sv_push(&code_buf, raw);
            continue;
        }

        if (strncmp(stripped, "```", 3) == 0) {
            in_code = true;
            const char *lang = stripped + 3;
            while (*lang == ' ')
                lang++;
            strncpy(code_lang, lang, sizeof(code_lang) - 1);
            code_lang[sizeof(code_lang) - 1] = '\0';
            if (strncasecmp(code_lang, "css-export", 10) == 0 ||
                strncasecmp(code_lang, "css", 3) == 0)
                strcpy(code_lang, "css-export");
            continue;
        }

        /* horizontal rule */
        {
            size_t l = strlen(stripped);
            if (l >= 3 && ((stripped[0] == '-' && strspn(stripped, "-") == l) ||
                           (stripped[0] == '*' && strspn(stripped, "*") == l) ||
                           (stripped[0] == '_' && strspn(stripped, "_") == l))) {
                sab_flush_table(&out, &table_buf, base_dir);
                sv_clear(&table_buf);
                in_table = false;
                sab_flush_quote(&out, &quote_buf, base_dir);
                sv_clear(&quote_buf);
                in_quote = false;
                sab_flush_list(&out, list_items, n_list, list_ordered, base_dir);
                free(list_items);
                list_items = NULL;
                n_list = cap_list = 0;
                sb_add(&out, "<hr>");
                continue;
            }
        }

        /* headings */
        if (stripped[0] == '#') {
            size_t level = 0;
            while (stripped[level] == '#')
                level++;
            if (level <= 6 && stripped[level] == ' ') {
                char *title = stripped + level;
                while (*title == ' ')
                    title++;
                sab_flush_list(&out, list_items, n_list, list_ordered, base_dir);
                free(list_items);
                list_items = NULL;
                n_list = cap_list = 0;
                char *hlevel = itoa_c((int)level);
                headings = realloc(headings, (nh + 1) * sizeof(Heading));
                headings[nh].level = hlevel;
                headings[nh].title = strdup(title);
                nh++;
                sb_add(&out, "<h");
                sb_add(&out, hlevel);
                sb_add(&out, " id=\"");
                SB slug; sb_init(&slug);
                slug_of(title, &slug);
                sb_add(&out, slug.data);
                sb_free(&slug);
                sb_add(&out, "\">");
                char *inl = render_inline(title, base_dir);
                sb_add(&out, inl);
                free(inl);
                sb_add(&out, "</h");
                sb_add(&out, hlevel);
                sb_add(&out, ">");
                continue;
            }
        }

        /* blockquote */
        if (stripped[0] == '>') {
            sab_flush_list(&out, list_items, n_list, list_ordered, base_dir);
            free(list_items);
            list_items = NULL;
            n_list = cap_list = 0;
            sab_flush_table(&out, &table_buf, base_dir);
            sv_clear(&table_buf);
            in_table = false;
            in_quote = true;
            const char *q = stripped + 1;
            while (*q == '>' || *q == ' ')
                q++;
            sv_push(&quote_buf, q);
            continue;
        }
        if (in_quote && stripped[0] != '\0') {
            sv_push(&quote_buf, stripped);
            continue;
        }

        /* table */
        if (!in_table && stripped[0] == '|') {
            const char *nxt = (i + 1 < nlines) ? lines[i + 1] : "";
            while (*nxt == ' ')
                nxt++;
            if (*nxt == '|' && strstr(nxt, "--")) {
                in_table = true;
                sv_clear(&table_buf);
                sv_push(&table_buf, stripped);
                continue;
            }
        }
        if (in_table && stripped[0] == '|') {
            if (strstr(stripped, "--")) {
                continue; /* separator row */
            }
            sv_push(&table_buf, stripped);
            continue;
        }
        if (in_table) {
            sab_flush_table(&out, &table_buf, base_dir);
            sv_clear(&table_buf);
            in_table = false;
        }

        /* list items */
        {
            size_t lead = 0;
            while (raw[lead] == ' ')
                lead++;
            const char *rest = raw + lead;
            bool ordered = false;
            bool is_list = false;
            const char *content = rest;
            if (rest[0] == '-' || rest[0] == '*' || rest[0] == '+') {
                is_list = (rest[1] == ' ');
                content = rest + 1;
            } else if (isdigit((unsigned char)rest[0])) {
                size_t j = 0;
                while (isdigit((unsigned char)rest[j]))
                    j++;
                is_list = (rest[j] == '.' || rest[j] == ')') && rest[j + 1] == ' ';
                if (is_list) {
                    ordered = true;
                    content = rest + j + 1;
                }
            }
            if (is_list) {
                while (*content == ' ')
                    content++;
                const char *checkbox = NULL;
                if (content[0] == '[' && (content[1] == 'x' || content[1] == 'X' ||
                                          content[1] == ' ')) {
                    checkbox = (content[1] == 'x' || content[1] == 'X') ? "checked" : "";
                    content += 2;
                    while (*content == ' ')
                        content++;
                }
                if (!n_list || list_ordered != ordered) {
                    sab_flush_list(&out, list_items, n_list, list_ordered, base_dir);
                    free(list_items);
                    list_items = NULL;
                    n_list = cap_list = 0;
                    list_ordered = ordered;
                }
                sab_flush_quote(&out, &quote_buf, base_dir);
                sv_clear(&quote_buf);
                in_quote = false;
                if (n_list >= cap_list) {
                    cap_list = cap_list ? cap_list * 2 : 8;
                    list_items = realloc(list_items, cap_list * sizeof(ListItem));
                }
                list_items[n_list].indent = lead;
                list_items[n_list].checkbox = checkbox;
                list_items[n_list].content = content;
                n_list++;
                continue;
            }
        }
        if (n_list) {
            sab_flush_list(&out, list_items, n_list, list_ordered, base_dir);
            free(list_items);
            list_items = NULL;
            n_list = cap_list = 0;
        }

        /* blank or paragraph */
        if (stripped[0] == '\0') {
            if (in_quote) {
                sab_flush_quote(&out, &quote_buf, base_dir);
                sv_clear(&quote_buf);
                in_quote = false;
            }
            if (in_table) {
                sab_flush_table(&out, &table_buf, base_dir);
                sv_clear(&table_buf);
                in_table = false;
            }
            continue;
        }
        sb_add(&out, "<p>");
        char *inl = render_inline(stripped, base_dir);
        sb_add(&out, inl);
        free(inl);
        sb_add(&out, "</p>");
    }

    if (in_code) {
        sb_add(&out, "<pre><code");
        if (code_lang[0]) {
            sb_add(&out, " class=\"language-");
            eb_add_esc(&out, code_lang);
            sb_add(&out, "\"");
        }
        sb_add(&out, ">");
        for (size_t k = 0; k < code_buf.n; k++) {
            if (k)
                sb_add(&out, "\n");
            eb_add_esc(&out, code_buf.rows[k]);
        }
        sb_add(&out, "</code></pre>");
        sv_clear(&code_buf);
    }
    sab_flush_list(&out, list_items, n_list, list_ordered, base_dir);
    free(list_items);
    sab_flush_quote(&out, &quote_buf, base_dir);
    sv_clear(&quote_buf);
    sab_flush_table(&out, &table_buf, base_dir);
    sv_clear(&table_buf);

    /* TOC */
    if (nh) {
        SB toc; sb_init(&toc);
        sb_add(&toc, "<nav class=\"toc\" id=\"toc\"><h2>Table of Contents</h2><ol>");
        for (size_t k = 0; k < nh; k++) {
            sb_add(&toc, "<li class=\"toc-h");
            sb_add(&toc, headings[k].level);
            sb_add(&toc, "\"><a href=\"#");
            SB slug; sb_init(&slug);
            slug_of(headings[k].title, &slug);
            sb_add(&toc, slug.data);
            sb_free(&slug);
            sb_add(&toc, "\">");
            eb_add_esc(&toc, headings[k].title);
            sb_add(&toc, "</a></li>");
        }
        sb_add(&toc, "</ol></nav>");
        char *toc_html = toc.data;
        if (strstr(out.data, "[[TOC]]")) {
            char *pos = strstr(out.data, "[[TOC]]");
            SB merged; sb_init(&merged);
            sb_addn(&merged, out.data, (size_t)(pos - out.data));
            sb_add(&merged, toc_html);
            sb_add(&merged, pos + 7);
            sb_free(&out);
            out = merged;
        } else {
            SB merged; sb_init(&merged);
            sb_add(&merged, toc_html);
            sb_add(&merged, "\n");
            sb_add(&merged, out.data);
            sb_free(&out);
            out = merged;
        }
        sb_free(&toc);
    }

    for (size_t k = 0; k < nh; k++) {
        free(headings[k].level);
        free(headings[k].title);
    }
    free(headings);
    for (size_t k = 0; k < nlines; k++)
        free(lines[k]);
    free(lines);
    return out.data ? out.data : strdup("");
}

/* ------------------------------------------------------------ plain */

char *render_plain(const char *text) {
    size_t nlines = 0;
    const char *p = text, *e;
    char **lines = NULL;
    while (*p) {
        e = strchr(p, '\n');
        if (!e)
            e = p + strlen(p);
        lines = realloc(lines, (nlines + 1) * sizeof(char *));
        lines[nlines++] = strndup(p, (size_t)(e - p));
        p = *e ? e + 1 : e;
    }
    if (!nlines) {
        lines = realloc(lines, sizeof(char *));
        lines[nlines++] = strdup("");
    }
    const char *RULES_TOP = "\n════════════════════════════════════════════════════════════";
    const char *RULES_BOT = "════════════════════════════════════════════════════════════";
    const char *CODE_OPEN = "┌─ code ─────────────────────────────";
    const char *CODE_CLOSE = "└─ end code ────────────────────────";
    const char *RULE_DASH = "────────────────────────────────────────────────────────────";

    SB out; sb_init(&out);
    bool in_code = false;
    for (size_t i = 0; i < nlines; i++) {
        char *s = lines[i];
        while (*s == ' ' || *s == '\t')
            s++;
        if (strncmp(s, "```", 3) == 0) {
            sb_add(&out, in_code ? CODE_CLOSE : CODE_OPEN);
            in_code = !in_code;
            continue;
        }
        if (in_code) {
            sb_add(&out, "    ");
            sb_add(&out, lines[i]);
            continue;
        }
        size_t level = 0;
        while (s[level] == '#')
            level++;
        if (level >= 1 && level <= 6 && s[level] == ' ') {
            char *t = s + level;
            while (*t == ' ')
                t++;
            char *up = strdup(t);
            for (char *q = up; *q; q++)
                *q = (char)toupper((unsigned char)*q);
            if (level == 1) {
                sb_add(&out, "\n");
                sb_add(&out, RULES_TOP);
                sb_add(&out, "\n");
                sb_add(&out, up);
                sb_add(&out, "\n");
                sb_add(&out, RULES_BOT);
            } else {
                sb_add(&out, up);
            }
            free(up);
            continue;
        }
        {
            size_t l = strlen(s);
            if (l >= 3 && ((s[0] == '-' && strspn(s, "-") == l) ||
                           (s[0] == '*' && strspn(s, "*") == l))) {
                sb_add(&out, RULE_DASH);
                continue;
            }
        }
        {
            bool is_li = false;
            const char *content = s;
            bool checked_marker = false;
            const char *box = NULL;
            if ((s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ')
                is_li = true, content = s + 1;
            else if (isdigit((unsigned char)s[0])) {
                size_t j = 0;
                while (isdigit((unsigned char)s[j]))
                    j++;
                if ((s[j] == '.' || s[j] == ')') && s[j + 1] == ' ')
                    is_li = true, content = s + j + 1;
            }
            if (is_li) {
                while (*content == ' ')
                    content++;
                if (content[0] == '[' && (content[1] == 'x' || content[1] == 'X' ||
                                          content[1] == ' ')) {
                    checked_marker = (content[1] == 'x' || content[1] == 'X');
                    box = checked_marker ? "[x]" : "[ ]";
                    content += 2;
                    while (*content == ' ')
                        content++;
                }
                sb_add(&out, "  ");
                sb_add(&out, box ? box : "•");
                sb_add(&out, " ");
                sb_add(&out, content);
                continue;
            }
        }
        if (s[0] == '>') {
            sb_add(&out, "  ▌ ");
            const char *q = s + 1;
            while (*q == '>')
                q++;
            while (*q == ' ')
                q++;
            sb_add(&out, q);
            continue;
        }
        if (*s) {
            char *plain = strdup(s);
            /* strip markup */
            char *dst = plain;
            for (char *q = plain; *q;) {
                if ((q[0] == '*' && q[1] == '*') || (q[0] == '~' && q[1] == '~')) {
                    q += 2;
                    continue;
                }
                if (*q == '`' || *q == '$') {
                    q++;
                    continue;
                }
                if (q[0] == '[' && q[1] == '[') {
                    q += 2;
                    continue;
                }
                if (q[0] == ']' && q[1] == ']') {
                    q += 2;
                    continue;
                }
                *dst++ = *q++;
            }
            *dst = '\0';
            sb_add(&out, plain);
            free(plain);
        } else {
            sb_add(&out, "");
        }
    }
    for (size_t k = 0; k < nlines; k++)
        free(lines[k]);
    free(lines);
    return out.data ? out.data : strdup("");
}

/* ------------------------------------------------------------ themes */

typedef struct {
    const char *bg, *fg, *code_bg, *border, *accent, *toc_bg;
} Theme;

static const Theme FACE_THEMES[] = {
    {"#ffffff", "#1f2328", "#f3f4f6", "#d8dee4", "#0969da", "#f6f8fa"},
    {"#0d1117", "#e6edf3", "#161b22", "#30363d", "#4493f8", "#161b22"},
};

static void build_style(SB *out, const char *theme, const char *custom_css) {
    const Theme *t = &FACE_THEMES[0];
    if (theme && strcmp(theme, "dark") == 0)
        t = &FACE_THEMES[1];
    char buf[1024];
    snprintf(buf, sizeof(buf),
             "body { background: %s; color: %s;\n"
             "       font-family: system-ui, -apple-system, 'Segoe UI', sans-serif;\n"
             "       max-width: 860px; margin: 0 auto; padding: 1.5em; line-height: 1.55; }\n"
             "h1, h2, h3, h4, h5, h6 { border-bottom: 1px solid %s; padding-bottom: .2em; }\n"
             "a { color: %s; }\n"
             "code, pre { background: %s; border-radius: 6px; }\n"
             "code { padding: .15em .35em; }\n"
             "pre { padding: .8em 1em; overflow-x: auto; }\n"
             "pre code { background: none; padding: 0; }\n"
             "blockquote { border-left: 4px solid %s; margin-left: 0; padding-left: 1em;\n"
             "             color: %s; opacity: .85; }\n"
             "table { border-collapse: collapse; margin: 1em 0; }\n"
             "th, td { border: 1px solid %s; padding: .35em .7em; }\n"
             "th { background: %s; }\n"
             ".math { font-family: 'STIX Two Math', 'Cambria Math', serif; }\n"
             "nav.toc { background: %s; border: 1px solid %s;\n"
             "          border-radius: 8px; padding: .6em 1.2em; margin: 1em 0; }\n"
             "nav.toc ol { margin: 0; padding-left: 1.4em; }\n"
             "li:has(> input[type=\"checkbox\"]) { list-style: none; margin-left: -1.2em; }\n"
             "img { max-width: 100%%; }\n",
             t->bg, t->fg, t->border, t->accent, t->code_bg, t->border, t->fg,
             t->border, t->code_bg, t->toc_bg, t->border);
    sb_add(out, buf);
    if (custom_css && *custom_css) {
        sb_add(out, "\n/* injected custom css */\n");
        char *clean = render_sanitize_css(custom_css);
        sb_add(out, clean);
        free(clean);
    }
}

char *render_sanitize_css(const char *css) {
    if (!css || !*css)
        return strdup("");
    size_t n = strlen(css);
    if (n > 8192)
        n = 8192;
    char *lower = strndup(css, n);
    for (size_t i = 0; i < n; i++)
        lower[i] = (char)tolower((unsigned char)lower[i]);
    bool bad = strstr(lower, "url(") || strstr(lower, "expression") ||
               strchr(lower, '<') || strchr(lower, '>');
    free(lower);
    if (bad)
        return strdup("");
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)css[i];
        bool ok = isalnum(c) || c == ' ' || c == '#' || c == '%' || c == '.' ||
                  c == ',' || c == ':' || c == ';' || c == '_' || c == '-' ||
                  c == '/' || c == '(' || c == ')' || c == '*' || c == '"' ||
                  c == '\'' || c == '[' || c == ']' || c == '{' || c == '}';
        if (!ok)
            return strdup("");
    }
    return strndup(css, n);
}

char *render_page(const char *text, const char *title, const char *theme,
                  const char *custom_css) {
    char *body = render_fragment(text, NULL);
    SB out; sb_init(&out);
    sb_add(&out, "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n");
    sb_add(&out, "<meta charset=\"utf-8\">\n<title>");
    eb_add_esc(&out, title);
    sb_add(&out, "</title>\n<style>\n");
    build_style(&out, theme, custom_css);
    sb_add(&out, "\n</style>\n</head>\n<body>\n");
    sb_add(&out, body);
    free(body);
    sb_add(&out, "\n</body>\n</html>\n");
    return out.data ? out.data : strdup("");
}

char *render_doc_title(const char *text) {
    if (strncmp(text, "# ", 2) == 0) {
        const char *start = text + 2;
        while (*start == ' ')
            start++;
        const char *end = start;
        while (*end && *end != '\n')
            end++;
        SB out; sb_init(&out);
        char *line = strndup(start, (size_t)(end - start));
        eb_add_esc(&out, line);
        free(line);
        while (out.len && out.data[out.len - 1] == ' ')
            out.data[--out.len] = '\0';
        return out.data ? out.data : strdup("FastNote");
    }
    const char *p = strstr(text, "\n# ");
    if (p) {
        const char *start = p + 3;
        while (*start == ' ')
            start++;
        const char *end = start;
        while (*end && *end != '\n')
            end++;
        SB out; sb_init(&out);
        char *line = strndup(start, (size_t)(end - start));
        eb_add_esc(&out, line);
        free(line);
        while (out.len && out.data[out.len - 1] == ' ')
            out.data[--out.len] = '\0';
        return out.data ? out.data : strdup("FastNote");
    }
    return strdup("FastNote");
}

double render_measure_large_document(void) {
    SB big; sb_init(&big);
    for (int i = 0; i < 1000; i++) {
        char h[128];
        snprintf(h, sizeof(h),
                 "# Heading %d\nSome **body** text with *italics* and `code` and $x^2$.\n\n",
                 i);
        sb_add(&big, h);
    }
    clock_t t0 = clock();
    char *frag = render_fragment(big.data, NULL);
    clock_t t1 = clock();
    free(frag);
    sb_free(&big);
    return (double)(t1 - t0) / CLOCKS_PER_SEC;
}