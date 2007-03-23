/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef HAVE_CONFIG_H
#include "config.h"
#endif

#include "clutter-backend.h"
#include "clutter-private.h"

G_DEFINE_ABSTRACT_TYPE (ClutterBackend,
                        clutter_backend,
                        G_TYPE_OBJECT);

static void
clutter_backend_class_init (ClutterBackendClass *klass)
{

}

static void
clutter_backend_init (ClutterBackend *backend)
{
  backend->events_queue = g_queue_new ();

  backend->button_click_time[0] = backend->button_click_time[1] = 0;
  backend->button_number[0] = backend->button_number[1] = -1;
  backend->button_x[0] = backend->button_x[1] = 0;
  backend->button_y[0] = backend->button_y[1] = 0;

  backend->double_click_time = 250;
  backend->double_click_distance = 5;
}

ClutterActor *
_clutter_backend_get_stage (ClutterBackend *backend)
{
  g_return_val_if_fail (CLUTTER_IS_BACKEND (backend), NULL);

  return CLUTTER_BACKEND_GET_CLASS (backend)->get_stage (backend);
}

void
_clutter_backend_add_options (ClutterBackend *backend,
                              GOptionGroup   *group)
{
  g_return_if_fail (CLUTTER_IS_BACKEND (backend));

  CLUTTER_BACKEND_GET_CLASS (backend)->add_options (backend, group);
}

gboolean
_clutter_backend_pre_parse (ClutterBackend  *backend,
                            GError         **error)
{
  ClutterBackendClass *klass;

  g_return_val_if_fail (CLUTTER_IS_BACKEND (backend), FALSE);

  klass = CLUTTER_BACKEND_GET_CLASS (backend);
  if (klass->pre_parse)
    return klass->pre_parse (backend, error);

  return TRUE;
}

gboolean
_clutter_backend_post_parse (ClutterBackend  *backend,
                             GError         **error)
{
  ClutterBackendClass *klass;

  g_return_val_if_fail (CLUTTER_IS_BACKEND (backend), FALSE);

  klass = CLUTTER_BACKEND_GET_CLASS (backend);
  if (klass->post_parse)
    return klass->post_parse (backend, error);

  return TRUE;
}

gboolean
_clutter_backend_init_stage (ClutterBackend  *backend,
                             GError         **error)
{
  ClutterBackendClass *klass;

  g_return_val_if_fail (CLUTTER_IS_BACKEND (backend), FALSE);

  klass = CLUTTER_BACKEND_GET_CLASS (backend);
  if (klass->init_stage)
    return klass->init_stage (backend, error);

  return TRUE;
}

void
_clutter_backend_init_events (ClutterBackend *backend)
{
  ClutterBackendClass *klass;

  g_return_if_fail (CLUTTER_IS_BACKEND (backend));

  klass = CLUTTER_BACKEND_GET_CLASS (backend);
  if (klass->init_events)
    klass->init_events (backend);
}

void
_clutter_backend_init_features (ClutterBackend *backend)
{
  ClutterBackendClass *klass;

  g_return_if_fail (CLUTTER_IS_BACKEND (backend));

  klass = CLUTTER_BACKEND_GET_CLASS (backend);
  if (klass->init_features)
    klass->init_features (backend);
}

/**
 * clutter_get_default_backend:
 *
 * FIXME
 *
 * Return value: the default backend. You should not ref or
 *   unref the returned object
 * 
 * Since: 0.4
 */
ClutterBackend *
clutter_get_default_backend (void)
{
  ClutterMainContext *clutter_context;

  clutter_context = clutter_context_get_default ();

  return clutter_context->backend;
}

/**
 * clutter_backend_get_event:
 * @backend: a #ClutterBackend
 *
 * FIXME
 *
 * Return value: the #ClutterEvent removed from the queue
 *
 * Since: 0.4
 */
ClutterEvent *
clutter_backend_get_event (ClutterBackend *backend)
{
  g_return_val_if_fail (CLUTTER_IS_BACKEND (backend), NULL);
    
  _clutter_events_queue (backend);
  return _clutter_event_queue_pop (backend);
}

/**
 * clutter_backend_peek_event:
 * @backend: a #ClutterBackend
 *
 * FIXME
 *
 * Return value: a copy of the first #ClutterEvent in the queue
 *
 * Since: 0.4
 */
ClutterEvent *
clutter_backend_peek_event (ClutterBackend *backend)
{
  g_return_val_if_fail (CLUTTER_IS_BACKEND (backend), NULL);

  return _clutter_event_queue_peek (backend);
}

/**
 * clutter_backend_put_event:
 * @backend: a #ClutterBackend
 * @event: a #ClutterEvent
 *
 * FIXME
 *
 * Since: 0.4
 */
void
clutter_backend_put_event (ClutterBackend *backend,
                           ClutterEvent   *event)
{
  g_return_if_fail (CLUTTER_IS_BACKEND (backend));
  g_return_if_fail (event != NULL);

  _clutter_event_queue_push (backend, clutter_event_copy (event));
}
