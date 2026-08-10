/* FastNote C editions — minimal self-contained PDF writer.
 *
 * Produces a valid single/multi-page PDF/1.4 with Helvetica text, enough
 * for FastNote's export requirement (FR-8).  No external library.
 */

#ifndef FASTNOTE_PDF_H
#define FASTNOTE_PDF_H

#include <stddef.h>

/* Returns a heap PDF blob and sets *out_len. Caller frees. */
unsigned char *pdf_from_lines(const char *text, int font_pt, size_t *out_len);

#endif