/* FastNote C editions — in-app file browser (spec §3).
 * Toolkit-independent state machine; the GUI renders this state and
 * routes pointer events through the same handlers.  No native dialog is
 * involved anywhere (spec 3.1).
 */

#ifndef FASTNOTE_BROWSER_H
#define FASTNOTE_BROWSER_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *name;
    bool is_dir;
} BrowserEntry;

typedef struct {
    char *mode;       /* "open" | "save" */
    char *cwd;
    bool show_all;
    char *path_input;
    char *selected;
    BrowserEntry *entries;
    size_t n_entries;
} FileBrowser;

/* Returns error string or NULL on success. */
char *browser_new(FileBrowser *b, const char *mode, const char *start_dir);
char *browser_refresh(FileBrowser *b);
/* Activate an entry: enter dir (returns NULL) or return selected path. */
char *browser_activate(FileBrowser *b, const char *name);
char *browser_parent(FileBrowser *b);
char *browser_toggle_filter(FileBrowser *b);
/* Result: absolute path to commit, or error string. */
char *browser_result(FileBrowser *b);
void  browser_show_all(FileBrowser *b);
void  browser_free(FileBrowser *b);

#endif /* FASTNOTE_BROWSER_H */