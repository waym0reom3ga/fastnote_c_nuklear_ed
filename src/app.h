/* FastNote C editions — document model.
 *
 * The actions in actions.c are the ONLY place the application's behaviour
 * is implemented.  The GUI callbacks and the headless CLI both call these
 * same functions (specification 5.2, shared-path rule), so a button cannot
 * rot while the CLI still works.
 */

#ifndef FASTNOTE_APP_H
#define FASTNOTE_APP_H

#include <stdbool.h>
#include <stddef.h>

#define APP_EDITOR_NAME "FastNote"
#define APP_VERSION "1.1.0"

typedef struct {
    char *path;   /* NULL or heap-allocated path */
    char *text;   /* heap-allocated */
    bool dirty;
} Document;

typedef struct {
    Document doc;
    char *notes_dir; /* heap-allocated absolute path */
    char *event_file; /* heap-allocated; phase-marker file (spec 5.1), or NULL */
    bool saved_once;
} AppState;

/* Open a document. Returns an error message (static/alloc), or NULL. */
char *doc_open(Document *d, const char *path);
void     doc_set_text(Document *d, const char *text);
void     doc_insert_text(Document *d, const char *text);
char *doc_save(Document *d);
char *doc_save_as(Document *d, const char *path);
void     doc_free(Document *d);

AppState *app_state_new(const char *notes_dir);
void     app_state_free_fields(AppState *s); /* free embedded fields only */
void     app_state_free(AppState *s);       /* free fields + the struct itself */

/* err helpers: actions return NULL on success or a static error string. */
const char *fn_error(void);
void        fn_set_error(const char *fmt, ...);

/* Append a phase marker to the event file, if one was requested (spec 5.1).
 * A reporting outlet only — never drives or simulates an operation. */
void fn_event(AppState *s, const char *marker);

#endif /* FASTNOTE_APP_H */