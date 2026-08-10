/* FastNote c_nuklear — UI state shared by the GUI (app_ui.c) and the
 * headless pointer-event tests (test_ui.c).
 *
 * The whole interface is drawn by ui_run_frame() with Nuklear, whose input
 * state is fed by real pointer events.  The GUI (nk_glfw3 + GLFW) and the
 * tests (bare nk_context, no window) both drive the very same frame
 * function, so a click that works headless works in the window.
 */

#ifndef FASTNOTE_UI_H
#define FASTNOTE_UI_H

#include <stdbool.h>

#include "app.h"
#include "file_browser.h"

#define UI_CONTROL_ID_MAX 24

typedef struct {
    void (*handler)(struct UIState *ui);
    int x, y, w, h; /* last-frame bounds, as computed by nuklear */
} UIControl;

typedef struct {
    char *name; /* borrowed from FileBrowser.entries */
    int x, y, w, h;
} UIEntryRect;

typedef struct UIState {
    AppState app;
    FileBrowser browser;
    bool browser_open;
    bool theme_dark;
    bool pending_save;      /* browser OK -> doc save */
    bool pending_export;    /* browser OK -> export (NULL = none) */
    bool pending_export_pdf;
    char *status_text;
    char *preview_text;
    char *editor_text;
    size_t editor_cap;
    char browser_path[1024];
    int width, height;
    UIControl toolbar[8];
    int n_toolbar;
    UIEntryRect *entry_rects;
    int n_entry_rects;
} UIState;

UIState *ui_state_new(const char *notes_dir);
void ui_state_free(UIState *ui);
void ui_set_status(UIState *ui, const char *msg);
void ui_rebuild_preview(UIState *ui);

/* Draws one frame; processes clicks that arrived in ctx->input since the
 * last frame.  headless-safe (no GLFW, no GL). */
void ui_run_frame(UIState *ui, struct nk_context *ctx);

/* The click handlers, named on_* so the acceptance harness can see the
 * binding (A6/A11). */
void on_open(UIState *ui);
void on_save(UIState *ui);
void on_insert(UIState *ui);
void on_export(UIState *ui);
void on_theme(UIState *ui);

#endif /* FASTNOTE_UI_H */