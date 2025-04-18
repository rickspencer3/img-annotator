#include <gtk/gtk.h>
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>

// Global variables
GtkWidget *drawing_area;
GdkPixbuf *original_pixbuf = NULL;
GdkPixbuf *current_pixbuf = NULL;
cairo_surface_t *surface = NULL;
gboolean is_drawing = FALSE;
gdouble last_x = 0;
gdouble last_y = 0;
GdkRGBA current_color = {1.0, 0.0, 0.0, 1.0}; // Default red
GdkRGBA text_color = {1.0, 0.0, 0.0, 1.0};   // Initialize to same red color
gint pen_width = 5;
gboolean is_text_mode = FALSE;
static GtkWidget *font_button;
static char *current_font = NULL;
GtkWidget *color_button = NULL; // Add color button as global variable
#define MAX_UNDO_STACK 20  // Maximum number of states to store
gboolean has_changes = FALSE;  // Track if any actual drawing has occurred
gboolean has_moved = FALSE;  // Add this global variable to track if we've moved since pressing
gboolean is_crop_mode = FALSE;
gboolean is_selecting = FALSE;
gdouble crop_start_x = 0;
gdouble crop_start_y = 0;
gdouble crop_end_x = 0;
gdouble crop_end_y = 0;
GtkWidget *crop_button = NULL;  // Button to perform crop
GtkWidget *mode_combo;
static GtkWidget *popup_menu = NULL;
static gboolean popup_visible = FALSE;

// Add these as global variables
static GtkWidget *mode_menu = NULL;
static int current_mode = 0;

// Add this enum definition before the mode_info array
typedef enum {
    MODE_DRAW,
    MODE_TEXT,
    MODE_CROP
} EditorMode;

// Structure to hold mode information
typedef struct {
    const char *icon_name;
    const char *tooltip;
    EditorMode mode;
} ModeInfo;

static const ModeInfo mode_info[] = {
    {"x-office-drawing", "Draw freely on the image", MODE_DRAW},
    {"insert-text", "Add text annotations", MODE_TEXT},
    {"edit-cut", "Crop the image", MODE_CROP}
};

typedef struct {
    GdkPixbuf *states[MAX_UNDO_STACK];
    int current;  // Current position in the stack
    int top;      // Top of the stack
} UndoStack;

UndoStack undo_stack = {.current = -1, .top = -1};
GtkWidget *undo_button;
GtkWidget *redo_button;

// Add this global variable to track the current tooltip
static char *current_tooltip = NULL;

// Forward declare the functions we'll need
static void on_menu_item_activate(GtkMenuItem *item, gpointer data);
static gboolean on_combo_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data);

// Function declarations
static void load_image_from_file(const gchar *filename);
static void load_image_from_clipboard();
static void save_image(const gchar *filename);
static void draw_on_surface(cairo_t *cr, gdouble x, gdouble y);
static void update_drawing_area();
static void add_text_at_position(gdouble x, gdouble y);
static void push_undo_state(void);
static void undo(void);
static void redo(void);
static void perform_crop(void);
static void on_mode_changed(GtkComboBox *combo, gpointer data);
static gboolean on_mode_combo_tooltip(GtkWidget *widget, gint x, gint y,
                                    gboolean keyboard_mode, GtkTooltip *tooltip,
                                    gpointer data);
static void on_popup_shown(GtkWidget *popup_window, gpointer data);
static void on_popup_hidden(GtkWidget *popup_window, gpointer data);

// Callback functions
static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    if (current_pixbuf) {
        // Set the drawing area size to match the image
        gtk_widget_set_size_request(drawing_area, 
                                  gdk_pixbuf_get_width(current_pixbuf),
                                  gdk_pixbuf_get_height(current_pixbuf));
        
        // Draw white background
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        
        // Draw the image
        gdk_cairo_set_source_pixbuf(cr, current_pixbuf, 0, 0);
        cairo_paint(cr);
        
        // Draw crop selection rectangle if needed
        if (is_crop_mode && (is_selecting || (crop_start_x != crop_end_x && crop_start_y != crop_end_y))) {
            double x = MIN(crop_start_x, crop_end_x);
            double y = MIN(crop_start_y, crop_end_y);
            double width = abs(crop_end_x - crop_start_x);
            double height = abs(crop_end_y - crop_start_y);
            
            // Draw semi-transparent overlay
            cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
            
            // Draw the darkened areas around the selection
            cairo_rectangle(cr, 0, 0, gdk_pixbuf_get_width(current_pixbuf),
                          gdk_pixbuf_get_height(current_pixbuf));
            cairo_rectangle(cr, x, y, width, height);
            cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
            cairo_fill(cr);
            
            // Draw selection rectangle
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_set_line_width(cr, 1);
            cairo_rectangle(cr, x - 0.5, y - 0.5, width + 1, height + 1);
            cairo_stroke(cr);
        }
    }
    return FALSE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button == GDK_BUTTON_PRIMARY) {
        if (is_text_mode) {
            add_text_at_position(event->x, event->y);
            return TRUE;
        }
        
        if (is_crop_mode) {
            is_selecting = TRUE;
            crop_start_x = crop_end_x = event->x;
            crop_start_y = crop_end_y = event->y;
            gtk_widget_queue_draw(drawing_area);
            return TRUE;
        }
        
        is_drawing = TRUE;
        has_moved = FALSE;
        last_x = event->x;
        last_y = event->y;
        
        // Store the initial state before any drawing
        if (current_pixbuf) {
            // Initialize the undo stack if it's empty
            if (undo_stack.current == -1) {
                undo_stack.current = 0;
                undo_stack.top = 0;
            }
            
            // Store initial state
            GdkPixbuf *initial_state = gdk_pixbuf_copy(current_pixbuf);
            if (initial_state) {
                if (undo_stack.states[undo_stack.current]) {
                    g_object_unref(undo_stack.states[undo_stack.current]);
                }
                undo_stack.states[undo_stack.current] = initial_state;
            }
        }
        
        return TRUE;
    }
    return TRUE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button == GDK_BUTTON_PRIMARY) {
        if (is_selecting && is_crop_mode) {
            is_selecting = FALSE;
            crop_end_x = event->x;
            crop_end_y = event->y;
            
            // Enable crop button if we have a valid selection
            int width = abs(crop_end_x - crop_start_x);
            int height = abs(crop_end_y - crop_start_y);
            gtk_widget_set_sensitive(crop_button, width > 1 && height > 1);
            
            gtk_widget_queue_draw(drawing_area);
            return TRUE;
        }
        
        if (is_drawing) {
            if (has_moved) {
                push_undo_state();
                gtk_widget_set_sensitive(undo_button, TRUE);
                gtk_widget_set_sensitive(redo_button, FALSE);
            }
            is_drawing = FALSE;
            has_moved = FALSE;
        }
    }
    return TRUE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    if (is_selecting && is_crop_mode) {
        crop_end_x = event->x;
        crop_end_y = event->y;
        gtk_widget_queue_draw(drawing_area);
        return TRUE;
    }
    
    if (is_drawing && !is_text_mode && current_pixbuf) {
        has_moved = TRUE;  // Mark that we've moved while drawing
        
        // Create a new surface for the current state
        cairo_surface_t *temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                                 gdk_pixbuf_get_width(current_pixbuf),
                                                                 gdk_pixbuf_get_height(current_pixbuf));
        cairo_t *cr = cairo_create(temp_surface);
        
        // Draw current state - use current_pixbuf instead of undo stack state
        gdk_cairo_set_source_pixbuf(cr, current_pixbuf, 0, 0);
        cairo_paint(cr);
        
        // Add new line
        cairo_set_source_rgba(cr, current_color.red, current_color.green, current_color.blue, current_color.alpha);
        cairo_set_line_width(cr, pen_width);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_move_to(cr, last_x, last_y);
        cairo_line_to(cr, event->x, event->y);
        cairo_stroke(cr);
        
        // Update the pixbuf
        cairo_surface_flush(temp_surface);
        GdkPixbuf *new_pixbuf = gdk_pixbuf_get_from_surface(temp_surface, 0, 0,
                                                           gdk_pixbuf_get_width(current_pixbuf),
                                                           gdk_pixbuf_get_height(current_pixbuf));
        
        if (new_pixbuf) {
            if (current_pixbuf) {
                g_object_unref(current_pixbuf);
            }
            current_pixbuf = new_pixbuf;
            gtk_widget_queue_draw(drawing_area);
        }
        
        cairo_destroy(cr);
        cairo_surface_destroy(temp_surface);
        
        last_x = event->x;
        last_y = event->y;
    }
    return TRUE;
}

static void on_color_set(GtkColorButton *button, gpointer data) {
    GdkRGBA new_color;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &new_color);
    
    // Update both colors to match the color picker
    current_color = new_color;
    text_color = new_color;
}

static void on_pen_width_changed(GtkSpinButton *button, gpointer data) {
    pen_width = gtk_spin_button_get_value_as_int(button);
}

static void on_font_set(GtkFontButton *button, gpointer data) {
    const char *new_font = gtk_font_button_get_font_name(GTK_FONT_BUTTON(button));
    if (new_font) {
        if (current_font) {
            g_free(current_font);
        }
        current_font = g_strdup(new_font);
    }
}

static void on_save_clicked(GtkButton *button, gpointer data) {
    GtkWidget *dialog;
    GtkFileChooser *chooser;
    gint res;

    dialog = gtk_file_chooser_dialog_new("Save Image",
                                        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))),
                                        GTK_FILE_CHOOSER_ACTION_SAVE,
                                        "_Cancel", GTK_RESPONSE_CANCEL,
                                        "_Save", GTK_RESPONSE_ACCEPT,
                                        NULL);

    chooser = GTK_FILE_CHOOSER(dialog);
    gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    gtk_file_chooser_set_current_name(chooser, "annotated.png");

    res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        char *filename;
        filename = gtk_file_chooser_get_filename(chooser);
        save_image(filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

static void on_open_clicked(GtkButton *button, gpointer data) {
    GtkWidget *dialog;
    GtkFileChooser *chooser;
    gint res;

    dialog = gtk_file_chooser_dialog_new("Open Image",
                                        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))),
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        "_Cancel", GTK_RESPONSE_CANCEL,
                                        "_Open", GTK_RESPONSE_ACCEPT,
                                        NULL);

    chooser = GTK_FILE_CHOOSER(dialog);

    res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        char *filename;
        filename = gtk_file_chooser_get_filename(chooser);
        load_image_from_file(filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

static void on_drawing_area_realize(GtkWidget *widget, gpointer data) {
    GdkWindow *window = gtk_widget_get_window(widget);
    if (window) {
        GdkCursor *cursor = gdk_cursor_new_from_name(gdk_display_get_default(), "crosshair");
        gdk_window_set_cursor(window, cursor);
        g_object_unref(cursor);
    }
}

static void on_copy_clicked(GtkButton *button, gpointer data) {
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (current_pixbuf) {
        gtk_clipboard_set_image(clipboard, current_pixbuf);
    }
}

static void on_undo_clicked(GtkButton *button, gpointer data) {
    undo();
}

static void on_redo_clicked(GtkButton *button, gpointer data) {
    redo();
}

static void on_entry_activate(GtkEntry *entry, GtkDialog *dialog) {
    gtk_dialog_response(dialog, GTK_RESPONSE_ACCEPT);
}

// Main function
int main(int argc, char *argv[]) {
    GtkWidget *window;
    GtkWidget *vbox;
    GtkWidget *hbox;
    GtkWidget *color_button;
    GtkWidget *pen_width_spin;
    GtkWidget *font_button;
    GtkWidget *save_button;
    GtkWidget *open_button;
    GtkWidget *mode_label;

    gtk_init(&argc, &argv);

    // Create main window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Image Annotator");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);  // Increased from 800 to 1000
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Create main container with more padding
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // Create toolbar with more spacing
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);  // Increased spacing between items
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);  // Add padding around the toolbar
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);

    // File operations group
    GtkWidget *file_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_pack_start(GTK_BOX(hbox), file_box, FALSE, FALSE, 0);

    // Open button with icon
    open_button = gtk_button_new_from_icon_name("document-open", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(open_button, "Open Image");
    g_signal_connect(open_button, "clicked", G_CALLBACK(on_open_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(file_box), open_button, FALSE, FALSE, 0);

    // Save button with icon
    save_button = gtk_button_new_from_icon_name("document-save", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(save_button, "Save Image");
    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(file_box), save_button, FALSE, FALSE, 0);

    // Copy button with icon
    GtkWidget *copy_button = gtk_button_new_from_icon_name("edit-copy", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(copy_button, "Copy to Clipboard");
    g_signal_connect(copy_button, "clicked", G_CALLBACK(on_copy_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(file_box), copy_button, FALSE, FALSE, 0);

    // Add a small separator
    gtk_box_pack_start(GTK_BOX(hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 5);

    // Edit operations group
    GtkWidget *edit_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_pack_start(GTK_BOX(hbox), edit_box, FALSE, FALSE, 0);

    // Undo button with icon
    undo_button = gtk_button_new_from_icon_name("edit-undo", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(undo_button, "Undo");
    gtk_widget_set_sensitive(undo_button, FALSE);
    g_signal_connect(undo_button, "clicked", G_CALLBACK(on_undo_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(edit_box), undo_button, FALSE, FALSE, 0);

    // Redo button with icon
    redo_button = gtk_button_new_from_icon_name("edit-redo", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(redo_button, "Redo");
    gtk_widget_set_sensitive(redo_button, FALSE);
    g_signal_connect(redo_button, "clicked", G_CALLBACK(on_redo_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(edit_box), redo_button, FALSE, FALSE, 0);

    // Add a small separator
    gtk_box_pack_start(GTK_BOX(hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 5);

    // Tool settings group
    GtkWidget *tool_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(hbox), tool_box, FALSE, FALSE, 0);

    // Color button
    color_button = gtk_color_button_new();
    gtk_widget_set_tooltip_text(color_button, "Select Color");
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(color_button), &current_color);
    g_signal_connect(color_button, "color-set", G_CALLBACK(on_color_set), NULL);
    gtk_box_pack_start(GTK_BOX(tool_box), color_button, FALSE, FALSE, 0);

    // Pen width spinner
    GtkWidget *width_label = gtk_label_new("Width:");
    gtk_box_pack_start(GTK_BOX(tool_box), width_label, FALSE, FALSE, 0);
    
    pen_width_spin = gtk_spin_button_new_with_range(1, 50, 1);
    gtk_widget_set_tooltip_text(pen_width_spin, "Pen Width");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pen_width_spin), pen_width);
    g_signal_connect(pen_width_spin, "value-changed", G_CALLBACK(on_pen_width_changed), NULL);
    gtk_box_pack_start(GTK_BOX(tool_box), pen_width_spin, FALSE, FALSE, 0);

    // Font button
    font_button = gtk_font_button_new();
    gtk_widget_set_tooltip_text(font_button, "Select Font");
    current_font = g_strdup("Sans 12");  // Initialize with default font
    gtk_font_button_set_font_name(GTK_FONT_BUTTON(font_button), current_font);
    g_signal_connect(font_button, "font-set", G_CALLBACK(on_font_set), NULL);
    gtk_box_pack_start(GTK_BOX(tool_box), font_button, FALSE, FALSE, 0);

    // Add a small separator
    gtk_box_pack_start(GTK_BOX(hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 5);

    // Mode selection group
    GtkWidget *mode_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(hbox), mode_box, FALSE, FALSE, 0);

    // Mode selection dropdown
    mode_label = gtk_label_new("Mode:");
    gtk_box_pack_start(GTK_BOX(mode_box), mode_label, FALSE, FALSE, 5);
    
    // Create a button that looks like a combo box
    mode_combo = gtk_button_new();
    gtk_widget_set_tooltip_text(mode_combo, mode_info[0].tooltip);
    
    // Create the initial icon
    GtkWidget *combo_image = gtk_image_new_from_icon_name(
        mode_info[0].icon_name, GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_button_set_image(GTK_BUTTON(mode_combo), combo_image);
    
    // Create the popup menu
    mode_menu = gtk_menu_new();
    
    // Add menu items
    for (int i = 0; i < G_N_ELEMENTS(mode_info); i++) {
        GtkWidget *item = gtk_menu_item_new();
        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *icon = gtk_image_new_from_icon_name(
            mode_info[i].icon_name, GTK_ICON_SIZE_MENU);
        GtkWidget *label = gtk_label_new(mode_info[i].tooltip);
        
        gtk_container_add(GTK_CONTAINER(hbox), icon);
        gtk_container_add(GTK_CONTAINER(hbox), label);
        gtk_container_add(GTK_CONTAINER(item), hbox);
        gtk_widget_show_all(item);
        
        gtk_menu_shell_append(GTK_MENU_SHELL(mode_menu), item);
        g_signal_connect(item, "activate", 
                        G_CALLBACK(on_menu_item_activate), 
                        GINT_TO_POINTER(i));
    }
    
    // Connect the button click to show the menu
    g_signal_connect(mode_combo, "button-press-event",
                    G_CALLBACK(on_combo_button_press), NULL);
    
    gtk_box_pack_start(GTK_BOX(mode_box), mode_combo, FALSE, FALSE, 0);

    // Add a separator
    gtk_box_pack_start(GTK_BOX(hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 5);

    // Add crop button
    crop_button = gtk_button_new_from_icon_name("edit-cut", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_widget_set_tooltip_text(crop_button, "Crop Selection");
    gtk_widget_set_sensitive(crop_button, FALSE);
    g_signal_connect(crop_button, "clicked", G_CALLBACK(perform_crop), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), crop_button, FALSE, FALSE, 0);

    // Create a scrolled window
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_widget_set_hexpand(scrolled_window, TRUE);
    
    // Create a container for the drawing area with padding
    GtkWidget *padding_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(padding_box, 20);   // Increased to 20px
    gtk_widget_set_margin_end(padding_box, 20);     // Increased to 20px
    gtk_widget_set_margin_top(padding_box, 20);     // Increased to 20px
    gtk_widget_set_margin_bottom(padding_box, 20);  // Increased to 20px
    
    // Set grey background for the padding area
    GtkStyleContext *style_context = gtk_widget_get_style_context(scrolled_window);
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "scrolledwindow { background-color: #E0E0E0; }", -1, NULL);
    gtk_style_context_add_provider(style_context,
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    // Create drawing area
    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 400, 300);  // Minimum size
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect(drawing_area, "button-press-event", G_CALLBACK(on_button_press), NULL);
    g_signal_connect(drawing_area, "button-release-event", G_CALLBACK(on_button_release), NULL);
    g_signal_connect(drawing_area, "motion-notify-event", G_CALLBACK(on_motion_notify), NULL);
    g_signal_connect(drawing_area, "realize", G_CALLBACK(on_drawing_area_realize), NULL);
    gtk_widget_set_events(drawing_area, gtk_widget_get_events(drawing_area) |
                         GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                         GDK_POINTER_MOTION_MASK);

    // Pack everything together
    gtk_container_add(GTK_CONTAINER(padding_box), drawing_area);
    gtk_container_add(GTK_CONTAINER(scrolled_window), padding_box);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    // Show all widgets
    gtk_widget_show_all(window);

    // Check for clipboard image or command line argument
    if (argc > 1) {
        load_image_from_file(argv[1]);
    } else {
        load_image_from_clipboard();
    }

    gtk_main();

    return 0;
}

// Helper functions
static void load_image_from_file(const gchar *filename) {
    GError *error = NULL;
    original_pixbuf = gdk_pixbuf_new_from_file(filename, &error);
    if (error) {
        g_error_free(error);
        return;
    }
    current_pixbuf = gdk_pixbuf_copy(original_pixbuf);
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                       gdk_pixbuf_get_width(current_pixbuf),
                                       gdk_pixbuf_get_height(current_pixbuf));
    gtk_widget_set_size_request(drawing_area,
                              gdk_pixbuf_get_width(current_pixbuf),
                              gdk_pixbuf_get_height(current_pixbuf));
    update_drawing_area();

    // Clear undo stack
    for (int i = 0; i <= undo_stack.top; i++) {
        if (undo_stack.states[i]) {
            g_object_unref(undo_stack.states[i]);
            undo_stack.states[i] = NULL;
        }
    }
    
    // Reset undo stack
    undo_stack.current = -1;
    undo_stack.top = -1;
    
    // Disable both buttons initially
    gtk_widget_set_sensitive(undo_button, FALSE);
    gtk_widget_set_sensitive(redo_button, FALSE);
}

static void load_image_from_clipboard() {
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    GdkPixbuf *pixbuf = gtk_clipboard_wait_for_image(clipboard);
    
    if (pixbuf) {
        if (current_pixbuf) {
            g_object_unref(current_pixbuf);
        }
        current_pixbuf = pixbuf;
        
        // Reset crop state
        crop_start_x = crop_start_y = crop_end_x = crop_end_y = 0;
        is_selecting = FALSE;
        if (crop_button) {
            gtk_widget_set_sensitive(crop_button, FALSE);
        }
        
        // Reset undo stack and store initial state
        for (int i = 0; i <= undo_stack.top; i++) {
            if (undo_stack.states[i]) {
                g_object_unref(undo_stack.states[i]);
                undo_stack.states[i] = NULL;
            }
        }
        undo_stack.current = 0;  // Set to 0 instead of -1
        undo_stack.top = 0;
        undo_stack.states[0] = gdk_pixbuf_copy(current_pixbuf);  // Store initial state
        
        // Initially no undo/redo available
        gtk_widget_set_sensitive(undo_button, FALSE);
        gtk_widget_set_sensitive(redo_button, FALSE);
        
        gtk_widget_queue_draw(drawing_area);
    }
}

static void save_image(const gchar *filename) {
    if (current_pixbuf) {
        GError *error = NULL;
        gdk_pixbuf_save(current_pixbuf, filename, "png", &error, NULL);
        if (error) {
            g_error_free(error);
        }
    }
}

static void draw_on_surface(cairo_t *cr, gdouble x, gdouble y) {
    if (is_text_mode) {
        return; // Don't draw in text mode
    }
    
    // First draw the current image
    if (current_pixbuf) {
        gdk_cairo_set_source_pixbuf(cr, current_pixbuf, 0, 0);
        cairo_paint(cr);
    }
    
    // Then draw the new line
    cairo_set_source_rgba(cr, current_color.red, current_color.green, current_color.blue, current_color.alpha);
    cairo_set_line_width(cr, pen_width);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    
    if (is_drawing) {
        cairo_move_to(cr, last_x, last_y);
        cairo_line_to(cr, x, y);
        cairo_stroke(cr);
        
        // Update the current pixbuf with the drawing
        cairo_surface_flush(surface);
        GdkPixbuf *new_pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0,
                                                           gdk_pixbuf_get_width(current_pixbuf),
                                                           gdk_pixbuf_get_height(current_pixbuf));
        if (new_pixbuf) {
            g_object_unref(current_pixbuf);
            current_pixbuf = new_pixbuf;
            push_undo_state();
        }
    }
}

static void update_drawing_area() {
    gtk_widget_queue_draw(drawing_area);
}

static void add_text_at_position(gdouble x, gdouble y) {
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *entry;
    gint response;

    // Store the current state before adding text
    if (current_pixbuf) {
        if (undo_stack.current == -1) {
            undo_stack.current = 0;
            undo_stack.top = 0;
            undo_stack.states[0] = gdk_pixbuf_copy(current_pixbuf);
        }
    }

    dialog = gtk_dialog_new_with_buttons("Enter Text",
                                       GTK_WINDOW(gtk_widget_get_toplevel(drawing_area)),
                                       GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                       "_OK", GTK_RESPONSE_ACCEPT,
                                       "_Cancel", GTK_RESPONSE_CANCEL,
                                       NULL);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    g_signal_connect(entry, "activate", G_CALLBACK(on_entry_activate), dialog);
    gtk_container_add(GTK_CONTAINER(content_area), entry);
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(entry);

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT) {
        const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
        if (text && *text && current_pixbuf) {
            cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                               gdk_pixbuf_get_width(current_pixbuf),
                                                               gdk_pixbuf_get_height(current_pixbuf));
            cairo_t *cr = cairo_create(surface);

            gdk_cairo_set_source_pixbuf(cr, current_pixbuf, 0, 0);
            cairo_paint(cr);

            cairo_set_source_rgba(cr, text_color.red, text_color.green, text_color.blue, text_color.alpha);
            
            // Parse the font string to get size and family
            PangoFontDescription *font_desc = pango_font_description_from_string(current_font);
            double font_size = pango_font_description_get_size(font_desc) / PANGO_SCALE;
            const char *font_family = pango_font_description_get_family(font_desc);
            
            // Set the font
            cairo_select_font_face(cr, 
                                 font_family ? font_family : "Sans",
                                 CAIRO_FONT_SLANT_NORMAL,
                                 CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, font_size > 0 ? font_size : 12);
            
            pango_font_description_free(font_desc);

            cairo_move_to(cr, x, y);
            cairo_show_text(cr, text);

            cairo_surface_flush(surface);
            GdkPixbuf *new_pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0,
                                                               gdk_pixbuf_get_width(current_pixbuf),
                                                               gdk_pixbuf_get_height(current_pixbuf));

            if (new_pixbuf) {
                g_object_unref(current_pixbuf);
                current_pixbuf = new_pixbuf;
                push_undo_state();
                gtk_widget_queue_draw(drawing_area);
            }

            cairo_destroy(cr);
            cairo_surface_destroy(surface);
        }
    }

    gtk_widget_destroy(dialog);
}

static void push_undo_state(void) {
    // Clear redo states
    for (int i = undo_stack.current + 1; i <= undo_stack.top; i++) {
        if (undo_stack.states[i]) {
            g_object_unref(undo_stack.states[i]);
            undo_stack.states[i] = NULL;
        }
    }

    // Shift states if we're at max capacity
    if (undo_stack.current >= MAX_UNDO_STACK - 1) {
        g_object_unref(undo_stack.states[0]);
        for (int i = 0; i < undo_stack.current; i++) {
            undo_stack.states[i] = undo_stack.states[i + 1];
        }
        undo_stack.current--;
    }

    // Add new state
    undo_stack.current++;
    undo_stack.top = undo_stack.current;
    if (current_pixbuf) {
        if (undo_stack.states[undo_stack.current]) {
            g_object_unref(undo_stack.states[undo_stack.current]);
        }
        undo_stack.states[undo_stack.current] = gdk_pixbuf_copy(current_pixbuf);
    }

    // Update button sensitivity
    gtk_widget_set_sensitive(undo_button, undo_stack.current > 0);
    gtk_widget_set_sensitive(redo_button, undo_stack.current < undo_stack.top);
}

static void undo(void) {
    if (undo_stack.current > 0) {
        undo_stack.current--;
        if (current_pixbuf) {
            g_object_unref(current_pixbuf);
        }
        current_pixbuf = gdk_pixbuf_copy(undo_stack.states[undo_stack.current]);
        gtk_widget_queue_draw(drawing_area);

        // Update button sensitivity
        gtk_widget_set_sensitive(undo_button, undo_stack.current > 0);
        gtk_widget_set_sensitive(redo_button, undo_stack.current < undo_stack.top);
    }
}

static void redo(void) {
    if (undo_stack.current < undo_stack.top) {
        undo_stack.current++;
        if (current_pixbuf) {
            g_object_unref(current_pixbuf);
        }
        current_pixbuf = gdk_pixbuf_copy(undo_stack.states[undo_stack.current]);
        gtk_widget_queue_draw(drawing_area);

        // Update button sensitivity
        gtk_widget_set_sensitive(undo_button, undo_stack.current > 0);
        gtk_widget_set_sensitive(redo_button, undo_stack.current < undo_stack.top);
    }
}

static void perform_crop(void) {
    if (!current_pixbuf) return;
    
    // Ensure valid crop coordinates
    int x = MIN(crop_start_x, crop_end_x);
    int y = MIN(crop_start_y, crop_end_y);
    int width = abs(crop_end_x - crop_start_x);
    int height = abs(crop_end_y - crop_start_y);
    
    // Ensure crop region is within image bounds
    x = CLAMP(x, 0, gdk_pixbuf_get_width(current_pixbuf));
    y = CLAMP(y, 0, gdk_pixbuf_get_height(current_pixbuf));
    width = CLAMP(width, 1, gdk_pixbuf_get_width(current_pixbuf) - x);
    height = CLAMP(height, 1, gdk_pixbuf_get_height(current_pixbuf) - y);
    
    // Create new cropped pixbuf
    GdkPixbuf *cropped = gdk_pixbuf_new_subpixbuf(current_pixbuf, x, y, width, height);
    if (cropped) {
        // First store the old pixbuf
        GdkPixbuf *old_pixbuf = current_pixbuf;
        
        // Set the new cropped pixbuf
        current_pixbuf = cropped;
        
        // Now push the state (after we've made the change)
        push_undo_state();
        
        // Free the old pixbuf
        g_object_unref(old_pixbuf);
        
        // Reset crop coordinates
        crop_start_x = crop_start_y = crop_end_x = crop_end_y = 0;
        is_selecting = FALSE;
        
        // Update the display
        gtk_widget_queue_draw(drawing_area);
        
        // Disable crop button until new selection is made
        gtk_widget_set_sensitive(crop_button, FALSE);
    }
}

static void on_mode_changed(GtkComboBox *combo, gpointer data) {
    int active = gtk_combo_box_get_active(combo);
    
    switch (active) {
        case MODE_DRAW:
            is_text_mode = FALSE;
            is_crop_mode = FALSE;
            if (gtk_widget_get_realized(drawing_area)) {
                GdkWindow *window = gtk_widget_get_window(drawing_area);
                GdkCursor *cursor = gdk_cursor_new_from_name(gdk_display_get_default(), "crosshair");
                gdk_window_set_cursor(window, cursor);
                g_object_unref(cursor);
            }
            break;
            
        case MODE_TEXT:
            is_text_mode = TRUE;
            is_crop_mode = FALSE;
            if (gtk_widget_get_realized(drawing_area)) {
                GdkWindow *window = gtk_widget_get_window(drawing_area);
                GdkCursor *cursor = gdk_cursor_new_from_name(gdk_display_get_default(), "text");
                gdk_window_set_cursor(window, cursor);
                g_object_unref(cursor);
            }
            break;
            
        case MODE_CROP:
            is_text_mode = FALSE;
            is_crop_mode = TRUE;
            if (gtk_widget_get_realized(drawing_area)) {
                GdkWindow *window = gtk_widget_get_window(drawing_area);
                GdkCursor *cursor = gdk_cursor_new_from_name(gdk_display_get_default(), "crosshair");
                gdk_window_set_cursor(window, cursor);
                g_object_unref(cursor);
            }
            break;
    }
}

// Modified tooltip handler
static gboolean on_mode_combo_tooltip(GtkWidget *widget, gint x, gint y,
                                    gboolean keyboard_mode, GtkTooltip *tooltip,
                                    gpointer data) {
    GtkComboBox *combo = GTK_COMBO_BOX(widget);
    GtkTreeModel *model = gtk_combo_box_get_model(combo);
    GtkTreeIter iter;
    
    if (popup_visible && popup_menu) {
        // Get the tree view from the popup
        GtkWidget *tree_view = gtk_bin_get_child(GTK_BIN(popup_menu));
        if (GTK_IS_TREE_VIEW(tree_view)) {
            gint wx, wy;
            GtkTreePath *path = NULL;
            
            // Convert coordinates
            gtk_widget_translate_coordinates(widget, tree_view, x, y, &wx, &wy);
            
            if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tree_view), 
                                             wx, wy, &path, NULL, NULL, NULL)) {
                if (gtk_tree_model_get_iter(model, &iter, path)) {
                    gchar *tooltip_text;
                    gtk_tree_model_get(model, &iter, 1, &tooltip_text, -1);
                    gtk_tooltip_set_text(tooltip, tooltip_text);
                    g_free(tooltip_text);
                    gtk_tree_path_free(path);
                    return TRUE;
                }
                if (path) {
                    gtk_tree_path_free(path);
                }
            }
        }
    } else {
        // Show tooltip for selected item when popup is closed
        gint active = gtk_combo_box_get_active(combo);
        if (active >= 0 && gtk_tree_model_iter_nth_child(model, &iter, NULL, active)) {
            gchar *tooltip_text;
            gtk_tree_model_get(model, &iter, 1, &tooltip_text, -1);
            gtk_tooltip_set_text(tooltip, tooltip_text);
            g_free(tooltip_text);
            return TRUE;
        }
    }
    
    return FALSE;
}

// Add these signal handlers
static void on_popup_shown(GtkWidget *popup_window, gpointer data) {
    popup_visible = TRUE;
    popup_menu = popup_window;
}

static void on_popup_hidden(GtkWidget *popup_window, gpointer data) {
    popup_visible = FALSE;
    popup_menu = NULL;
}

// Handler for menu item activation
static void on_menu_item_activate(GtkMenuItem *item, gpointer data) {
    int mode = GPOINTER_TO_INT(data);
    current_mode = mode;
    
    // Update the button's icon
    GtkWidget *image = gtk_button_get_image(GTK_BUTTON(mode_combo));
    if (image && GTK_IS_IMAGE(image)) {
        gtk_image_set_from_icon_name(GTK_IMAGE(image), 
                                   mode_info[mode].icon_name, 
                                   GTK_ICON_SIZE_SMALL_TOOLBAR);  // Use consistent icon size
    }
    gtk_widget_set_tooltip_text(mode_combo, mode_info[mode].tooltip);
    
    // Update the application mode
    switch (mode) {
        case MODE_DRAW:
            is_text_mode = FALSE;
            is_crop_mode = FALSE;
            break;
        case MODE_TEXT:
            is_text_mode = TRUE;
            is_crop_mode = FALSE;
            break;
        case MODE_CROP:
            is_text_mode = FALSE;
            is_crop_mode = TRUE;
            break;
    }
    
    // Update cursor
    if (gtk_widget_get_realized(drawing_area)) {
        GdkWindow *window = gtk_widget_get_window(drawing_area);
        GdkCursor *cursor = gdk_cursor_new_from_name(gdk_display_get_default(),
            is_text_mode ? "text" : "crosshair");
        gdk_window_set_cursor(window, cursor);
        g_object_unref(cursor);
    }
}

// Handler for combo box clicked
static gboolean on_combo_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button == 1) {  // Left click
        gtk_menu_popup_at_widget(GTK_MENU(mode_menu), 
                               widget,
                               GDK_GRAVITY_SOUTH_WEST,
                               GDK_GRAVITY_NORTH_WEST,
                               (GdkEvent*)event);
        return TRUE;
    }
    return FALSE;
} 