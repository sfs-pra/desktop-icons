#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <gtk-layer-shell/gtk-layer-shell.h>

// Enum for list store columns
enum {
    COL_PIXBUF,
    COL_DISPLAY_NAME,
    COL_FILE,
    COL_FILE_INFO,
    NUM_COLS
};

// Target entry for drag and drop
static GtkTargetEntry targets[] = {
    { "text/uri-list", 0, 0 }
};

// Global icon theme
GtkIconTheme *theme;

// Function to remove a row from the list store by file
void remove_row_by_file(GtkListStore *store, GFile *file) {
    GtkTreeIter iter;
    GFile *file_iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

    while (valid) {
        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, COL_FILE, &file_iter, -1);

        if (g_file_equal(file, file_iter)) {
            gtk_list_store_remove(store, &iter);
            return;
        }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }
}

// Function to append a row to the list store from a file
void append_row_from_file(GtkListStore *store, GFile *file) {
    GtkTreeIter iter;
    GFileInfo *file_info;
    GdkPixbuf *pixbuf;
    GAppInfo *app;
    const gchar *display_name = NULL;
    GIcon *icon = NULL;

    GKeyFile *keyfile = g_key_file_new();
    file_info = g_file_query_info(file, "standard::*,owner::user", 0, 0, 0);
    if (g_key_file_load_from_file(keyfile, g_file_get_path(file), G_KEY_FILE_NONE, NULL)) {
        app = (GAppInfo*)g_desktop_app_info_new_from_keyfile(keyfile);
        if (app) {
            display_name = g_app_info_get_display_name(app);
            icon = g_app_info_get_icon(app);
        }
    }

    if (!display_name)
        display_name = g_file_info_get_display_name(file_info);
    if (!icon)
        icon = g_file_info_get_icon(file_info);

    pixbuf = gtk_icon_info_load_icon(
            gtk_icon_theme_lookup_by_gicon(theme, icon, 48, 0), 0);

    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
            COL_PIXBUF, pixbuf,
            COL_DISPLAY_NAME, display_name,
            COL_FILE, file,
            COL_FILE_INFO, file_info,
            -1
            );
}

// Callback function for file created signal
static void file_changed_cb(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent evtype, gpointer user_data) {
    GtkListStore *store = GTK_LIST_STORE(user_data);
    switch (evtype) {
        case G_FILE_MONITOR_EVENT_DELETED:
            remove_row_by_file(store, file);
            break;
        case G_FILE_MONITOR_EVENT_CREATED:
            append_row_from_file(store, file);
            break;
    }
}

// Callback function for drop data received signal
static void drop_data_cb(GtkWidget* self, GdkDragContext* context, gint x, gint y, GtkSelectionData* data, guint info, guint time, gpointer user_data) {
    const gchar *desktop_path;
    gchar **uris;
    GFile *file, *dir_file;
    GFileInfo *file_info;

    desktop_path = g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
    dir_file = g_file_new_for_path(desktop_path);

    uris = gtk_selection_data_get_uris(data);
    for (gchar **uri = uris; *uri != 0; uri++) {
        file = g_file_new_for_uri(*uri);
        file_info = g_file_query_info(file, "standard::*,owner::user", 0, 0, 0);

        g_file_copy(file, g_file_get_child(dir_file, g_file_info_get_name(file_info)), G_FILE_COPY_NONE, 0, 0, 0, 0);
    }
    gtk_drag_finish(context, TRUE, FALSE, time);
}

// Callback function for drag data get signal
static void drag_data_cb(GtkWidget* widget, GdkDragContext* context, GtkSelectionData* data, guint info, guint time, gpointer user_data) {
    GList *selected_items, *iter;
    GtkTreeModel *model = GTK_TREE_MODEL(user_data);
    gchar **uris;
    int index = 0;

    selected_items = gtk_icon_view_get_selected_items(GTK_ICON_VIEW(widget));
    uris = g_new(gchar*, g_list_length(selected_items) + 1);
    for (iter = selected_items; iter != NULL; iter = iter->next) {
        GtkTreeIter tree_iter;
        GFile *file;
        gtk_tree_model_get_iter(model, &tree_iter, (GtkTreePath *)iter->data);
        gtk_tree_model_get(model, &tree_iter, COL_FILE, &file, -1);
        uris[index++] = g_file_get_uri(file);
    }
    gtk_selection_data_set_uris(data, uris);
}

// Function to create a desktop list store
static GtkListStore *create_desktop_list(void) {
    GtkTreeIter iter;
    GtkListStore *store;
    GDir *dir;
    GFile *file, *dir_file;
    GFileMonitor *monitor;
    const gchar *desktop_path, *file_name;

    desktop_path = g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);

    store = gtk_list_store_new(NUM_COLS,
            GDK_TYPE_PIXBUF,
            G_TYPE_STRING,
            G_TYPE_FILE,
            G_TYPE_FILE_INFO);

    dir = g_dir_open(desktop_path, 0, 0);
    dir_file = g_file_new_for_path(desktop_path);
    monitor = g_file_monitor_directory(dir_file, G_FILE_MONITOR_NONE, NULL, NULL);

    while ((file_name = g_dir_read_name(dir))) {
        file = g_file_new_for_path(g_build_filename(desktop_path, file_name, NULL));
        append_row_from_file(store, file);
    }

    g_signal_connect(monitor, "changed", G_CALLBACK(file_changed_cb), store);

    return GTK_LIST_STORE(store);
}

// Function to launch the default app or the app for a file
static void launch_default_or_app_for_file(GFile *desktop_file) {
    GAppInfo *app;
    GKeyFile *keyfile = g_key_file_new();
    char* file_uri;

    if (g_key_file_load_from_file(keyfile, g_file_get_path(desktop_file), G_KEY_FILE_NONE, NULL)) {
        app = (GAppInfo*)g_desktop_app_info_new_from_keyfile(keyfile);
        if (app) {
            GAppLaunchContext* app_context = g_app_launch_context_new();
            g_app_info_launch(app, NULL, app_context, NULL);
            g_clear_object(&app_context);
            return;
        }
    }
    // Not a .desktop, falling back to xdg open
    file_uri = g_file_get_uri(desktop_file);
    g_app_info_launch_default_for_uri(file_uri, 0, 0);
}

// Callback function for item activated signal
static void activate_cb(GtkIconView *icon_view, GtkTreePath *tree_path, gpointer user_data) {
    GtkListStore *store;
    GtkTreeIter iter;
    GFile *file;
    store = GTK_LIST_STORE(user_data);

    gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, tree_path);

    gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, COL_FILE, &file, -1);

    launch_default_or_app_for_file(file);
}

static char *get_font_color() {
    const char *env = getenv("DIRICONS_FONT_COLOR");
    if(!env)
	return NULL;
    
    char *str = (char *)malloc(255);
    if(!str)
	return NULL;
	
    snprintf(str, 254, "* { background-color: rgba(0, 0, 0, 0); color: %s; }", env);
    return str;
}


// Function to create a new window
static GtkWidget *window_new(GtkApplication *app, GtkListStore *model) {
    GtkWidget *window, *icon_view;

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Window");

    gtk_layer_init_for_window(GTK_WINDOW(window));
    gtk_layer_set_namespace(GTK_WINDOW(window), "desktop-icons");
    gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_BOTTOM);

    for (int anchor = 0; anchor < 4; anchor++)
        gtk_layer_set_anchor(GTK_WINDOW(window), anchor, 1);

    gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, 20);

    icon_view = gtk_icon_view_new_with_model(GTK_TREE_MODEL(model));

    // Replace deprecated function with CSS
    GtkCssProvider *provider = gtk_css_provider_new();
    
    char *font_color = get_font_color();
    if(font_color)
    {
        gtk_css_provider_load_from_data(provider,font_color, -1, NULL);
	free(font_color);
    }
    else{
	gtk_css_provider_load_from_data(provider,
        "* { background-color: rgba(0, 0, 0, 0); }", -1, NULL);
    }
        
    gtk_style_context_add_provider(gtk_widget_get_style_context(icon_view),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_style_context_add_provider(gtk_widget_get_style_context(window),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    gtk_icon_view_set_item_width(GTK_ICON_VIEW(icon_view), 96);
    gtk_icon_view_set_selection_mode(GTK_ICON_VIEW(icon_view),
            GTK_SELECTION_MULTIPLE);
    gtk_icon_view_set_text_column(GTK_ICON_VIEW(icon_view), COL_DISPLAY_NAME);
    gtk_icon_view_set_pixbuf_column(GTK_ICON_VIEW(icon_view), COL_PIXBUF);

    gtk_drag_dest_set(icon_view, GTK_DEST_DEFAULT_ALL, targets, G_N_ELEMENTS(targets), GDK_ACTION_COPY);
    gtk_icon_view_enable_model_drag_source(
            GTK_ICON_VIEW(icon_view),
            GDK_BUTTON1_MASK,
            targets,
            G_N_ELEMENTS(targets),
            GDK_ACTION_COPY
            );

    g_signal_connect(icon_view, "item-activated", G_CALLBACK(activate_cb), model);
    g_signal_connect(icon_view, "drag-data-received", G_CALLBACK(drop_data_cb), model);
    g_signal_connect(icon_view, "drag-data-get", G_CALLBACK(drag_data_cb), model);

    gtk_container_add(GTK_CONTAINER(window), icon_view);
    gtk_widget_grab_focus(icon_view);

    gtk_widget_show_all(window);

    return window;
}

// Activate callback for the application
static void activate(GtkApplication* app, gpointer user_data) {
    GtkWidget *window, *icon_view;
    GtkListStore *model;

    GdkDisplay *display;

    display = gdk_display_get_default();

    theme = gtk_icon_theme_get_default();
    model = create_desktop_list();

    GdkMonitor *monitor;

    for (int i = 0; i < gdk_display_get_n_monitors(display); i++) {
        monitor = gdk_display_get_monitor(display, i);
        window = window_new(app, model);
        gtk_layer_set_monitor(GTK_WINDOW(window), monitor);
    }
}

// Main function
int main(int argc, char **argv) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("dev.orangerot.dicons", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
