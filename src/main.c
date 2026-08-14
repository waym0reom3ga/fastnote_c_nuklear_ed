/* FastNote C/Nuklear Edition — Entry Point.
 *
 * Exactly two permitted flags (specification §5.1):
 *   --version        print port identifier and version, exit 0
 *   --event-file P   append one line per completed user-visible phase
 *                    (painted/open/save/save-as/export-html/export-pdf)
 * Any other argument is rejected with a non-zero exit.  Bare launch opens
 * the GUI.  There is no CLI seam. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

extern void RunGUI(AppState *state, const char *open_path);

static void print_usage(FILE *out) {
    fprintf(out,
        "fastnote_c_nuklear — markdown editor (C/Nuklear)\n"
        "usage: fastnote_c_nuklear [--version] [--event-file PATH]\n"
        "  --version        print version and exit\n"
        "  --event-file P   append a phase marker line to P when each\n"
        "                   user-visible phase completes\n");
}

int main(int argc, char *argv[]) {
    const char *event_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("fastnote_c_nuklear v%s\n", APP_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--event-file") == 0 && i + 1 < argc) {
            event_file = argv[i + 1];
            i++;
            continue;
        }
        fprintf(stderr, "fastnote_c_nuklear: unknown option: %s\n", argv[i]);
        print_usage(stderr);
        return 2;
    }

    AppState *state = app_state_new(NULL);
    if (!state) {
        fprintf(stderr, "fastnote: out of memory\n");
        return 1;
    }
    if (event_file) {
        free(state->event_file);
        state->event_file = strdup(event_file);
    }
    RunGUI(state, NULL);
    app_state_free(state);
    return 0;
}
