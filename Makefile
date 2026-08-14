# FastNote C/Nuklear Edition — Build system

CC = ccache gcc
CFLAGS = -Wall -Wextra $(shell pkg-config --cflags glfw3 gl) -Isrc
LDFLAGS = $(shell pkg-config --libs glfw3 gl) -lm -lpthread -ldl
TEST_LDFLAGS = -lm -lpthread

SRCDIR = src
BUILDDIR = build
TARGET = fastnote_c_nuklear

SOURCES = $(SRCDIR)/actions.c $(SRCDIR)/app.c $(SRCDIR)/app_ui.c \
          $(SRCDIR)/export.c $(SRCDIR)/file_browser.c $(SRCDIR)/main.c \
          $(SRCDIR)/nk_glfw3.c $(SRCDIR)/pdfwriter.c $(SRCDIR)/renderer.c

OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SOURCES))

.PHONY: all clean test test-ui

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

TEST_OBJECTS = $(BUILDDIR)/actions.o $(BUILDDIR)/app.o $(BUILDDIR)/app_ui.o \
               $(BUILDDIR)/export.o                $(BUILDDIR)/file_browser.o \
               $(BUILDDIR)/pdfwriter.o $(BUILDDIR)/renderer.o

$(BUILDDIR)/test_ui.o: $(SRCDIR)/test_ui.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

TEST_BIN = test_ui_bin
$(TEST_BIN): $(TEST_OBJECTS) $(BUILDDIR)/test_ui.o
	$(CC) -o $@ $^ $(TEST_LDFLAGS)

test-ui: $(TEST_BIN)
	./$(TEST_BIN)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR) $(TARGET) $(TEST_BIN)

test: all
	./$(TARGET) --version
	$(MAKE) test-ui
