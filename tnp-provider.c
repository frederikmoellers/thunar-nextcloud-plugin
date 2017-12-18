/* vi:set et ai sw=4 sts=4 ts=4: */
/*-
 * Copyright (c) 2017 Frederik Möllers <frederik@die-sinlosen.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General 
 * Public License along with this library; if not, write to the 
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <libxfce4util/libxfce4util.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "tnp-provider.h"

// "SHARE:CANNOTSHAREROOT:" + path + "\n\0"
#define SOCKET_BUFFER_SIZE PATH_MAX+24
#define MAX_SYNCED_DIRS 128

// forward declarations
static gboolean handle_responses(const char const* share_path);
static void tnp_provider_menu_provider_init (ThunarxMenuProviderIface* iface);
static void tnp_provider_finalize (GObject* object);
static GList* tnp_provider_get_file_actions(ThunarxMenuProvider* menu_provider,
                                            GtkWidget* window,
                                            GList* files);
static void tnp_provider_execute(TnpProvider* tnp_provider, GPid (*action) (const gchar* folder,
                                                                            GList* files,
                                                                            GtkWidget* window,
                                                                            GError** error),
                                 GtkWidget* window,
                                 const gchar* folder,
                                 GList* files,
                                 const gchar* error_message);
static void tnp_provider_child_watch(GPid pid, gint status, gpointer user_data);
static void tnp_provider_child_watch_destroy(gpointer user_data);

struct _TnpProviderClass
{
    GObjectClass __parent__;
};

struct _TnpProvider
{
    GObject __parent__;

    #if !GTK_CHECK_VERSION(2,9,0)
    /* 
    * GTK+ 2.9.0 and above provide an icon-name property
    * for GtkActions, so we don't need the icon factory.
    */
    GtkIconFactory *icon_factory;
    #endif

    // taken from thunar-archive-plugin and kept just to be safe
    gint            child_watch_id;
};

static GQuark tnp_action_files_quark;
#if THUNARX_CHECK_VERSION(0,4,1)
static GQuark tnp_action_folder_quark;
#endif
static GQuark tnp_action_provider_quark;

THUNARX_DEFINE_TYPE_WITH_CODE(TnpProvider,
                              tnp_provider,
                              G_TYPE_OBJECT,
                              THUNARX_IMPLEMENT_INTERFACE (THUNARX_TYPE_MENU_PROVIDER,
                                                           tnp_provider_menu_provider_init));


static int nextcloud_client_socket = -1;
char synced_dirs[MAX_SYNCED_DIRS][PATH_MAX] = {0};
char socket_buffer[SOCKET_BUFFER_SIZE] = {0};
size_t socket_buffer_index = 0;
struct timeval socket_timeout = {.tv_sec = 0, .tv_usec = 500000};

/**
 * Disconnects the nextcloud_client_socket connection. After calling this
 * function, nextcloud_client_socket is -1.
 */
static void disconnect_socket()
{
    size_t i;
    if(nextcloud_client_socket >= 0)
    {
        close(nextcloud_client_socket);
    }
    nextcloud_client_socket = -1;
    // delete list of synced dirs
    for(i = 0; i < MAX_SYNCED_DIRS; i++)
    {
        synced_dirs[i][0] = '\0';
    }
    // clear socket buffer
    socket_buffer_index = 0;
    socket_buffer[0] = '\0';
}

/**
 * Connect to the unix socket of the Nextcloud client
 * If the connection is successful, the variable nextcloud_client_socket will
 * hold the file descriptor of the connected socket. If the connection failed,
 * the variable will be -1.
 * @return -1 if the connection failed, 0 if it succeeded
 */
static int connect_socket()
{
    // taken from the unix(7) manpage
    struct sockaddr_un addr;
    int ret;
    char const* xdg_runtime_dir;
    static const char const* nextcloud_client_socket_relpath = "/Nextcloud/socket";


    // if socket is still connected, abort
    if(nextcloud_client_socket >= 0)
    {
        return 0;
    }

    /* Create local socket. */
    nextcloud_client_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (nextcloud_client_socket == -1)
    {
        #ifdef G_ENABLE_DEBUG
        g_message("socket() failed! %s", strerror(errno));
        #endif
        return -1;
    }

    if(setsockopt(nextcloud_client_socket,
                  SOL_SOCKET,
                  SO_RCVTIMEO,
                  (char*) &socket_timeout,
                  sizeof(socket_timeout)) < 0 ||
       setsockopt(nextcloud_client_socket,
                  SOL_SOCKET,
                  SO_SNDTIMEO,
                  (char*) &socket_timeout,
                  sizeof(socket_timeout)) < 0)
    {
        #ifdef G_ENABLE_DEBUG
        g_message("setsockopt() failed! %s", strerror(errno));
        #endif
        return -1;
    }

    /*
    * For portability clear the whole structure, since some
    * implementations have additional (nonstandard) fields in
    * the structure.
    */
    memset(&addr, 0, sizeof(struct sockaddr_un));

    /* Connect socket to socket address */
    addr.sun_family = AF_UNIX;
    xdg_runtime_dir = g_getenv("XDG_RUNTIME_DIR");
    snprintf(addr.sun_path, strlen(xdg_runtime_dir) + strlen(nextcloud_client_socket_relpath) + 1,
             "%s%s", xdg_runtime_dir, nextcloud_client_socket_relpath);
    ret = connect(nextcloud_client_socket, (const struct sockaddr*) &addr, sizeof(struct sockaddr_un));
    if (ret == -1)
    {
        #ifdef G_ENABLE_DEBUG
        g_message("connect() failed! %s", strerror(errno));
        #endif
        disconnect_socket();
    }
    else
    {
        #ifdef G_ENABLE_DEBUG
        g_message("Connected to '%s'", addr.sun_path);
        #endif
        // handle immediate messages
        handle_responses(NULL);
    }
    return ret;
}

/**
 * Handle one or many messages from the nextcloud client if there is one on the
 * socket. If "share_path" is non-NULL, it will block until a "SHARE:"
 * message is received whose tail matches the share_path. Will return TRUE if
 * "share_path" was NULL or if a "SHARE:OK:" message was received. Will return
 * FALSE otherwise.
 * @param share_path Path to wait for
 */
static gboolean handle_responses(const char const* share_path)
{
    char* newline_pos;
    int read_status;
    size_t i;
    char realpath_buffer[PATH_MAX];
    int recv_flag = share_path == NULL ? MSG_DONTWAIT : 0;
    gboolean retval = TRUE;
    // make sure the socket is connected
    if(nextcloud_client_socket == -1)
    {
        #ifdef G_ENABLE_DEBUG
        g_message("Not connected yet!");
        #endif
        if(connect_socket() == -1)
        {
            #ifdef G_ENABLE_DEBUG
            g_message("Socket not connected and connection failed");
            #endif
            return FALSE;
        }
    }
    // repeat as long as there is data on the socket
    do
    {
        // try to fill the buffer
        read_status = recv(nextcloud_client_socket, socket_buffer + socket_buffer_index, SOCKET_BUFFER_SIZE - socket_buffer_index - 1, recv_flag);
        if(read_status > 0)
        {
            socket_buffer_index += read_status;
            socket_buffer[socket_buffer_index] = '\0';
        }
        else if(errno != EAGAIN && errno != EWOULDBLOCK)
        {
            disconnect_socket();
            if(connect_socket() == 0)
                read_status = 1;
            else
            {
                retval = FALSE;
                break;
            }
        }
        // repeat as long as there are complete lines in the buffer
        for(newline_pos = strchr(socket_buffer, '\n'); newline_pos != NULL; newline_pos = strchr(socket_buffer, '\n'))
        {
            // substitute newline for '\0' for easier string handling
            *newline_pos = '\0';
            // if a new directory is synced, add it
            if(strncmp(socket_buffer, "REGISTER_PATH:", strlen("REGISTER_PATH:")) == 0)
            {
                // find first empty entry
                for(i = 0; i < MAX_SYNCED_DIRS && synced_dirs[i][0] != '\0'; i++);
                if(i != MAX_SYNCED_DIRS)
                {
                    char* path = socket_buffer + strlen("REGISTER_PATH:");
                    // dereference directory
                    if(realpath(path, realpath_buffer) == NULL)
                    {
                        #ifdef G_ENABLE_DEBUG
                        g_message("Failed to resolve path: %s", path);
                        #endif
                    }
                    else
                    {
                        // copy dirname
                        strcpy(synced_dirs[i], realpath_buffer);
                        #ifdef G_ENABLE_DEBUG
                        g_message("Added directory: %s", synced_dirs[i]);
                        #endif
                    }
                }
                else
                {
                    #ifdef G_ENABLE_DEBUG
                    g_message("No space left for shared directories!");
                    #endif
                }
            }
            // if a directory is no longer synced, remove it
            else if(strncmp(socket_buffer, "UNREGISTER_PATH:", strlen("UNREGISTER_PATH:")) == 0)
            {
                // find corresponding entry
                for(i = 0; i < MAX_SYNCED_DIRS; i++)
                {
                    if(strcmp(socket_buffer + strlen("UNREGISTER_PATH:"), synced_dirs[i]) == 0)
                    {
                        // remove
                        synced_dirs[i][0] = '\0';
                        #ifdef G_ENABLE_DEBUG
                        g_message("Removed directory: %s", synced_dirs[i]);
                        #endif
                        break;
                    }
                }
            }
            // check if the message relates to the requested directory
            else if(share_path != NULL &&
                    strncmp(socket_buffer, "SHARE:", strlen("SHARE:")) == 0 && // begins with "SHARE:"
                    strcmp(strchr(socket_buffer + 6, ':') + 1, share_path) == 0) // ends with share_path after "SHARE:FOO:"
            {
                if(strncmp(socket_buffer + 6, "NOP:", 4) == 0)
                {
                    #ifdef G_ENABLE_DEBUG
                    g_message("Failed to share path: %s", share_path);
                    g_message("Response: '%s'", socket_buffer);
                    #endif
                    retval = FALSE;
                }
                else
                {
                    #ifdef G_ENABLE_DEBUG
                    g_message("Successfully shared path: %s", share_path);
                    #endif
                    retval = TRUE;
                }
            }
            // all other messsages can be ignored
            // remove line from the buffer
            memmove(socket_buffer, newline_pos + 2, SOCKET_BUFFER_SIZE - (newline_pos - socket_buffer + 2));
            socket_buffer_index -= newline_pos - socket_buffer + 1;
        }
    }
    while(read_status > 0);
    return retval;
}

static void tnp_share_item(GtkAction* action, GtkWidget* window)
{
    GList* files;
    gchar* path;
    gchar* path_unescaped;
    char realpath_buffer[PATH_MAX];
    GtkWidget *dialog;

    /* determine the files associated with the action */
    files = g_object_get_qdata (G_OBJECT (action), tnp_action_files_quark);
    if (G_UNLIKELY (files == NULL || files->next != NULL))
    {
        return;
    }

    // get the file's path
    path = thunarx_file_info_get_uri(files->data);
    path_unescaped = g_filename_from_uri(path, NULL, NULL);
    g_free(path);
    if(realpath(path_unescaped, realpath_buffer) == NULL)
    {
        #ifdef G_ENABLE_DEBUG
        g_message("Failed to resolve path '%s'", path_unescaped);
        #endif
        g_free(path_unescaped);
        return;
    }
    g_free(path_unescaped);

    send(nextcloud_client_socket, "SHARE:", 6, 0);
    send(nextcloud_client_socket, realpath_buffer, strlen(realpath_buffer), 0);
    send(nextcloud_client_socket, "\n", 1, 0);
    if(handle_responses(realpath_buffer) == FALSE)
    {
        /* display an error dialog */
        dialog = gtk_message_dialog_new (GTK_WINDOW (window),
                                         GTK_DIALOG_DESTROY_WITH_PARENT
                                         | GTK_DIALOG_MODAL,
                                         GTK_MESSAGE_ERROR,
                                         GTK_BUTTONS_CLOSE,
                                         "Failed to share item");
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "Failed to share the path '%s'.", realpath_buffer);
        gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);
    }
}

static GList* tnp_provider_get_file_actions(ThunarxMenuProvider* menu_provider,
                                            GtkWidget* window,
                                            GList* files)
{
    gchar* uri_scheme, * path, * path_unescaped;
    char realpath_buffer[PATH_MAX];
    char* tooltip_name_dir = "Share the selected directory via Nextcloud";
    char* tooltip_name_file = "Share the selected file via Nextcloud";
    char* tooltip = tooltip_name_dir;
    size_t i, synced_dir_len;
    gboolean is_synced = FALSE;

    TnpProvider* tnp_provider = TNP_PROVIDER (menu_provider);
    GtkAction* action;
    GClosure* closure;
    GList* actions = NULL;

    // we cannot share more than one item at a time
    if(files->next != NULL)
    {
        return NULL;
    }
    // items must be local
    uri_scheme = thunarx_file_info_get_uri_scheme (files->data);
    if (G_UNLIKELY (strcmp (uri_scheme, "file")))
    {
        g_free (uri_scheme);
        return NULL;
    }
    g_free (uri_scheme);
    // handle pending messages on socket to make sure the list of synced dirs is up to date
    handle_responses(NULL);

    // check if entry is direct descendant of a synced directory
    // i.e. check if the parent is either a synced dir or a descendant
    path = thunarx_file_info_get_parent_uri(files->data);
    path_unescaped = g_filename_from_uri(path, NULL, NULL);
    g_free(path);
    if(realpath(path_unescaped, realpath_buffer) == NULL)
    // dereference the path minus the leading "file://"
    if(realpath(path + 7, realpath_buffer) == NULL)
    {
        g_free(path_unescaped);
        #ifdef G_ENABLE_DEBUG
        g_message("Failed to resolve path: %s", path_unescaped);
        #endif
        return NULL;
    }
    g_free(path_unescaped);
    for(i = 0; i < MAX_SYNCED_DIRS; i++)
    {
        if(*synced_dirs[i] == '\0')
            continue;
        synced_dir_len = strlen(synced_dirs[i]);
        if(strncmp(synced_dirs[i], realpath_buffer, strlen(synced_dirs[i])) == 0)
        {
            is_synced = TRUE;
        }
    }
    if(!is_synced)
    {
        return NULL;
    }

    // select the correct tooltip
    if(!thunarx_file_info_is_directory(files->data))
    {
        tooltip = tooltip_name_file;
    }
    // append the "Share" action
    action = g_object_new (GTK_TYPE_ACTION,
                           "name", "Tnp::extract-here",
                           "label", _("Share via _Nextcloud"),
#if !GTK_CHECK_VERSION(2,9,0)
                           "stock-id", "Nextcloud",
#else
                           "icon-name", "Nextcloud",
#endif
                           "tooltip", tooltip,
                           NULL);
    g_object_set_qdata_full (G_OBJECT (action), tnp_action_files_quark,
                             thunarx_file_info_list_copy (files),
                             (GDestroyNotify) thunarx_file_info_list_free);
    g_object_set_qdata_full (G_OBJECT (action), tnp_action_provider_quark,
                             g_object_ref (G_OBJECT (tnp_provider)),
                             (GDestroyNotify) g_object_unref);
    closure = g_cclosure_new_object (G_CALLBACK (tnp_share_item), G_OBJECT (window));
    g_signal_connect_closure (G_OBJECT (action), "activate", closure, TRUE);
    actions = g_list_append (actions, action);
    return actions;
}










/**
 * No idea what these do
 */
static void tnp_provider_class_init(TnpProviderClass* classname)
{
    GObjectClass* gobject_class;

    /* determine the "tnp-action-files", "tnp-action-folder" and "tnp-action-provider" quarks */
    tnp_action_files_quark = g_quark_from_string("tnp-action-files");
    #if THUNARX_CHECK_VERSION(0,4,1)
    tnp_action_folder_quark = g_quark_from_string("tnp-action-folder");
    #endif
    tnp_action_provider_quark = g_quark_from_string("tnp-action-provider");

    gobject_class = G_OBJECT_CLASS(classname);
    gobject_class->finalize = tnp_provider_finalize;
}

static void tnp_provider_menu_provider_init(ThunarxMenuProviderIface* iface)
{
    iface->get_file_actions = tnp_provider_get_file_actions;
}

static void tnp_provider_init(TnpProvider* tnp_provider)
{
    /* connect to the socket */
    connect_socket();
}

static void tnp_provider_finalize(GObject* object)
{
    TnpProvider* tnp_provider = TNP_PROVIDER(object);
    GSource* source;

    /* give up maintaince of any pending child watch */
    if (G_UNLIKELY (tnp_provider->child_watch_id != 0))
    {
        /* 
        * reset the callback function to g_spawn_close_pid() so the plugin can be
        * safely unloaded and the child will still not become a zombie afterwards.
        */
        source = g_main_context_find_source_by_id (NULL, tnp_provider->child_watch_id);
        g_source_set_callback(source, (GSourceFunc) g_spawn_close_pid, NULL, NULL);
    }

    (*G_OBJECT_CLASS(tnp_provider_parent_class)->finalize)(object);
}

static gboolean tnp_is_parent_writable(ThunarxFileInfo* file_info)
{
    return TRUE;
}