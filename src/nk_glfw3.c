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

    /* Initialize nuklear context */
    struct nk_context *ctx = nk_glfw3_init(win, NK_GLFW3_DEFAULT);
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

    /* Create our UI state */
    UIState *ui = ui_state_new(state ? state->notes_dir : NULL);
    if (!ui) {
        nk_glfw3_shutdown();
        free(ctx);
        glfwTerminate();
        return;
    }
    app_state_free_fields(&ui->app);
    memcpy(&ui->app, state, sizeof(AppState));
    ui_rebuild_preview(ui);
    snprintf(ui->editor_text, (int)ui->editor_cap, "%s",
             ui->app.doc.text ? ui->app.doc.text : "");

    int fb_w = 1024, fb_h = 640;
    glfwGetFramebufferSize(win, &fb_w, &fb_h);
    ui->width = fb_w;
    ui->height = fb_h;

    /* Main loop */
    while (!glfwWindowShouldClose(win)) {
        nk_glfw3_new_frame();
        glfwPollEvents();

        if (ui) {
            int w, h;
            glfwGetFramebufferSize(win, &w, &h);
            if (w > 0 && h > 0) {
                ui->width = w;
                ui->height = h;
            }
            ui_run_frame(ui, ctx);
        }

        nk_glfw3_render(NK_ANTI_ALIASING_ON);
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
