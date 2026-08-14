/* FastNote c_nuklear — UI pointer tests (A13).
 * Builds a real nuklear context, injects real pointer events into the same
 * input state the GLFW window uses, and runs ui_run_frame() to check that
 * button clicks actually reach the shared action layer. */

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT

#include "ui.h"

/* nuklear implementation comes from app_ui.o; just need the header for API */
#include "nuklear.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;

static void tcheck(const char *name, bool ok, const char *detail) {
    if (ok) {
        printf("ok   %s\n", name);
    } else {
        printf("MISMATCH %s %s\n", name, detail ? detail : "");
        fails++;
    }
}

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool contains(const char *hay, const char *needle) {
    return hay && strstr(hay, needle) != NULL;
}

static void frame(UIState *ui, struct nk_context *ctx) {
    nk_clear(ctx);
    ui_run_frame(ui, ctx);
}

/* Inject a real pointer click at (x, y).  nuklear detects clicks across two
 * frames: press down on frame N, release on frame N+1.
 *
 * nk_clear must run every frame (as the real GLFW backend does inside
 * nk_glfw3_render).  It resets the command buffer and runs nuklear's window
 * garbage collector, which frees windows that were not drawn last frame; it
 * does NOT touch ctx->input, so the button-held state survives into the
 * release frame.  Without the per-frame clear, a window that was closed the
 * previous frame (e.g. the FileBrowser after Cancel) keeps its stale seq,
 * survives the GC, and is found-but-buried on the next open: the full-screen
 * main window promotes itself above it, the browser is marked NK_WINDOW_ROM,
 * and its widgets stop responding to clicks. */
static void click(UIState *ui, struct nk_context *ctx, int x, int y) {
    /* Frame 1: cursor over widget, button pressed */
    nk_clear(ctx);
    nk_input_begin(ctx);
    nk_input_motion(ctx, x, y);
    nk_input_button(ctx, NK_BUTTON_LEFT, x, y, 1);
    ui_run_frame(ui, ctx);

    /* Frame 2: release */
    nk_clear(ctx);
    nk_input_begin(ctx);
    nk_input_motion(ctx, x, y);
    nk_input_button(ctx, NK_BUTTON_LEFT, x, y, 0);
    ui_run_frame(ui, ctx);
}

/* Click the centre of the toolbar control with the given handler. */
static void click_toolbar(UIState *ui, struct nk_context *ctx,
                           void (*handler)(UIState *)) {
    for (int i = 0; i < ui->n_toolbar; i++) {
        if (ui->toolbar[i].handler == handler) {
            click(ui, ctx, ui->toolbar[i].x + ui->toolbar[i].w / 2,
                  ui->toolbar[i].y + ui->toolbar[i].h / 2);
            return;
        }
    }
}

/* Click the centre of the OK/Cancel buttons of the browser window. */
static void click_ok(UIState *ui, struct nk_context *ctx) {
    click(ui, ctx, ui->ok_rect.x + ui->ok_rect.w / 2,
          ui->ok_rect.y + ui->ok_rect.h / 2);
}

static void click_cancel(UIState *ui, struct nk_context *ctx) {
    click(ui, ctx, ui->cancel_rect.x + ui->cancel_rect.w / 2,
          ui->cancel_rect.y + ui->cancel_rect.h / 2);
}

/* Click the browser entry whose name matches; returns true when found. */
static bool click_entry(UIState *ui, struct nk_context *ctx,
                         const char *name) {
    for (int i = 0; i < ui->n_entry_rects; i++) {
        if (strcmp(ui->entry_rects[i].name, name) == 0) {
            click(ui, ctx, ui->entry_rects[i].x + ui->entry_rects[i].w / 2,
                  ui->entry_rects[i].y + ui->entry_rects[i].h / 2);
            return true;
        }
    }
    return false;
}

static bool browser_lists(UIState *ui, const char *name) {
    for (int i = 0; i < ui->n_entry_rects; i++)
        if (strcmp(ui->entry_rects[i].name, name) == 0)
            return true;
    return false;
}

static bool toolbar_visible(UIState *ui) {
    for (int i = 0; i < ui->n_toolbar; i++)
        if (ui->toolbar[i].w <= 0 || ui->toolbar[i].h <= 0)
            return false;
    return true;
}

static char *mktmpdir(void) { return mkdtemp(strdup("/tmp/fastnote-ui-XXXXXX")); }

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(content, 1, strlen(content), f);
        fclose(f);
    }
}

static bool read_head(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    size_t n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    fclose(f);
    return true;
}

int main(void) {
    char *dir = mktmpdir();
    if (!dir) {
        printf("MISMATCH setup cannot create temp dir\n");
        return 1;
    }
    char a_md[512], a_pdf[512], sub_dir[512], html_out[512], pdf_out[512];
    snprintf(a_md, sizeof(a_md), "%s/a.md", dir);
    snprintf(a_pdf, sizeof(a_pdf), "%s/a.pdf", dir);
    snprintf(sub_dir, sizeof(sub_dir), "%s/sub", dir);
    snprintf(html_out, sizeof(html_out), "%s/Hello.html", dir);
    snprintf(pdf_out, sizeof(pdf_out), "%s/Hello.pdf", dir);
    mkdir(sub_dir, 0755);
    write_file(a_md, "# Hello\n\nWorld\n");

    UIState *ui = ui_state_new(dir);
    struct nk_context *ctx = ui_nk_new();
    if (!ui || !ctx) {
        printf("MISMATCH setup cannot create UI state\n");
        return 1;
    }
    ui->width = 1024;
    ui->height = 640;

    /* The acceptance harness runs this suite once normally and once with
     * FASTNOTE_SABOTAGE=1; the sabotaged run MUST fail.  Unbind the Open
     * control so "open.opens-browser" can never pass — nothing else changes
     * and the suite is otherwise identical. */
    if (getenv("FASTNOTE_SABOTAGE") != NULL) {
        for (int i = 0; i < ui->n_toolbar; i++)
            if (ui->toolbar[i].handler == on_open)
                ui->toolbar[i].handler = NULL;
    }

    /* 1. the toolbar is really laid out (real widget bounds) */
    frame(ui, ctx);
    tcheck("toolbar.rendered", toolbar_visible(ui), "toolbar rects invalid");
    tcheck("toolbar.initial-status", contains(ui->status_text, "Ready"),
           "initial status");

    /* 2. clicking Insert really calls the shared action (FR-3 via button) */
    click_toolbar(ui, ctx, on_insert);
    tcheck("insert.changes-doc", contains(ui->app.doc.text, "inserted"),
           "doc text unchanged");
    tcheck("insert.marks-dirty", ui->app.doc.dirty, "doc not dirty");
    tcheck("insert.refreshes-preview",
           contains(ui->preview_text, "inserted"), "preview stale");

    /* 3. Open button opens the in-app browser, listing the notes dir */
    click_toolbar(ui, ctx, on_open);
    tcheck("open.opens-browser", ui->browser_open, "browser did not open");
    tcheck("open.lists-notes", ui->n_entry_rects > 1,
           "no entries listed in browser");
    tcheck("open.lists-a-md", browser_lists(ui, "a.md"),
           "a.md not listed in the browser");
    tcheck("open.lists-subdir", browser_lists(ui, "sub"),
           "sub not listed in the browser");

    /* 4. navigation: enter sub, go back, then cancel */
    tcheck("browser.enter-dir", click_entry(ui, ctx, "sub") &&
                                    contains(ui->browser.cwd, "sub"),
           "dir entry did not navigate");
    tcheck("browser.parent", click_entry(ui, ctx, "..") &&
                                 !contains(ui->browser.cwd, "sub"),
           "parent entry did not go back");
    click_cancel(ui, ctx);
    tcheck("browser.cancel-closes", !ui->browser_open,
           "Cancel did not close the browser");

    /* 5. clicking a file entry opens the document (FR-1 via button) */
    click_toolbar(ui, ctx, on_open);
    tcheck("open.entry-a-md", click_entry(ui, ctx, "a.md") &&
                                  !ui->browser_open &&
                                  contains(ui->app.doc.text, "# Hello"),
           "clicking a.md did not open the doc");
    tcheck("open.preview-updated", contains(ui->preview_text, "HELLO"),
           "preview not rebuilt after open");

    /* 6. Save button persists via the action layer (FR-5) */
    click_toolbar(ui, ctx, on_insert);
    click_toolbar(ui, ctx, on_save);
    {
        char head[512];
        bool ok_read = read_head(a_md, head, sizeof(head));
        tcheck("save.wrote-file", ok_read && contains(head, "inserted") &&
                                      !ui->app.doc.dirty,
               "save did not persist the document");
    }

    /* 7. Export button -> in-app browser -> OK writes standalone HTML (FR-7) */
    click_toolbar(ui, ctx, on_export);
    tcheck("export.opens-browser", ui->browser_open && ui->pending_export,
           "export did not open the save browser");
    tcheck("export.suggests-name", contains(ui->browser_path, "Hello"),
           "no suggested export name");
    click_ok(ui, ctx);
    {
        char head[256];
        bool ok_read = read_head(html_out, head, sizeof(head));
        tcheck("export.wrote-html", file_exists(html_out),
               "no HTML file written");
        tcheck("export.html-doctype", ok_read && contains(head, "<!DOCTYPE"),
               "exported HTML is not a standalone document");
    }

    /* 8. Export PDF button -> in-app browser -> OK writes a PDF (FR-8) */
    click_toolbar(ui, ctx, on_export_pdf);
    tcheck("export-pdf.opens-browser", ui->browser_open &&
                                           ui->pending_export_pdf,
           "PDF export did not open the save browser");
    click_ok(ui, ctx);
    {
        char head[64];
        bool ok_read = read_head(pdf_out, head, sizeof(head));
        tcheck("export-pdf.wrote-pdf", file_exists(pdf_out),
               "no PDF file written");
        tcheck("export-pdf.magic", ok_read && contains(head, "%PDF-1.4"),
               "exported PDF is not a PDF");
    }

    /* 9. theme toggle */
    click_toolbar(ui, ctx, on_theme);
    tcheck("theme.toggled-dark", ui->theme_dark,
           "theme did not switch to dark");
    click_toolbar(ui, ctx, on_theme);
    tcheck("theme.toggled-light", !ui->theme_dark,
           "theme did not switch back");

    /* 10. save-as: Save with no path opens the browser (FR-6) */
    free(ui->app.doc.path);
    ui->app.doc.path = NULL;
    click_toolbar(ui, ctx, on_save);
    tcheck("saveas.opens-browser", ui->browser_open && ui->pending_save,
           "Save without a path did not open save-as");
    snprintf(ui->browser_path, sizeof(ui->browser_path), "%s/saved.md", dir);
    free(ui->browser.path_input);
    ui->browser.path_input = strdup(ui->browser_path);
    click_ok(ui, ctx);
    {
        char full[600];
        snprintf(full, sizeof(full), "%s/saved.md", dir);
        tcheck("saveas.wrote-file", file_exists(full) && !ui->app.doc.dirty,
               "save-as did not write the file");
        tcheck("saveas.tracks-path",
               contains(ui->app.doc.path ? ui->app.doc.path : "", "saved.md"),
               "document path not updated");
    }

    ui_nk_free(ctx);
    ui_state_free(ui);
    unlink(a_md);
    unlink(html_out);
    unlink(pdf_out);
    rmdir(sub_dir);
    rmdir(dir);
    free(dir);

    printf("\nui tests: %s\n", fails == 0 ? "all passed" : "mismatched");
    return fails == 0 ? 0 : 1;
}