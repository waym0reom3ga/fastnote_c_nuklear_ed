/* FastNote c_nuklear — GLFW + OpenGL device for Nuklear (modern API). */

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT

#include "nuklear.h"
#define NK_GLFW_GL2_IMPLEMENTATION
#include "nk_glfw.h" /* nuklear_glfw_gl2 header + impl */

/* Our application types (needed for RunGUI signature and UI state) */
#include "app.h"
#include "ui.h"

#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FR-11 accelerators + browser keyboard contract (spec 3.2).  Chain the
 * nuklear key callback (so its edit keys keep working) and translate
 * Ctrl+O/S, Ctrl+Shift+S, Ctrl+E, Ctrl+Shift+E and Ctrl+L, plus Enter and
 * Escape while the browser is open.  Enter/Escape are delivered on the key
 * PRESS here, not via nuklear's edge-triggered key events, which can lose a
 * fast press+release that collapses into one poll batch. */
static void fn_key_callback(GLFWwindow *win, int key, int scancode,
                            int action, int mods) {
    nk_glfw3_key_callback(win, key, scancode, action, mods);
    UIState *ui = glfwGetWindowUserPointer(win);
    if (!ui || action != GLFW_PRESS)
        return;
    if (key == GLFW_KEY_ENTER && !(mods & GLFW_MOD_CONTROL)) {
        ui->pending_accel = ACCEL_CONFIRM;
        return;
    }
    if (key == GLFW_KEY_ESCAPE) {
        if (ui->browser_open)
            ui->pending_accel = ACCEL_CANCEL;
        return;
    }
    if (!(mods & GLFW_MOD_CONTROL))
        return;
    switch (key) {
    case GLFW_KEY_O: ui->pending_accel = ACCEL_OPEN; break;
    case GLFW_KEY_S:
        ui->pending_accel = (mods & GLFW_MOD_SHIFT) ? ACCEL_SAVE_AS : ACCEL_SAVE;
        break;
    case GLFW_KEY_E:
        ui->pending_accel = (mods & GLFW_MOD_SHIFT) ? ACCEL_EXPORT_PDF : ACCEL_EXPORT;
        break;
    case GLFW_KEY_L: ui->pending_accel = ACCEL_FOCUS_PATH; break;
    }
}

/* FR-9: a close request on a dirty document opens the prompt instead of
 * closing (and never silently discards). */
static void fn_close_callback(GLFWwindow *win) {
    UIState *ui = glfwGetWindowUserPointer(win);
    if (ui && ui_request_close(ui))
        glfwSetWindowShouldClose(win, GLFW_FALSE);
}

void RunGUI(AppState *state, const char *open_path) {
    (void)open_path; /* doc state comes from shared AppState already */

    if (!glfwInit()) {
        fprintf(stderr, "fastnote: cannot initialize GLFW\n");
        return;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 1);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    GLFWwindow *win =
        glfwCreateWindow(1024, 640, "FastNote - markdown editor", NULL, NULL);
    if (!win) {
        fprintf(stderr, "fastnote: cannot open window\n");
        glfwTerminate();
        return;
    }
    glfwMakeContextCurrent(win);

    /* Initialize nuklear context.  INSTALL_CALLBACKS is required: it wires
     * the char/scroll/mouse callbacks that feed nuklear's text input and
     * double-click state — without them the real window never receives typed
     * characters (the in-process tests drive nuklear directly and would mask
     * that).  The key callback is re-installed below to chain accelerators. */
    struct nk_context *ctx = nk_glfw3_init(win, NK_GLFW3_INSTALL_CALLBACKS);
    if (!ctx) {
        fprintf(stderr, "fastnote: cannot create nuklear context\n");
        glfwTerminate();
        return;
    }

    /* Bake default font and upload to GL */
    struct nk_font_atlas *atlas = NULL;
    nk_glfw3_font_stash_begin(&atlas);
    nk_glfw3_font_stash_end();
    if (atlas && atlas->default_font) {
        nk_style_set_font(ctx, &atlas->default_font->handle);
    }

    /* Create our UI state.  It owns its own model; only the event-file path
     * is taken from the caller's state (which main() frees afterwards). */
    UIState *ui = ui_state_new(state ? state->notes_dir : NULL);
    if (!ui) {
        nk_glfw3_shutdown();
        free(ctx);
        glfwTerminate();
        return;
    }
    if (state && state->event_file) {
        free(ui->app.event_file);
        ui->app.event_file = strdup(state->event_file);
    }
    ui_rebuild_preview(ui);
    snprintf(ui->editor_text, (int)ui->editor_cap, "%s",
             ui->app.doc.text ? ui->app.doc.text : "");

    glfwSetWindowUserPointer(win, ui);
    glfwSetKeyCallback(win, fn_key_callback);
    glfwSetWindowCloseCallback(win, fn_close_callback);

    int fb_w = 1024, fb_h = 640;
    glfwGetFramebufferSize(win, &fb_w, &fb_h);
    ui->width = fb_w;
    ui->height = fb_h;

    bool painted = false;
    char last_title[1024] = "";

    /* Main loop.  glfwPollEvents runs first: it delivers events to the
     * installed callbacks (char -> text, key -> key_events, scroll, mouse),
     * and nk_glfw3_new_frame then mirrors that accumulated state into the
     * nuklear input inside its input_begin/input_end block — the canonical
     * per-frame input model from the official example. */
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        nk_glfw3_new_frame();

        if (ui) {
            int w, h;
            glfwGetFramebufferSize(win, &w, &h);
            if (w > 0 && h > 0) {
                ui->width = w;
                ui->height = h;
            }
            ui_pump_focus(ui, ctx);
            ui_run_frame(ui, ctx);

            /* the prompt's Save/Discard confirmed the close */
            if (ui->closing)
                glfwSetWindowShouldClose(win, GLFW_TRUE);

            /* the window title carries the document + dirty state (FR-3) */
            char title[1024];
            if (ui->app.doc.path) {
                const char *base = strrchr(ui->app.doc.path, '/');
                base = base ? base + 1 : ui->app.doc.path;
                snprintf(title, sizeof(title), "%s%s — FastNote", base,
                         ui->app.doc.dirty ? "*" : "");
            } else {
                snprintf(title, sizeof(title), "FastNote");
            }
            if (strcmp(title, last_title) != 0) {
                glfwSetWindowTitle(win, title);
                snprintf(last_title, sizeof(last_title), "%s", title);
            }
        }

        nk_glfw3_render(NK_ANTI_ALIASING_ON);
        if (!painted) {
            fn_event(state, "painted"); /* first presented frame (spec 5.1) */
            painted = true;
        }
    }

    /* Cleanup */
    if (ui) {
        ui_state_free(ui);
    } else {
        free(ctx);
    }
    nk_glfw3_shutdown();
    glfwTerminate();
}
