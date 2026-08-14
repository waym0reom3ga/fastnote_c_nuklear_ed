/* FastNote C editions — in-app file browser implementation. */

#include "file_browser.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"

static bool has_app_ext(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext)
        return false;
    return strcasecmp(ext, ".md") == 0 || strcasecmp(ext, ".markdown") == 0 ||
           strcasecmp(ext, ".txt") == 0;
}

static bool is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    return S_ISDIR(st.st_mode);
}

static bool is_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    return S_ISREG(st.st_mode);
}

char *browser_new(FileBrowser *b, const char *mode, const char *start_dir) {
    /* Free any allocations from a previous open */
    for (size_t i = 0; i < b->n_entries; i++)
        free(b->entries[i].name);
    free(b->entries);
    free(b->mode);
    free(b->cwd);
    free(b->path_input);
    free(b->selected);
    memset(b, 0, sizeof(*b));
    b->mode = strdup(mode && strcmp(mode, "open") == 0 ? "open" : "save");
    const char *start = start_dir && *start_dir ? start_dir : getenv("HOME");
    b->cwd = strdup(start && *start ? start : "/");
    b->path_input = strdup("");
    return browser_refresh(b);
}

char *browser_refresh(FileBrowser *b) {
    DIR *d = opendir(b->cwd);
    if (!d) {
        fn_set_error("cannot list %s", b->cwd);
        return (char *)fn_error();
    }
    BrowserEntry *entries = NULL;
    size_t n = 0, cap = 0;

    entries = realloc(entries, (n + 1) * sizeof(BrowserEntry));
    entries[n].name = strdup("..");
    entries[n].is_dir = true;
    n++;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0 ||
            de->d_name[0] == '\0')
            continue;
        char *full = malloc(strlen(b->cwd) + strlen(de->d_name) + 2);
        sprintf(full, "%s/%s", b->cwd, de->d_name);
        if (is_dir(full)) {
            if (cap <= n) { cap += 8; entries = realloc(entries, cap * sizeof(BrowserEntry)); }
            entries[n].name = strdup(de->d_name);
            entries[n].is_dir = true;
            n++;
        } else if (is_file(full) && (b->show_all || has_app_ext(de->d_name))) {
            if (cap <= n) { cap += 8; entries = realloc(entries, cap * sizeof(BrowserEntry)); }
            entries[n].name = strdup(de->d_name);
            entries[n].is_dir = false;
            n++;
        }
        free(full);
    }
    closedir(d);

    for (size_t i = 0; i < b->n_entries; i++)
        free(b->entries[i].name);
    free(b->entries);
    b->entries = entries;
    b->n_entries = n;
    return NULL;
}

static char *join_abs(const char *dir, const char *name) {
    char *full = malloc(strlen(dir) + strlen(name) + 2);
    if (!full)
        return NULL;
    sprintf(full, "%s/%s", dir, name);
    return full;
}

char *browser_activate(FileBrowser *b, const char *name) {
    if (strcmp(name, "..") == 0) {
        /* join_abs("..") would build a literal ".../sub/.." cwd; go to the
         * real parent instead. */
        char *err = browser_parent(b);
        if (err)
            return err;
        free(b->path_input);
        b->path_input = strdup("");
        free(b->selected);
        b->selected = strdup("");
        return NULL;
    }
    char *full = join_abs(b->cwd, name);
    if (is_dir(full)) {
        free(b->cwd);
        b->cwd = full;
        free(b->path_input);
        b->path_input = strdup("");
        free(b->selected);
        b->selected = strdup("");
        return browser_refresh(b);
    }
    if (strcmp(b->mode, "open") == 0 && !is_file(full)) {
        free(full);
        return NULL;
    }
    free(b->selected);
    b->selected = full;
    return NULL; /* selected file path in b->selected */
}

char *browser_parent(FileBrowser *b) {
    char *slash = strrchr(b->cwd, '/');
    if (!slash || slash == b->cwd)
        return NULL;
    *slash = '\0';
    return browser_refresh(b);
}

char *browser_toggle_filter(FileBrowser *b) {
    b->show_all = !b->show_all;
    return browser_refresh(b);
}

char *browser_result(FileBrowser *b) {
    char *path = b->path_input && *b->path_input ? b->path_input : b->selected;
    if (!path || !*path) {
        fn_set_error("choose a file or type a path");
        return (char *)fn_error();
    }
    if (path[0] != '/') {
        char *full = join_abs(b->cwd, path);
        return full;
    }
    return strdup(path);
}

void browser_show_all(FileBrowser *b) { b->show_all = true; }

void browser_free(FileBrowser *b) {
    for (size_t i = 0; i < b->n_entries; i++)
        free(b->entries[i].name);
    free(b->entries);
    free(b->mode);
    free(b->cwd);
    free(b->path_input);
    free(b->selected);
    memset(b, 0, sizeof(*b));
}