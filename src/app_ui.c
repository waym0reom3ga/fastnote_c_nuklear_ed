/* FastNote c_nuklear — the whole interface, drawn with Nuklear.
 *
 * ui_run_frame() is toolkit-complete: toolbar, editor pane, preview pane,
 * in-app file browser (spec 3.1: no native dialog anywhere), status bar.
 * Both the GLFW GUI (main.c) and the headless pointer tests (test_ui.c)
 * drive this same single function with the same nuklear context, so a
 * click that works headless is exactly the click the window sees.
 */

#include "ui.h"

#include "renderer.h"

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#ifndef NK_ASSERT
#define NK_ASSERT(x) ((void)0) /* headless tests don't need nuklear seq checks */
#endif
#define NK_IMPLEMENTATION
#include "nuklear.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Extern declarations for action functions defined in actions.c */
char *actionOpen(AppState *s, const char *path);
void actionInsert(AppState *s, const char *text);
char *actionSave(AppState *s);
char *actionSaveAs(AppState *s, const char *path);
char *actionExportHTML(AppState *s, const char *path, const char *theme);
char *actionExportPDF(AppState *s, const char *path);

/* forward declarations */
static void ui_toolbar_labels(UIState *ui);
static void ui_browser_after_activate(UIState *ui);
static void ui_commit_path(UIState *ui, const char *path);
static UIControl ui_widget_bounds(UIControl c, struct nk_context *ctx);

/* ------------------------------------------------------------ context (headless) */
/* Creates a bare nuklear context with baked default font.  Used by the
 * headless UI test binary (test_ui.c).  The GLFW GUI uses nk_glfw3_init()
 * instead via the nuklear demo helper header. */
struct nk_context *ui_nk_new(void) {
    struct nk_context *ctx = malloc(sizeof(struct nk_context));
    if (!ctx)
        return NULL;
    memset(ctx, 0, sizeof(*ctx));
    if (nk_init_default(ctx, NULL) == 0) {
        free(ctx);
        return NULL;
    }
    struct nk_font_atlas atlas_storage;
    memset(&atlas_storage, 0, sizeof(atlas_storage));
    struct nk_font_atlas *atlas = &atlas_storage;
    nk_font_atlas_init_default(atlas);
    int w = 0, h = 0;
    nk_font_atlas_begin(atlas);
    const void *pixels = nk_font_atlas_bake(atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
    struct nk_draw_null_texture null_tex;
    memset(&null_tex, 0, sizeof(null_tex));
    nk_font_atlas_end(atlas, nk_handle_id(0), &null_tex);
    if (atlas->default_font) {
        nk_style_set_font(ctx, &atlas->default_font->handle);
    }
    (void)pixels; /* not uploaded to GL in headless mode */
    return ctx;
}

void ui_nk_free(struct nk_context *ctx) {
    if (!ctx)
        return;
    nk_free(ctx);
    free(ctx);
}

UIState *ui_state_new(const char *notes_dir) {
    UIState *ui = calloc(1, sizeof(UIState));
    if (!ui)
        return NULL;
    AppState *s = app_state_new(notes_dir);
    if (!s) {
        free(ui);
        return NULL;
    }
    ui->app = *s;
    free(s);
    ui->width = 1024;
    ui->height = 640;
    ui->editor_cap = 1 << 20;
    ui->editor_text = malloc(ui->editor_cap);
    ui->status_text = strdup("Ready");
    ui->preview_text = strdup("");
    if (!ui->editor_text || !ui->status_text || !ui->preview_text) {
        ui_state_free(ui);
        return NULL;
    }
    ui->editor_text[0] = '\0';
    ui_toolbar_labels(ui);
    return ui;
}

void ui_state_free(UIState *ui) {
    if (!ui)
        return;
    app_state_free_fields(&ui->app); /* embedded struct — don't free the pointer */
    browser_free(&ui->browser);
    free(ui->status_text);
    free(ui->preview_text);
    free(ui->entry_rects);
    free(ui->editor_text);
    free(ui);
}

void ui_set_status(UIState *ui, const char *msg) {
    free(ui->status_text);
    ui->status_text = strdup(msg ? msg : "");
}

void ui_rebuild_preview(UIState *ui) {
    char *plain = render_plain(ui->app.doc.text);
    free(ui->preview_text);
    ui->preview_text = plain ? plain : strdup("");
}

/* ------------------------------------------------------------ handlers */

void on_open(UIState *ui) {
    if (browser_new(&ui->browser, "open", ui->app.notes_dir) != NULL) {
        ui_set_status(ui, "cannot browse directory");
        return;
    }
    ui->browser_open = true;
    ui->pending_save = false;
    ui->pending_export = false;
    ui->browser_path[0] = '\0';
    free(ui->browser.path_input);
    ui->browser.path_input = strdup("");
    ui_set_status(ui, "Choose a document to open");
}

void on_save(UIState *ui) {
    if (ui->app.doc.path) {
        char *err = actionSave(&ui->app);
        ui_set_status(ui, err ? "save failed" : "Saved");
        return;
    }
    /* no path yet: save-as through the browser */
    if (browser_new(&ui->browser, "save", ui->app.notes_dir) != NULL) {
        ui_set_status(ui, "cannot browse directory");
        return;
    }
    ui->browser_open = true;
    ui->pending_save = true;
    ui->pending_export = false;
    snprintf(ui->browser_path, sizeof(ui->browser_path), "%s",
             ui->app.doc.path ? ui->app.doc.path : "untitled.md");
    free(ui->browser.path_input);
    ui->browser.path_input = strdup(ui->browser_path);
    ui_set_status(ui, "Choose a location to save");
}

void on_insert(UIState *ui) {
    actionInsert(&ui->app, "\ninserted\n");
    ui_rebuild_preview(ui);
    ui_set_status(ui, "Inserted text");
}

static void ui_prompt_export(UIState *ui, bool pdf) {
    if (browser_new(&ui->browser, "save", ui->app.notes_dir) != NULL) {
        ui_set_status(ui, "cannot browse directory");
        return;
    }
    ui->browser_open = true;
    ui->pending_save = false;
    ui->pending_export = true;
    ui->pending_export_pdf = pdf;
    const char *suggest = "export";
    char *t = render_doc_title(ui->app.doc.text);
    if (t && *t)
        suggest = t;
    snprintf(ui->browser_path, sizeof(ui->browser_path), "%.*s",
             (int)(sizeof(ui->browser_path) - 8), suggest);
    free(t);
    free(ui->browser.path_input);
    ui->browser.path_input = strdup(ui->browser_path);
    ui_set_status(ui, "Choose where to export");
}

void on_export(UIState *ui) { ui_prompt_export(ui, false); }

void on_export_pdf(UIState *ui) { ui_prompt_export(ui, true); }

void on_theme(UIState *ui) {
    ui->theme_dark = !ui->theme_dark;
    ui_set_status(ui, ui->theme_dark ? "Dark theme" : "Light theme");
}

static const char *toolbar_label(const UIControl *c, bool dark) {
    if (c->handler == on_open)
        return "Open";
    if (c->handler == on_save)
        return "Save";
    if (c->handler == on_insert)
        return "Insert";
    if (c->handler == on_export)
        return "Export";
    if (c->handler == on_export_pdf)
        return "Export PDF";
    if (c->handler == on_theme)
        return dark ? "Theme: dark" : "Theme: light";
    return "";
}

static void ui_toolbar_labels(UIState *ui) {
    ui->n_toolbar = 6;
    ui->toolbar[0].handler = on_open;
    ui->toolbar[1].handler = on_save;
    ui->toolbar[2].handler = on_insert;
    ui->toolbar[3].handler = on_export;
    ui->toolbar[4].handler = on_export_pdf;
    ui->toolbar[5].handler = on_theme;
    for (int k = 0; k < ui->n_toolbar; k++)
        ui->toolbar[k].x = ui->toolbar[k].y = ui->toolbar[k].w =
            ui->toolbar[k].h = 0;
}

/* ------------------------------------------------------------ browser */

static void ui_browser_after_activate(UIState *ui) {
    if (ui->browser.selected && *ui->browser.selected) {
        ui_commit_path(ui, ui->browser.selected);
        ui->browser_open = false;
        ui->pending_save = false;
        ui->pending_export = false;
    }
}

static void ui_commit_path(UIState *ui, const char *path) {
    if (ui->pending_save) {
        char *err = actionSaveAs(&ui->app, path);
        ui_set_status(ui, err ? "save failed" : "Saved");
    } else if (ui->pending_export) {
        char *err;
        if (ui->pending_export_pdf)
            err = actionExportPDF(&ui->app, path);
        else
            err = actionExportHTML(&ui->app, path,
                                   ui->theme_dark ? "dark" : "light");
        ui_set_status(ui, err ? "export failed" : "Exported");
    } else {
        char *err = actionOpen(&ui->app, path);
        if (err) {
            ui_set_status(ui, "open failed");
        } else {
            ui_rebuild_preview(ui);
            snprintf(ui->editor_text, ui->editor_cap, "%s",
                     ui->app.doc.text);
            ui_set_status(ui, "Opened");
        }
    }
}

/* ------------------------------------------------------------ frame */

static UIControl ui_widget_bounds(UIControl c, struct nk_context *ctx) {
    struct nk_rect r = nk_widget_bounds(ctx);
    c.x = (int)r.x;
    c.y = (int)r.y;
    c.w = (int)r.w;
    c.h = (int)r.h;
    return c;
}

static UIRect ui_rect_of(UIRect r, struct nk_context *ctx) {
    struct nk_rect b = nk_widget_bounds(ctx);
    r.x = (int)b.x;
    r.y = (int)b.y;
    r.w = (int)b.w;
    r.h = (int)b.h;
    return r;
}

void ui_run_frame(UIState *ui, struct nk_context *ctx) {
    if (nk_begin(ctx, "FastNote", nk_rect(0, 0, (float)ui->width,
                                          (float)ui->height), 0)) {
        /* toolbar: static row so all six buttons fit in one row (a dynamic
         * row overflows by one column width of spacing) and the widget
         * bounds are deterministic for the tests. */
        {
            float panel_w = ctx->current->layout->clip.w;
            int cols = ui->n_toolbar;
            nk_layout_row_static(ctx, 30,
                                 (int)((panel_w - 4.0f * (cols - 1)) / cols),
                                 cols);
        }
        for (int i = 0; i < ui->n_toolbar; i++) {
            /* record the rect BEFORE drawing: nk_widget_bounds peeks the
             * layout cursor, so after-drawing would record the NEXT button */
            ui->toolbar[i] = ui_widget_bounds(ui->toolbar[i], ctx);
            if (nk_button_label(ctx, toolbar_label(&ui->toolbar[i],
                                                   ui->theme_dark)))
                ui->toolbar[i].handler(ui);
        }

        /* editor + preview, side by side */
        nk_layout_row_dynamic(ctx, 22, 1);
        nk_label(ctx, "Editor / Preview", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, (float)(ui->height - 40 - 22 - 24), 2);
        {
            /* The edit widget writes into editor_text; the doc is the model.
             * If the user typed this frame, the doc follows the editor.
             * Otherwise a toolbar handler may have changed the doc, so the
             * editor follows the doc — otherwise handler edits are wiped. */
            char *ed_before = strdup(ui->editor_text);
            int len = (int)strlen(ui->editor_text);
            nk_edit_string(ctx, NK_EDIT_BOX | NK_EDIT_SIG_ENTER,
                           ui->editor_text, &len, (int)ui->editor_cap,
                           nk_filter_default);
            ui->editor_text[len] = '\0';
            if (strcmp(ed_before, ui->editor_text) != 0) {
                doc_set_text(&ui->app.doc, ui->editor_text);
                ui_rebuild_preview(ui);
            } else if (strcmp(ui->editor_text, ui->app.doc.text) != 0) {
                snprintf(ui->editor_text, ui->editor_cap, "%s",
                         ui->app.doc.text);
                ui_rebuild_preview(ui);
            }
            free(ed_before);
        }
        nk_label_wrap(ctx, ui->preview_text);

        /* status bar */
        nk_layout_row_dynamic(ctx, 24, 1);
        nk_label(ctx, ui->status_text, NK_TEXT_LEFT);
    }
    nk_end(ctx);

    /* browser window on top */
    if (ui->browser_open) {
        nk_begin(ctx, "FileBrowser", nk_rect(60, 60, 480, 320),
                 NK_WINDOW_TITLE | NK_WINDOW_MOVABLE);
        nk_layout_row_dynamic(ctx, 22, 1);
        nk_label(ctx, ui->browser.cwd, NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 180, 1);
        if (nk_group_begin(ctx, "entries", 0)) {
            nk_layout_row_dynamic(ctx, 22, 1);
            ui->n_entry_rects = 0;
            for (size_t i = 0; i < ui->browser.n_entries; i++) {
                BrowserEntry *b = &ui->browser.entries[i];
                if ((size_t)ui->n_entry_rects < ui->browser.n_entries) {
                    struct nk_rect r = nk_widget_bounds(ctx);
                    ui->entry_rects =
                        realloc(ui->entry_rects,
                                (size_t)(ui->n_entry_rects + 1) *
                                    sizeof(UIEntryRect));
                    ui->entry_rects[ui->n_entry_rects].name = b->name;
                    ui->entry_rects[ui->n_entry_rects].x = (int)r.x;
                    ui->entry_rects[ui->n_entry_rects].y = (int)r.y;
                    ui->entry_rects[ui->n_entry_rects].w = (int)r.w;
                    ui->entry_rects[ui->n_entry_rects].h = (int)r.h;
                    ui->n_entry_rects++;
                }
                nk_bool sel = 0;
                if (nk_selectable_label(ctx, b->name, NK_TEXT_LEFT, &sel)) {
                    browser_activate(&ui->browser, b->name);
                    ui->n_entry_rects = 0;
                    ui_browser_after_activate(ui);
                    break;
                }
            }
            nk_group_end(ctx);
        }
        nk_layout_row_dynamic(ctx, 24, 1);
        {
            int len = (int)strlen(ui->browser_path);
            nk_edit_string(ctx, NK_EDIT_SIMPLE, ui->browser_path, &len,
                           (int)sizeof(ui->browser_path) - 1,
                           nk_filter_default);
            ui->browser_path[len] = '\0';
            free(ui->browser.path_input);
            ui->browser.path_input = strdup(ui->browser_path);
        }
        nk_layout_row_static(ctx, 26,
                             (int)((ctx->current->layout->clip.w - 4.0f) / 2),
                             2);
        ui->ok_rect = ui_rect_of(ui->ok_rect, ctx);
        if (nk_button_label(ctx, "OK")) {
            char *res = browser_result(&ui->browser);
            if (res && *res)
                ui_commit_path(ui, res);
            free(res);
            ui->browser_open = false;
            ui->pending_save = false;
            ui->pending_export = false;
        }
        ui->cancel_rect = ui_rect_of(ui->cancel_rect, ctx);
        if (nk_button_label(ctx, "Cancel")) {
            ui->browser_open = false;
            ui->pending_save = false;
            ui->pending_export = false;
        }
        nk_end(ctx);
    }
}