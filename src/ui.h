/* FastNote c_nuklear — UI state shared by the GUI (app_ui.c) and the
 * headless pointer-event tests (test_ui.c).
 */

#ifndef FASTNOTE_UI_H
#define FASTNOTE_UI_H

#include <stdbool.h>

/* Forward declaration for nuklear context (header not included here) */
struct nk_context;

/* Headless context helpers (used by test_ui.c) — the GLFW GUI uses
 * nk_glfw3_init() via the nuklear demo helper header instead. */
struct nk_context *ui_nk_new(void);
void ui_nk_free(struct nk_context *ctx);

#include "app.h"
#include "file_browser.h"

#define UI_CONTROL_ID_MAX 24

/* FR-11 accelerators, as delivered to ui_run_frame via pending_accel. */
enum {
    ACCEL_NONE = 0,
    ACCEL_OPEN,        /* Ctrl+O */
    ACCEL_SAVE,        /* Ctrl+S */
    ACCEL_SAVE_AS,     /* Ctrl+Shift+S */
    ACCEL_EXPORT,      /* Ctrl+E */
    ACCEL_EXPORT_PDF,  /* Ctrl+Shift+E */
    ACCEL_FOCUS_PATH,  /* Ctrl+L */
    ACCEL_CONFIRM,     /* Enter in the browser (spec 3.2) */
    ACCEL_CANCEL,      /* Escape in the browser (spec 3.2) */
};

/* Forward-declare so UIControl can reference it */
typedef struct UIState UIState;

typedef struct {
    void (*handler)(UIState *ui);
    int x, y, w, h; /* last-frame bounds, as computed by nuklear */
} UIControl;

typedef struct {
    int x, y, w, h;
} UIRect;

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
    UIRect ok_rect, cancel_rect;

    /* Keyboard-first input: one accelerator applied at the start of the next
     * frame; focus requests activate the editor / browser path field with a
     * real pointer press so typing lands (spec FR-11, 3.2). */
    int pending_accel;
    bool editor_focus_pending;
    bool path_focus_pending;
    bool release_pending;   /* pointer-up for the injected focus press */
    int release_x, release_y;
    UIRect editor_rect;     /* last-frame bounds of the edit widget */
    UIRect path_rect;       /* last-frame bounds of the browser path field */

    /* FR-9 close handling. */
    bool close_prompt_open;
    bool closing;           /* user confirmed save/discard; allow the close */
} UIState;

UIState *ui_state_new(const char *notes_dir);
void ui_state_free(UIState *ui);
void ui_set_status(UIState *ui, const char *msg);
void ui_rebuild_preview(UIState *ui);

/* Draws one frame; processes clicks that arrived in ctx->input since the
 * last frame.  headless-safe (no GLFW, no GL). */
void ui_run_frame(UIState *ui, struct nk_context *ctx);

/* Call once per frame BEFORE ui_run_frame: activates the editor / browser
 * path field with a real pointer press when a focus request is pending. */
void ui_pump_focus(UIState *ui, struct nk_context *ctx);

/* FR-9: intercept a window close request.  Returns true when the close was
 * blocked (dirty document -> the prompt window is shown instead). */
bool ui_request_close(UIState *ui);

/* The click handlers, named on_* so the acceptance harness can see the
 * binding (A6/A11). */
void on_open(UIState *ui);
void on_save(UIState *ui);
void on_save_as(UIState *ui);
void on_insert(UIState *ui);
void on_export(UIState *ui);
void on_export_pdf(UIState *ui);
void on_theme(UIState *ui);

#endif /* FASTNOTE_UI_H */