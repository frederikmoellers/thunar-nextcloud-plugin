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

#include <exo/exo.h>
#include "tnp-provider.h"

G_MODULE_EXPORT void thunar_extension_initialize(ThunarxProviderPlugin* plugin);
G_MODULE_EXPORT void thunar_extension_shutdown();
G_MODULE_EXPORT void thunar_extension_list_types(const GType** types, gint* n_types);

static GType type_list[1];

void thunar_extension_initialize(ThunarxProviderPlugin* plugin)
{
    const gchar* mismatch;

    /* verify that the thunarx versions are compatible */
    mismatch = thunarx_check_version (THUNARX_MAJOR_VERSION, THUNARX_MINOR_VERSION, THUNARX_MICRO_VERSION);
    if (G_UNLIKELY (mismatch != NULL))
    {
        g_warning ("Version mismatch: %s", mismatch);
        return;
    }

    #ifdef G_ENABLE_DEBUG
    g_message ("Initializing thunar-nextcloud-plugin extension");
    #endif

    /* register the types provided by this plugin */
    tnp_provider_register_type(plugin);

    /* setup the plugin provider type list */
    type_list[0] = TNP_TYPE_PROVIDER;
}

void thunar_extension_shutdown()
{
    #ifdef G_ENABLE_DEBUG
    g_message ("Shutting down thunar-nextcloud-plugin extension");
    #endif
}

void thunar_extension_list_types (const GType** types, gint* n_types)
{
    *types = type_list;
    *n_types = G_N_ELEMENTS(type_list);
}