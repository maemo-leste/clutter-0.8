/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2007 OpenedHand
 * Copyright (C) 2010 Intel Corporation.
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
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *  Johan Bilien   <johan.bilien@nokia.com>
 *  Neil Roberts   <neil@linux.intel.com>
 */

/**
 * SECTION:clutter-x11-texture-pixmap
 * @Title: ClutterX11TexturePixmap
 * @short_description: A texture which displays the content of an X Pixmap.
 *
 * #ClutterX11TexturePixmap is a class for displaying the content of an
 * X Pixmap as a ClutterActor. Used together with the X Composite extension,
 * it allows to display the content of X Windows inside Clutter.
 *
 * The class uses the GLX_EXT_texture_from_pixmap OpenGL extension
 * (http://people.freedesktop.org/~davidr/GLX_EXT_texture_from_pixmap.txt)
 * if available
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../clutter-marshal.h"
#include "clutter-x11-texture-pixmap.h"
#include "clutter-x11.h"
#include "clutter-backend-x11.h"
#include "clutter-private.h"

#include "cogl/cogl.h"
#include "cogl/winsys/cogl-texture-pixmap-x11.h"

#include <X11/extensions/Xdamage.h>

#if HAVE_XCOMPOSITE
#include <X11/extensions/Xcomposite.h>
#endif

enum
{
  PROP_PIXMAP = 1,
  PROP_PIXMAP_WIDTH,
  PROP_PIXMAP_HEIGHT,
  PROP_DEPTH,
  PROP_AUTO,
  PROP_WINDOW,
  PROP_WINDOW_REDIRECT_AUTOMATIC,
  PROP_WINDOW_MAPPED,
  PROP_DESTROYED,
  PROP_WINDOW_X,
  PROP_WINDOW_Y,
  PROP_WINDOW_OVERRIDE_REDIRECT
};

enum
{
  UPDATE_AREA,
  QUEUE_DAMAGE_REDRAW,

  /* FIXME: Pixmap lost signal? */
  LAST_SIGNAL
};

static ClutterX11FilterReturn
on_x_event_filter (XEvent *xev, ClutterEvent *cev, gpointer data);

static void
clutter_x11_texture_pixmap_update_area_real (ClutterX11TexturePixmap *texture,
                                             gint                     x,
                                             gint                     y,
                                             gint                     width,
                                             gint                     height);
static void
clutter_x11_texture_pixmap_set_mapped (ClutterX11TexturePixmap *texture, gboolean mapped);
static void
clutter_x11_texture_pixmap_destroyed (ClutterX11TexturePixmap *texture);

static guint signals[LAST_SIGNAL] = { 0, };

struct _ClutterX11TexturePixmapPrivate
{
  Window        window;
  Pixmap        pixmap;
  guint         pixmap_width, pixmap_height;
  guint         depth;

  Damage        damage;

  gint          window_x, window_y;

  guint window_redirect_automatic : 1;
  guint window_mapped             : 1;
  guint destroyed                 : 1;
  guint owns_pixmap               : 1;
  guint override_redirect         : 1;
  guint automatic_updates         : 1;
};

static int _damage_event_base = 0;

G_DEFINE_TYPE (ClutterX11TexturePixmap,
               clutter_x11_texture_pixmap,
               CLUTTER_TYPE_TEXTURE);

static gboolean
check_extensions (ClutterX11TexturePixmap *texture)
{
  int                             damage_error;
  ClutterX11TexturePixmapPrivate *priv;
  Display                        *dpy;

  priv = texture->priv;

  if (_damage_event_base)
    return TRUE;

  dpy = clutter_x11_get_default_display();

  if (!XDamageQueryExtension (dpy,
                              &_damage_event_base, &damage_error))
    {
      g_warning ("No Damage extension");
      return FALSE;
    }

  return TRUE;
}

static void
process_damage_event (ClutterX11TexturePixmap *texture,
                      XDamageNotifyEvent *damage_event)
{
  Display *dpy;

  dpy = clutter_x11_get_default_display();

  /* Cogl will deal with updating the texture and subtracting from the
     damage region so we only need to queue a redraw */
  g_signal_emit (texture, signals[QUEUE_DAMAGE_REDRAW],
                 0,
                 damage_event->area.x,
                 damage_event->area.y,
                 damage_event->area.width,
                 damage_event->area.height);
}

static ClutterX11FilterReturn
on_x_event_filter (XEvent *xev, ClutterEvent *cev, gpointer data)
{
  ClutterX11TexturePixmap        *texture;
  ClutterX11TexturePixmapPrivate *priv;

  texture = CLUTTER_X11_TEXTURE_PIXMAP (data);

  g_return_val_if_fail (CLUTTER_X11_IS_TEXTURE_PIXMAP (texture), \
                        CLUTTER_X11_FILTER_CONTINUE);

  priv = texture->priv;

  if (xev->type == _damage_event_base + XDamageNotify)
    {
      XDamageNotifyEvent *dev = (XDamageNotifyEvent*)xev;

      if (dev->damage != priv->damage)
        return CLUTTER_X11_FILTER_CONTINUE;

      process_damage_event (texture, dev);
    }

  return  CLUTTER_X11_FILTER_CONTINUE;
}

static ClutterX11FilterReturn
on_x_event_filter_too (XEvent *xev, ClutterEvent *cev, gpointer data)
{
  ClutterX11TexturePixmap        *texture;
  ClutterX11TexturePixmapPrivate *priv;

  texture = CLUTTER_X11_TEXTURE_PIXMAP (data);

  g_return_val_if_fail (CLUTTER_X11_IS_TEXTURE_PIXMAP (texture), \
                        CLUTTER_X11_FILTER_CONTINUE);

  priv = texture->priv;

  if (xev->xany.window != priv->window)
    return CLUTTER_X11_FILTER_CONTINUE;

  switch (xev->type) {
  case MapNotify:
  case ConfigureNotify:
    clutter_x11_texture_pixmap_sync_window (texture);
    break;
  case UnmapNotify:
    clutter_x11_texture_pixmap_set_mapped (texture, FALSE);
    break;
  case DestroyNotify:
    clutter_x11_texture_pixmap_destroyed (texture);
    break;
  default:
    break;
  }

  return CLUTTER_X11_FILTER_CONTINUE;
}

static void
update_pixmap_damage_object (ClutterX11TexturePixmap *texture)
{
  ClutterX11TexturePixmapPrivate *priv = texture->priv;
  CoglHandle cogl_texture;

  /* If we already have a CoglTexturePixmapX11 then update its
     damage object */
  cogl_texture =
    clutter_texture_get_cogl_texture (CLUTTER_TEXTURE (texture));

  if (cogl_texture && cogl_is_texture_pixmap_x11 (cogl_texture))
    {
      if (priv->damage)
        {
          const CoglTexturePixmapX11ReportLevel report_level =
            COGL_TEXTURE_PIXMAP_X11_DAMAGE_BOUNDING_BOX;
          cogl_texture_pixmap_x11_set_damage_object (cogl_texture,
                                                     priv->damage,
                                                     report_level);
        }
      else
        cogl_texture_pixmap_x11_set_damage_object (cogl_texture, 0, 0);
    }
}

static void
create_damage_resources (ClutterX11TexturePixmap *texture)
{
  ClutterX11TexturePixmapPrivate *priv;
  Display                        *dpy;

  priv = texture->priv;
  dpy = clutter_x11_get_default_display();

  if (!priv->pixmap)
    return;

  clutter_x11_trap_x_errors ();

  priv->damage = XDamageCreate (dpy,
                                priv->pixmap,
                                XDamageReportBoundingBox);

  /* Errors here might occur if the window is already destroyed, we
   * simply skip processing damage and assume that the texture pixmap
   * will be cleaned up by the app when it gets a DestroyNotify.
   */
  XSync (dpy, FALSE);
  clutter_x11_untrap_x_errors ();

  if (priv->damage)
    {
      clutter_x11_add_filter (on_x_event_filter, (gpointer)texture);

      update_pixmap_damage_object (texture);
    }
}

static void
free_damage_resources (ClutterX11TexturePixmap *texture)
{
  ClutterX11TexturePixmapPrivate *priv;
  Display                        *dpy;

  priv = texture->priv;
  dpy = clutter_x11_get_default_display();

  if (priv->damage)
    {
      clutter_x11_trap_x_errors ();
      XDamageDestroy (dpy, priv->damage);
      XSync (dpy, FALSE);
      clutter_x11_untrap_x_errors ();
      priv->damage = None;

      clutter_x11_remove_filter (on_x_event_filter, (gpointer)texture);

      update_pixmap_damage_object (texture);
    }
}

static gboolean
clutter_x11_texture_pixmap_get_paint_volume (ClutterActor       *self,
                                             ClutterPaintVolume *volume)
{
  return clutter_paint_volume_set_from_allocation (volume, self);
}

static void
clutter_x11_texture_pixmap_real_queue_damage_redraw (
                                              ClutterX11TexturePixmap *texture,
                                              gint x,
                                              gint y,
                                              gint width,
                                              gint height)
{
  ClutterX11TexturePixmapPrivate *priv = texture->priv;
  ClutterActor    *self = CLUTTER_ACTOR (texture);
  ClutterActorBox  allocation;
  float            scale_x;
  float            scale_y;
  ClutterVertex    origin;
  ClutterPaintVolume clip;

  /* NB: clutter_actor_queue_clipped_redraw expects a box in the actor's
   * coordinate space so we need to convert from pixmap coordinates to
   * actor coordinates...
   */

  /* Calling clutter_actor_get_allocation_box() is enormously expensive
   * if the actor has an out-of-date allocation, since it triggers
   * a full redraw. clutter_actor_queue_clipped_redraw() would redraw
   * the whole stage anyways in that case, so just go ahead and do
   * it here.
   */
  if (!clutter_actor_has_allocation (self))
    {
      clutter_actor_queue_redraw (self);
      return;
    }

  if (priv->pixmap_width == 0 || priv->pixmap_height == 0)
    return;

  clutter_actor_get_allocation_box (self, &allocation);

  scale_x = (allocation.x2 - allocation.x1) / priv->pixmap_width;
  scale_y = (allocation.y2 - allocation.y1) / priv->pixmap_height;

  _clutter_paint_volume_init_static (self, &clip);

  origin.x = x * scale_x;
  origin.y = y * scale_y;
  origin.z = 0;
  clutter_paint_volume_set_origin (&clip, &origin);
  clutter_paint_volume_set_width (&clip, width * scale_x);
  clutter_paint_volume_set_height (&clip, height * scale_y);

  _clutter_actor_queue_redraw_with_clip (self, 0, &clip);
  clutter_paint_volume_free (&clip);
}

static void
clutter_x11_texture_pixmap_init (ClutterX11TexturePixmap *self)
{
  self->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (self,
                                   CLUTTER_X11_TYPE_TEXTURE_PIXMAP,
                                   ClutterX11TexturePixmapPrivate);

  if (!check_extensions (self))
    {
      /* FIMXE: means display lacks needed extensions for at least auto.
       *        - a _can_autoupdate() method ?
      */
    }

  self->priv->automatic_updates = FALSE;
  self->priv->damage = None;
  self->priv->window = None;
  self->priv->pixmap = None;
  self->priv->pixmap_height = 0;
  self->priv->pixmap_width = 0;
  self->priv->window_redirect_automatic = TRUE;
  self->priv->window_mapped = FALSE;
  self->priv->destroyed = FALSE;
  self->priv->override_redirect = FALSE;
  self->priv->window_x = 0;
  self->priv->window_y = 0;
}

static void
clutter_x11_texture_pixmap_dispose (GObject *object)
{
  ClutterX11TexturePixmap *texture = CLUTTER_X11_TEXTURE_PIXMAP (object);

  free_damage_resources (texture);

  clutter_x11_remove_filter (on_x_event_filter_too, (gpointer)texture);
  clutter_x11_texture_pixmap_set_pixmap (texture, None);

  G_OBJECT_CLASS (clutter_x11_texture_pixmap_parent_class)->dispose (object);
}

static void
clutter_x11_texture_pixmap_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  ClutterX11TexturePixmap  *texture = CLUTTER_X11_TEXTURE_PIXMAP (object);
  ClutterX11TexturePixmapPrivate *priv = texture->priv;

  switch (prop_id)
    {
    case PROP_PIXMAP:
      clutter_x11_texture_pixmap_set_pixmap (texture,
                                             g_value_get_ulong (value));
      break;
    case PROP_AUTO:
      clutter_x11_texture_pixmap_set_automatic (texture,
                                                g_value_get_boolean (value));
      break;
    case PROP_WINDOW:
      clutter_x11_texture_pixmap_set_window (texture,
                                             g_value_get_ulong (value),
                                             priv->window_redirect_automatic);
      break;
    case PROP_WINDOW_REDIRECT_AUTOMATIC:
      {
        gboolean new;
        new = g_value_get_boolean (value);

        /* Change the update mode.. */
        if (new != priv->window_redirect_automatic && priv->window)
          clutter_x11_texture_pixmap_set_window (texture, priv->window, new);

        priv->window_redirect_automatic = new;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_x11_texture_pixmap_get_property (GObject      *object,
                                         guint         prop_id,
                                         GValue       *value,
                                         GParamSpec   *pspec)
{
  ClutterX11TexturePixmap *texture = CLUTTER_X11_TEXTURE_PIXMAP (object);
  ClutterX11TexturePixmapPrivate *priv = texture->priv;

  switch (prop_id)
    {
    case PROP_PIXMAP:
      g_value_set_ulong (value, priv->pixmap);
      break;
    case PROP_PIXMAP_WIDTH:
      g_value_set_uint (value, priv->pixmap_width);
      break;
    case PROP_PIXMAP_HEIGHT:
      g_value_set_uint (value, priv->pixmap_height);
      break;
    case PROP_DEPTH:
      g_value_set_uint (value, priv->depth);
      break;
    case PROP_AUTO:
      g_value_set_boolean (value, priv->automatic_updates);
      break;
    case PROP_WINDOW:
      g_value_set_ulong (value, priv->window);
      break;
    case PROP_WINDOW_REDIRECT_AUTOMATIC:
      g_value_set_boolean (value, priv->window_redirect_automatic);
      break;
    case PROP_WINDOW_MAPPED:
      g_value_set_boolean (value, priv->window_mapped);
      break;
    case PROP_DESTROYED:
      g_value_set_boolean (value, priv->destroyed);
      break;
    case PROP_WINDOW_X:
      g_value_set_int (value, priv->window_x);
      break;
    case PROP_WINDOW_Y:
      g_value_set_int (value, priv->window_y);
      break;
    case PROP_WINDOW_OVERRIDE_REDIRECT:
      g_value_set_boolean (value, priv->override_redirect);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_x11_texture_pixmap_class_init (ClutterX11TexturePixmapClass *klass)
{
  GObjectClass      *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GParamSpec        *pspec;
  ClutterBackend    *default_backend;

  g_type_class_add_private (klass, sizeof (ClutterX11TexturePixmapPrivate));

  actor_class->get_paint_volume = clutter_x11_texture_pixmap_get_paint_volume;

  object_class->dispose      = clutter_x11_texture_pixmap_dispose;
  object_class->set_property = clutter_x11_texture_pixmap_set_property;
  object_class->get_property = clutter_x11_texture_pixmap_get_property;

  klass->update_area         = clutter_x11_texture_pixmap_update_area_real;

  pspec = g_param_spec_ulong ("pixmap",
			      P_("Pixmap"),
			      P_("The X11 Pixmap to be bound"),
			      0, G_MAXULONG,
			      None,
			      G_PARAM_READWRITE);

  g_object_class_install_property (object_class, PROP_PIXMAP, pspec);

  pspec = g_param_spec_uint ("pixmap-width",
                             P_("Pixmap width"),
                             P_("The width of the pixmap bound to this texture"),
                             0, G_MAXUINT,
                             0,
                             G_PARAM_READABLE);

  g_object_class_install_property (object_class, PROP_PIXMAP_WIDTH, pspec);

  pspec = g_param_spec_uint ("pixmap-height",
                             P_("Pixmap height"),
                             P_("The height of the pixmap bound to this texture"),
                             0, G_MAXUINT,
                             0,
                             G_PARAM_READABLE);

  g_object_class_install_property (object_class, PROP_PIXMAP_HEIGHT, pspec);

  pspec = g_param_spec_uint ("pixmap-depth",
                             P_("Pixmap Depth"),
                             P_("The depth (in number of bits) of the pixmap bound to this texture"),
                             0, G_MAXUINT,
                             0,
                             G_PARAM_READABLE);

  g_object_class_install_property (object_class, PROP_DEPTH, pspec);

  pspec = g_param_spec_boolean ("automatic-updates",
                                P_("Automatic Updates"),
                                P_("If the texture should be kept in "
                                   "sync with any pixmap changes."),
                                FALSE,
                                G_PARAM_READWRITE);

  g_object_class_install_property (object_class, PROP_AUTO, pspec);

  pspec = g_param_spec_ulong ("window",
			      P_("Window"),
			      P_("The X11 Window to be bound"),
			      0, G_MAXULONG,
			      None,
			      G_PARAM_READWRITE);

  g_object_class_install_property (object_class, PROP_WINDOW, pspec);

  pspec = g_param_spec_boolean ("window-redirect-automatic",
                                P_("Window Redirect Automatic"),
                                P_("If composite window redirects are set to "
                                   "Automatic (or Manual if false)"),
                                TRUE,
                                G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_WINDOW_REDIRECT_AUTOMATIC, pspec);


  pspec = g_param_spec_boolean ("window-mapped",
                                P_("Window Mapped"),
                                P_("If window is mapped"),
                                FALSE,
                                G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_WINDOW_MAPPED, pspec);


  pspec = g_param_spec_boolean ("destroyed",
                                P_("Destroyed"),
                                P_("If window has been destroyed"),
                                FALSE,
                                G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_DESTROYED, pspec);

  pspec = g_param_spec_int ("window-x",
                            P_("Window X"),
                            P_("X position of window on screen according to X11"),
                            G_MININT, G_MAXINT, 0, G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_WINDOW_X, pspec);


  pspec = g_param_spec_int ("window-y",
                            P_("Window Y"),
                            P_("Y position of window on screen according to X11"),
                            G_MININT, G_MAXINT, 0, G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_WINDOW_Y, pspec);

  pspec = g_param_spec_boolean ("window-override-redirect",
                                P_("Window Override Redirect"),
                                P_("If this is an override-redirect window"),
                                FALSE,
                                G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_WINDOW_OVERRIDE_REDIRECT, pspec);


  /**
   * ClutterX11TexturePixmap::update-area:
   * @texture: the object which received the signal
   *
   * The ::hide signal is emitted to ask the texture to update its
   * content from its source pixmap.
   *
   * Since: 0.8
   */
  signals[UPDATE_AREA] =
      g_signal_new (g_intern_static_string ("update-area"),
                    G_TYPE_FROM_CLASS (object_class),
                    G_SIGNAL_RUN_FIRST,
                    G_STRUCT_OFFSET (ClutterX11TexturePixmapClass, \
                                     update_area),
                    NULL, NULL,
                    _clutter_marshal_VOID__INT_INT_INT_INT,
                    G_TYPE_NONE, 4,
                    G_TYPE_INT,
                    G_TYPE_INT,
                    G_TYPE_INT,
                    G_TYPE_INT);

  /**
   * ClutterX11TexturePixmap::queue-damage-redraw
   * @texture: the object which received the signal
   * @x: The top left x position of the damage region
   * @y: The top left y position of the damage region
   * @width: The width of the damage region
   * @height: The height of the damage region
   *
   * ::queue-damage-redraw is emitted to notify that some sub-region
   * of the texture has been changed (either by an automatic damage
   * update or by an explicit call to
   * clutter_x11_texture_pixmap_update_area). This usually means a
   * redraw needs to be queued for the actor.
   *
   * The default handler will queue a clipped redraw in response to
   * the damage, using the assumption that the pixmap is being painted
   * to a rectangle covering the transformed allocation of the actor.
   * If you sub-class and change the paint method so this isn't true
   * then you must also provide your own damage signal handler to
   * queue a redraw that blocks this default behaviour.
   *
   * Since: 1.2
   */
  signals[QUEUE_DAMAGE_REDRAW] =
    g_signal_new (g_intern_static_string ("queue-damage-redraw"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_FIRST,
                  0,
                  NULL, NULL,
                  _clutter_marshal_VOID__INT_INT_INT_INT,
                  G_TYPE_NONE, 4,
                  G_TYPE_INT,
                  G_TYPE_INT,
                  G_TYPE_INT,
                  G_TYPE_INT);

  g_signal_override_class_handler ("queue-damage-redraw",
                                   CLUTTER_X11_TYPE_TEXTURE_PIXMAP,
                                   G_CALLBACK (clutter_x11_texture_pixmap_real_queue_damage_redraw));

  default_backend = clutter_get_default_backend ();

  if (!CLUTTER_IS_BACKEND_X11 (default_backend))
    {
      g_critical ("ClutterX11TexturePixmap instantiated with a "
                  "non-X11 backend");
      return;
    }
}

static void
clutter_x11_texture_pixmap_update_area_real (ClutterX11TexturePixmap *texture,
                                             gint                     x,
                                             gint                     y,
                                             gint                     width,
                                             gint                     height)
{
  CoglHandle cogl_texture;

  cogl_texture = clutter_texture_get_cogl_texture (CLUTTER_TEXTURE (texture));

  if (cogl_texture)
    cogl_texture_pixmap_x11_update_area (cogl_texture, x, y, width, height);
}

/**
 * clutter_x11_texture_pixmap_new:
 *
 * Creates a new #ClutterX11TexturePixmap which can be used to display the
 * contents of an X11 Pixmap inside a Clutter scene graph
 *
 * Return value: A new #ClutterX11TexturePixmap
 *
 * Since: 0.8
 */
ClutterActor *
clutter_x11_texture_pixmap_new (void)
{
  ClutterActor *actor;

  actor = g_object_new (CLUTTER_X11_TYPE_TEXTURE_PIXMAP, NULL);

  return actor;
}

/**
 * clutter_x11_texture_pixmap_new_with_pixmap:
 * @pixmap: the X Pixmap to which this texture should be bound
 *
 * Creates a new #ClutterX11TexturePixmap for @pixmap
 *
 * Return value: A new #ClutterX11TexturePixmap bound to the given X Pixmap
 *
 * Since: 0.8
 */
ClutterActor *
clutter_x11_texture_pixmap_new_with_pixmap (Pixmap pixmap)
{
  ClutterActor *actor;

  actor = g_object_new (CLUTTER_X11_TYPE_TEXTURE_PIXMAP,
			"pixmap", pixmap,
			NULL);

  return actor;
}

/**
 * clutter_x11_texture_pixmap_new_with_window:
 * @window: the X window to which this texture should be bound
 *
 * Creates a new #ClutterX11TexturePixmap for @window
 *
 * Return value: A new #ClutterX11TexturePixmap bound to the given X window.
 *
 * Since: 0.8
 **/
ClutterActor *
clutter_x11_texture_pixmap_new_with_window (Window window)
{
  ClutterActor *actor;

  actor = g_object_new (CLUTTER_X11_TYPE_TEXTURE_PIXMAP,
			"window", window,
			NULL);

  return actor;
}

/**
 * clutter_x11_texture_pixmap_set_pixmap:
 * @texture: the texture to bind
 * @pixmap: the X Pixmap to which the texture should be bound
 *
 * Sets the X Pixmap to which the texture should be bound.
 *
 * Since: 0.8
 */
void
clutter_x11_texture_pixmap_set_pixmap (ClutterX11TexturePixmap *texture,
                                       Pixmap                   pixmap)
{
  Window       root;
  int          x, y;
  unsigned int width, height, border_width, depth;
  Status       status = 0;
  gboolean     new_pixmap = FALSE, new_pixmap_width = FALSE;
  gboolean     new_pixmap_height = FALSE, new_pixmap_depth = FALSE;
  CoglHandle   material;
  CoglHandle   cogl_texture;

  ClutterX11TexturePixmapPrivate *priv;

  g_return_if_fail (CLUTTER_X11_IS_TEXTURE_PIXMAP (texture));

  priv = texture->priv;

  /* Get rid of the existing Cogl texture early because it may try to
     use the pixmap which we might destroy */
  material = clutter_texture_get_cogl_material (CLUTTER_TEXTURE (texture));
  if (material)
    cogl_material_set_layer (material, 0, COGL_INVALID_HANDLE);

  if (pixmap != None)
    {
      clutter_x11_trap_x_errors ();
      status = XGetGeometry (clutter_x11_get_default_display(),
			     (Drawable)pixmap,
			     &root,
			     &x,
			     &y,
			     &width,
			     &height,
			     &border_width,
			     &depth);

      if (clutter_x11_untrap_x_errors () || status == 0)
	{
	  g_warning ("Unable to query pixmap: %lx", pixmap);
	  pixmap = None;
	  width = height = depth = 0;
	}
    }
  else
    {
      width = height = depth = 0;
    }

  if (priv->pixmap != pixmap)
    {
      if (priv->pixmap && priv->owns_pixmap)
        XFreePixmap (clutter_x11_get_default_display (), priv->pixmap);

      priv->pixmap = pixmap;
      new_pixmap = TRUE;

      /* The damage object is created on the pixmap, so it needs to be
       * recreated with a change in pixmap.
       */
      if (priv->automatic_updates)
	{
	  free_damage_resources (texture);
	  create_damage_resources (texture);
	}
    }

  if (priv->pixmap_width != width)
    {
      priv->pixmap_width = width;
      new_pixmap_width = TRUE;
    }

  if (priv->pixmap_height != height)
    {
      priv->pixmap_height = height;
      new_pixmap_height = TRUE;
    }

  if (priv->depth != depth)
    {
      priv->depth = depth;
      new_pixmap_depth = TRUE;
    }

  /* NB: We defer sending the signals until updating all the
   * above members so the values are all available to the
   * signal handlers. */
  g_object_ref (texture);

  if (new_pixmap)
    g_object_notify (G_OBJECT (texture), "pixmap");
  if (new_pixmap_width)
    g_object_notify (G_OBJECT (texture), "pixmap-width");
  if (new_pixmap_height)
    g_object_notify (G_OBJECT (texture), "pixmap-height");
  if (new_pixmap_depth)
    g_object_notify (G_OBJECT (texture), "pixmap-depth");

  if (pixmap)
    {
      cogl_texture = cogl_texture_pixmap_x11_new (pixmap, FALSE);
      clutter_texture_set_cogl_texture (CLUTTER_TEXTURE (texture),
                                        cogl_texture);
      cogl_handle_unref (cogl_texture);
      update_pixmap_damage_object (texture);
    }

  /*
   * Keep ref until here in case a notify causes removal from the scene; can't
   * lower the notifies because glx's notify handler needs to run before
   * update_area
   */
  g_object_unref (texture);
}

/**
 * clutter_x11_texture_pixmap_set_window:
 * @texture: the texture to bind
 * @window: the X window to which the texture should be bound
 * @automatic: %TRUE for automatic window updates, %FALSE for manual.
 *
 * Sets up a suitable pixmap for the window, using the composite and damage
 * extensions if possible, and then calls
 * clutter_x11_texture_pixmap_set_pixmap().
 *
 * If you want to display a window in a #ClutterTexture, you probably want
 * this function, or its older sister, clutter_glx_texture_pixmap_set_window().
 *
 * This function has no effect unless the XComposite extension is available.
 *
 * Since: 0.8
 */
void
clutter_x11_texture_pixmap_set_window (ClutterX11TexturePixmap *texture,
                                       Window                   window,
                                       gboolean                 automatic)
{
  ClutterX11TexturePixmapPrivate *priv;
  XWindowAttributes attr;
  Display *dpy = clutter_x11_get_default_display ();

  g_return_if_fail (CLUTTER_X11_IS_TEXTURE_PIXMAP (texture));

  if (!clutter_x11_has_composite_extension ())
    return;

#if HAVE_XCOMPOSITE
  priv = texture->priv;

  if (priv->window == window && automatic == priv->window_redirect_automatic)
    return;

  if (priv->window)
    {
      clutter_x11_remove_filter (on_x_event_filter_too, (gpointer)texture);
      clutter_x11_trap_x_errors ();
      XCompositeUnredirectWindow(clutter_x11_get_default_display (),
                                  priv->window,
                                  priv->window_redirect_automatic ?
                                  CompositeRedirectAutomatic : CompositeRedirectManual);
      XSync (clutter_x11_get_default_display (), False);
      clutter_x11_untrap_x_errors ();

      clutter_x11_texture_pixmap_set_pixmap (texture, None);
    }

  priv->window = window;
  priv->window_redirect_automatic = automatic;
  priv->window_mapped = FALSE;
  priv->destroyed = FALSE;

  if (window == None)
    return;

  clutter_x11_trap_x_errors ();
  {
    if (!XGetWindowAttributes (dpy, window, &attr))
      {
        XSync (dpy, False);
        clutter_x11_untrap_x_errors ();
        g_warning ("bad window 0x%x", (guint32)window);
        priv->window = None;
        return;
      }

    XCompositeRedirectWindow
                       (dpy,
                        window,
                        automatic ?
                        CompositeRedirectAutomatic : CompositeRedirectManual);
    XSync (dpy, False);
  }

  clutter_x11_untrap_x_errors ();

  if (priv->window)
    {
      XSelectInput (dpy, priv->window,
                    attr.your_event_mask | StructureNotifyMask);
      clutter_x11_add_filter (on_x_event_filter_too, (gpointer)texture);
    }

  g_object_ref (texture);
  g_object_notify (G_OBJECT (texture), "window");

  clutter_x11_texture_pixmap_set_mapped (texture,
                                         attr.map_state == IsViewable);

  clutter_x11_texture_pixmap_sync_window (texture);
  g_object_unref (texture);

#endif /* HAVE_XCOMPOSITE */
}

/**
 * clutter_x11_texture_pixmap_sync_window:
 * @texture: the texture to bind
 *
 * Resets the texture's pixmap from its window, perhaps in response to the
 * pixmap's invalidation as the window changed size.
 *
 * Since: 0.8
 */
void
clutter_x11_texture_pixmap_sync_window (ClutterX11TexturePixmap *texture)
{
  ClutterX11TexturePixmapPrivate *priv;
  Pixmap pixmap;

  g_return_if_fail (CLUTTER_X11_IS_TEXTURE_PIXMAP (texture));

  priv = texture->priv;

  if (priv->destroyed)
    return;

  if (!clutter_x11_has_composite_extension())
    {
      clutter_x11_texture_pixmap_set_pixmap (texture, priv->window);
      return;
    }

  if (priv->window)
    {
      XWindowAttributes attr;
      Display *dpy = clutter_x11_get_default_display ();
      gboolean mapped = FALSE;
      gboolean notify_x = FALSE;
      gboolean notify_y = FALSE;
      gboolean notify_override_redirect = FALSE;
      Status status;

      /* NB: It's only valid to name a pixmap if the window is viewable.
       *
       * We don't explicitly check this though since there would be a race
       * between checking and naming unless we use a server grab which is
       * undesireable.
       *
       * Instead we gracefully handle any error with naming the pixmap.
       */
      clutter_x11_trap_x_errors ();

      pixmap = XCompositeNameWindowPixmap (dpy, priv->window);

      status = XGetWindowAttributes (dpy, priv->window, &attr);
      if (status != 0)
	{
	  notify_x = attr.x != priv->window_x;
	  notify_y = attr.y != priv->window_y;
	  notify_override_redirect = attr.override_redirect != priv->override_redirect;
	  priv->window_x = attr.x;
	  priv->window_y = attr.y;
	  priv->override_redirect = attr.override_redirect;
	}

      /* We rely on XGetWindowAttributes() implicitly syncing with the server
       * so if there was an error naming the pixmap that will have been
       * caught by now. */
      if (clutter_x11_untrap_x_errors ())
	pixmap = None;

      g_object_ref (texture); /* guard against unparent */
      if (pixmap)
        {
          clutter_x11_texture_pixmap_set_pixmap (texture, pixmap);
          priv->owns_pixmap = TRUE;
        }
      clutter_x11_texture_pixmap_set_mapped (texture, mapped);
      /* could do more clever things with a signal, i guess.. */
      if (notify_override_redirect)
        g_object_notify (G_OBJECT (texture), "window-override-redirect");
      if (notify_x)
        g_object_notify (G_OBJECT (texture), "window-x");
      if (notify_y)
        g_object_notify (G_OBJECT (texture), "window-y");
      g_object_unref (texture);
    }
}

static void
clutter_x11_texture_pixmap_set_mapped (ClutterX11TexturePixmap *texture,
                                       gboolean mapped)
{
  ClutterX11TexturePixmapPrivate *priv;

  priv = texture->priv;

  if (mapped != priv->window_mapped)
    {
      priv->window_mapped = mapped;
      g_object_notify (G_OBJECT (texture), "window-mapped");
    }
}

static void
clutter_x11_texture_pixmap_destroyed (ClutterX11TexturePixmap *texture)
{
  ClutterX11TexturePixmapPrivate *priv;

  priv = texture->priv;

  if (!priv->destroyed)
    {
      priv->destroyed = TRUE;
      g_object_notify (G_OBJECT (texture), "destroyed");
    }

  /*
   * Don't set window to None, that would destroy the pixmap, which might still
   * be useful e.g. for destroy animations -- app's responsibility.
   */
}

/**
 * clutter_x11_texture_pixmap_update_area:
 * @texture: The texture whose content shall be updated.
 * @x: the X coordinate of the area to update
 * @y: the Y coordinate of the area to update
 * @width: the width of the area to update
 * @height: the height of the area to update
 *
 * Performs the actual binding of texture to the current content of
 * the pixmap. Can be called to update the texture if the pixmap
 * content has changed.
 *
 * Since: 0.8
 **/
void
clutter_x11_texture_pixmap_update_area (ClutterX11TexturePixmap *texture,
                                        gint                     x,
                                        gint                     y,
                                        gint                     width,
                                        gint                     height)
{
  g_return_if_fail (CLUTTER_X11_IS_TEXTURE_PIXMAP (texture));

  g_signal_emit (texture, signals[UPDATE_AREA], 0, x, y, width, height);

  /* The default handler for the "queue-damage-redraw" signal is
   * clutter_x11_texture_pixmap_real_queue_damage_redraw which will queue a
   * clipped redraw. */
  g_signal_emit (texture, signals[QUEUE_DAMAGE_REDRAW],
                 0, x, y, width, height);
}

/**
 * clutter_x11_texture_pixmap_set_automatic:
 * @texture: a #ClutterX11TexturePixmap
 * @setting: %TRUE to enable automatic updates
 *
 * Enables or disables the automatic updates ot @texture in case the backing
 * pixmap or window is damaged
 *
 * Since: 0.8
 */
void
clutter_x11_texture_pixmap_set_automatic (ClutterX11TexturePixmap *texture,
                                          gboolean                 setting)
{
  ClutterX11TexturePixmapPrivate *priv;
  Display                        *dpy;

  g_return_if_fail (CLUTTER_X11_IS_TEXTURE_PIXMAP (texture));

  priv = texture->priv;

  setting = !!setting;
  if (setting == priv->automatic_updates)
    return;

  dpy = clutter_x11_get_default_display();

  if (setting)
    create_damage_resources (texture);
  else
    free_damage_resources (texture);

  priv->automatic_updates = setting;
}
