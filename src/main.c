/* FastNote C/Nuklear Edition — Entry Point.
 *
 * The only permitted flag is --version (specification §5.1).  There is no
 * CLI seam: no --open/--insert/--save/--export, no --headless, no
 * --selftest.  Bare launch opens the GUI. */

#include <stdio.h>
#include <string.h>

#include "app.h"

extern void RunGUI(AppState *state, const char *open_path);

int main(int argc, char *argv[]) {
    (void)argc;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("fastnote_c_nuklear v%s\n", APP_VERSION);
            return 0;
        }
    }

    AppState *state = app_state_new(NULL);
    if (!state) {
        fprintf(stderr, "fastnote: out of memory\n");
        return 1;
    }
    RunGUI(state, NULL);
    app_state_free(state);
    return 0;
}
