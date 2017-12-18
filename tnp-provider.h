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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __TNP_PROVIDER_H__
#define __TNP_PROVIDER_H__

#include <thunarx/thunarx.h>

G_BEGIN_DECLS;

typedef struct _TnpProviderClass TnpProviderClass;
typedef struct _TnpProvider      TnpProvider;

#define TNP_TYPE_PROVIDER (tnp_provider_get_type())
#define TNP_PROVIDER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), TNP_TYPE_PROVIDER, TnpProvider))
#define TNP_PROVIDER_CLASS(classname) (G_TYPE_CHECK_CLASS_CAST((classname), TNP_TYPE_PROVIDER, TnpProviderClass))
#define TNP_IS_PROVIDER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), TNP_TYPE_PROVIDER))
#define TNP_IS_PROVIDER_CLASS(classname) (G_TYPE_CHECK_CLASS_TYPE((classname), TNP_TYPE_PROVIDER))
#define TNP_PROVIDER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), TNP_TYPE_PROVIDER, TnpProviderClass))

GType tnp_provider_get_type (void) G_GNUC_CONST G_GNUC_INTERNAL;
void tnp_provider_register_type (ThunarxProviderPlugin* plugin) G_GNUC_INTERNAL;

G_END_DECLS;

#endif /* !__TNP_PROVIDER_H__ */
