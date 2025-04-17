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
gint pen_width = 5;
gboolean is_text_mode = FALSE;
gchar *current_font = "Sans 12";
GdkRGBA text_color = {0.0, 0.0, 0.0, 1.0}; // Default black
GtkWidget *color_button = NULL; // Add color button as global variable

// Function declarations
static void load_image_from_file(const gchar *filename);
static void load_image_from_clipboard();
static void save_image(const gchar *filename);
static void draw_on_surface(cairo_t *cr, gdouble x, gdouble y);
static void update_drawing_area();
static void add_text_at_position(gdouble x, gdouble y);

// Callback functions
static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    if (current_pixbuf) {
        gdk_cairo_set_source_pixbuf(cr, current_pixbuf, 0, 0);
        cairo_paint(cr);
    }
    return FALSE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button == GDK_BUTTON_PRIMARY) {
        if (is_text_mode) {
            add_text_at_position(event->x, event->y);
            return TRUE;
        }
        
        is_drawing = TRUE;
        last_x = event->x;
        last_y = event->y;
        
        // Create a new surface for drawing
        cairo_surface_t *temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                                 gdk_pixbuf_get_width(current_pixbuf),
                                                                 gdk_pixbuf_get_height(current_pixbuf));
        cairo_t *cr = cairo_create(temp_surface);
        
        // Draw the current image
        gdk_cairo_set_source_pixbuf(cr, current_pixbuf, 0, 0);
        cairo_paint(cr);
        
        // Draw the new point
        cairo_set_source_rgba(cr, current_color.red, current_color.green, current_color.blue, current_color.alpha);
        cairo_set_line_width(cr, pen_width);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_move_to(cr, event->x, event->y);
        cairo_line_to(cr, event->x, event->y);
        cairo_stroke(cr);
        
        // Update the current pixbuf
        cairo_surface_flush(temp_surface);
        GdkPixbuf *new_pixbuf = gdk_pixbuf_get_from_surface(temp_surface, 0, 0,
                                                           gdk_pixbuf_get_width(current_pixbuf),
                                                           gdk_pixbuf_get_height(current_pixbuf));
        
        if (new_pixbuf) {
            g_object_unref(current_pixbuf);
            current_pixbuf = new_pixbuf;
        }
        
        cairo_destroy(cr);
        cairo_surface_destroy(temp_surface);
        update_drawing_area();
    }
    return TRUE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button == GDK_BUTTON_PRIMARY) {
        is_drawing = FALSE;
    }
    return TRUE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    if (is_drawing && !is_text_mode) {
        cairo_t *cr = cairo_create(surface);
        draw_on_surface(cr, event->x, event->y);
        cairo_destroy(cr);
        update_drawing_area();
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
    const gchar *new_font = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(button));
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

static void on_text_mode_toggled(GtkToggleButton *button, gpointer data) {
    const gchar *label = gtk_button_get_label(GTK_BUTTON(button));
    
    if (g_strcmp0(label, "Text Mode") == 0) {
        is_text_mode = gtk_toggle_button_get_active(button);
    } else {
        is_text_mode = !gtk_toggle_button_get_active(button);
    }
    
    // Set appropriate cursor
    GdkWindow *window = gtk_widget_get_window(drawing_area);
    if (window && gtk_widget_get_realized(drawing_area)) {
        GdkCursor *cursor = gdk_cursor_new_from_name(gdk_display_get_default(), 
                                                    is_text_mode ? "text" : "crosshair");
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
    GtkWidget *text_mode_button;
    GtkWidget *draw_mode_button;
    GtkWidget *mode_box;

    gtk_init(&argc, &argv);

    // Create main window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Image Annotator");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Create main container
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // Create toolbar
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    // Create buttons and controls
    open_button = gtk_button_new_with_label("Open");
    g_signal_connect(open_button, "clicked", G_CALLBACK(on_open_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), open_button, FALSE, FALSE, 0);

    save_button = gtk_button_new_with_label("Save");
    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), save_button, FALSE, FALSE, 0);

    // Add the new Copy to Clipboard button
    GtkWidget *copy_button = gtk_button_new_with_label("Copy to Clipboard");
    g_signal_connect(copy_button, "clicked", G_CALLBACK(on_copy_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), copy_button, FALSE, FALSE, 0);

    color_button = gtk_color_button_new();
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(color_button), &current_color);
    g_signal_connect(color_button, "color-set", G_CALLBACK(on_color_set), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), color_button, FALSE, FALSE, 0);

    // Initialize colors
    current_color.red = 1.0;
    current_color.green = 0.0;
    current_color.blue = 0.0;
    current_color.alpha = 1.0;

    text_color.red = 1.0;
    text_color.green = 0.0;
    text_color.blue = 0.0;
    text_color.alpha = 1.0;

    // Initialize current_font
    current_font = g_strdup("Sans 12");

    pen_width_spin = gtk_spin_button_new_with_range(1, 50, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pen_width_spin), pen_width);
    g_signal_connect(pen_width_spin, "value-changed", G_CALLBACK(on_pen_width_changed), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), pen_width_spin, FALSE, FALSE, 0);

    font_button = gtk_font_button_new();
    gtk_font_button_set_font_name(GTK_FONT_BUTTON(font_button), current_font);
    g_signal_connect(font_button, "font-set", G_CALLBACK(on_font_set), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), font_button, FALSE, FALSE, 0);

    // Create mode selection box
    mode_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(hbox), mode_box, FALSE, FALSE, 0);

    // Create drawing mode button
    draw_mode_button = gtk_radio_button_new_with_label(NULL, "Drawing Mode");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(draw_mode_button), TRUE);
    g_signal_connect(draw_mode_button, "toggled", G_CALLBACK(on_text_mode_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(mode_box), draw_mode_button, FALSE, FALSE, 0);

    // Create text mode button
    text_mode_button = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(draw_mode_button), "Text Mode");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(text_mode_button), FALSE);
    g_signal_connect(text_mode_button, "toggled", G_CALLBACK(on_text_mode_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(mode_box), text_mode_button, FALSE, FALSE, 0);

    // Create drawing area
    drawing_area = gtk_drawing_area_new();
    gtk_box_pack_start(GTK_BOX(vbox), drawing_area, TRUE, TRUE, 0);
    gtk_widget_set_events(drawing_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    
    // Connect signals
    g_signal_connect(drawing_area, "realize", G_CALLBACK(on_drawing_area_realize), NULL);
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect(drawing_area, "button-press-event", G_CALLBACK(on_button_press), NULL);
    g_signal_connect(drawing_area, "button-release-event", G_CALLBACK(on_button_release), NULL);
    g_signal_connect(drawing_area, "motion-notify-event", G_CALLBACK(on_motion_notify), NULL);

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
}

static void load_image_from_clipboard() {
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    GdkPixbuf *pixbuf = gtk_clipboard_wait_for_image(clipboard);
    
    if (pixbuf) {
        original_pixbuf = pixbuf;
        current_pixbuf = gdk_pixbuf_copy(original_pixbuf);
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                           gdk_pixbuf_get_width(current_pixbuf),
                                           gdk_pixbuf_get_height(current_pixbuf));
        gtk_widget_set_size_request(drawing_area,
                                  gdk_pixbuf_get_width(current_pixbuf),
                                  gdk_pixbuf_get_height(current_pixbuf));
        update_drawing_area();
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

    // Create the dialog
    dialog = gtk_dialog_new_with_buttons("Enter Text",
                                       GTK_WINDOW(gtk_widget_get_toplevel(drawing_area)),
                                       GTK_DIALOG_MODAL,
                                       "_Cancel", GTK_RESPONSE_CANCEL,
                                       "_OK", GTK_RESPONSE_ACCEPT,
                                       NULL);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(content_area), entry);
    gtk_widget_show_all(dialog);

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT) {
        const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
        if (text && *text) {
            // Create a new surface for drawing
            cairo_surface_t *temp_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                                     gdk_pixbuf_get_width(current_pixbuf),
                                                                     gdk_pixbuf_get_height(current_pixbuf));
            cairo_t *cr = cairo_create(temp_surface);
            
            // Draw the current image
            gdk_cairo_set_source_pixbuf(cr, current_pixbuf, 0, 0);
            cairo_paint(cr);
            
            // Parse font string
            gchar *font_name = NULL;
            gdouble font_size = 12.0;
            if (current_font) {
                // Find the last space in the font string
                gchar *last_space = g_strrstr(current_font, " ");
                if (last_space) {
                    // Extract the size (everything after the last space)
                    font_size = g_ascii_strtod(last_space + 1, NULL);
                    // Extract the font name (everything before the last space)
                    font_name = g_strndup(current_font, last_space - current_font);
                } else {
                    font_name = g_strdup(current_font);
                }
            }
            
            // Set up text properties
            if (font_name) {
                cairo_select_font_face(cr, font_name, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                g_free(font_name);
            } else {
                cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            }
            cairo_set_font_size(cr, font_size);
            
            // Set text color
            cairo_set_source_rgba(cr, text_color.red, text_color.green, text_color.blue, text_color.alpha);
            
            // Draw the text
            cairo_move_to(cr, x, y);
            cairo_show_text(cr, text);
            
            // Update the current pixbuf
            cairo_surface_flush(temp_surface);
            GdkPixbuf *new_pixbuf = gdk_pixbuf_get_from_surface(temp_surface, 0, 0,
                                                               gdk_pixbuf_get_width(current_pixbuf),
                                                               gdk_pixbuf_get_height(current_pixbuf));
            
            if (new_pixbuf) {
                g_object_unref(current_pixbuf);
                current_pixbuf = new_pixbuf;
            }
            
            cairo_destroy(cr);
            cairo_surface_destroy(temp_surface);
            update_drawing_area();
        }
    }

    gtk_widget_destroy(dialog);
} 