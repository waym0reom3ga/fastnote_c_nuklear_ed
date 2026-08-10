/* FastNote C editions — markdown renderer, shared by preview and exports.
 *
 * Pure C, no external dependencies.  Implements the feature table of
 * FASTNOTE_SPECIFICATION.md section 4.  All source text is HTML-escaped
 * before any inline formatting is applied, so embedded <script> cannot
 * execute.
 */

#ifndef FASTNOTE_RENDERER_H
#define FASTNOTE_RENDERER_H

/* Renders markdown to an HTML fragment.  Caller frees the result. */
char *render_fragment(const char *text, const char *base_dir);
/* Renders the pseudo-plain preview text.  Caller frees the result. */
char *render_plain(const char *text);
/* Renders a full standalone HTML page (FR-7).  Caller frees the result. */
char *render_page(const char *text, const char *title, const char *theme,
                  const char *custom_css);
/* First H1 line, HTML-escaped.  Caller frees the result. */
char *render_doc_title(const char *text);
/* CSS sanitizer: returns NULL when the css is clean (injected as-is),
 * or an empty string when it must be dropped. Caller frees the result. */
char *render_sanitize_css(const char *css);
/* Synthesizes the spec worst case (~1000 headings) and times it. */
double render_measure_large_document(void);

#endif /* FASTNOTE_RENDERER_H */