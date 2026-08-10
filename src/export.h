/* FastNote C editions — export writers (HTML FR-7, PDF FR-8). */

#ifndef FASTNOTE_EXPORT_H
#define FASTNOTE_EXPORT_H

char *write_pdf_export(const char *md_text, const char *path);
char *write_html_export(const char *md_text, const char *title,
                        const char *theme, const char *custom_css,
                        const char *path);

#endif /* FASTNOTE_EXPORT_H */