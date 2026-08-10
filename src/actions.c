/* FastNote C editions — shared action layer (spec 5.2 shared-path rule).
 *
 * The actions here are the ONLY place the application's behaviour is
 * implemented.  The GUI callbacks and the headless CLI (main.c) both call
 * these same functions, so a button cannot rot while the CLI still works.
 */

#include "app.h"
#include "export.h"
#include "renderer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *strip_err(const char *msg) {
    if (!msg)
        return "";
    if (strstr(msg, "No such file"))
        return "no such file";
    if (strstr(msg, "Permission denied"))
        return "permission denied";
    return msg;
}

/* actionOpen: open a document file (FR-2).  NULL on success. */
char *actionOpen(AppState *s, const char *path) {
    char *err = doc_open(&s->doc, path);
    if (err)
        fn_set_error("cannot open %s: %s", path, strip_err(err));
    return err;
}

/* actionInsert: append text at the document end (FR-3; --insert seam). */
void actionInsert(AppState *s, const char *text) {
    doc_insert_text(&s->doc, text);
}

/* actionSave: write the document (FR-5).  NULL on success. */
char *actionSave(AppState *s) {
    char *err = doc_save(&s->doc);
    if (!err)
        s->saved_once = true;
    return err;
}

/* actionSaveAs: save under a new path (FR-6).  NULL on success. */
char *actionSaveAs(AppState *s, const char *path) {
    char *err = doc_save_as(&s->doc, path);
    if (!err)
        s->saved_once = true;
    return err;
}

/* actionExportHTML / actionExportPDF: FR-7/8.  NULL on success. */
char *actionExportHTML(AppState *s, const char *path, const char *theme) {
    char *title = render_doc_title(s->doc.text);
    char *css = render_sanitize_css(NULL);
    char *err = write_html_export(s->doc.text, title ? title : "FastNote",
                                  theme ? theme : "light", css ? css : "",
                                  path);
    free(title);
    free(css);
    if (err)
        fn_set_error("%s", err);
    return err;
}

char *actionExportPDF(AppState *s, const char *path) {
    char *err = write_pdf_export(s->doc.text, path);
    if (err)
        fn_set_error("%s", err);
    return err;
}

/* RunCLIActions executes the headless seam in the mandated order (spec 5.1):
 * --open, --insert, --save, --export.  NULL on success. */
char *RunCLIActions(AppState *s, const char *open_path, const char *insert,
                    bool do_save, const char *export_path) {
    char *err;
    if (open_path && *open_path) {
        if ((err = actionOpen(s, open_path)))
            return err;
    }
    if (insert && *insert)
        actionInsert(s, insert);
    if (do_save) {
        if ((err = actionSave(s)))
            return err;
    }
    if (export_path && *export_path) {
        size_t n = strlen(export_path);
        if (n >= 4 && strcasecmp(export_path + n - 4, ".pdf") == 0)
            err = actionExportPDF(s, export_path);
        else
            err = actionExportHTML(s, export_path, "light");
        if (err)
            return err;
    }
    return NULL;
}