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

/**
 * SECTION:clutter-timeline
 * @short_description: A class for based events
 *
 * #ClutterTimeline is a base class for managing time based events such
 * as animations.
 *
 * Every timeline shares the same #ClutterTimeoutPool to decrease the
 * possibility of starvating the main loop when using many timelines
 * at the same time; this might cause problems if you are also using
 * a library making heavy use of threads with no GLib main loop integration.
 * In that case you might disable the common timeline pool by setting
 * the %CLUTTER_TIMELINE=no-pool environment variable prior to launching
 * your application.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "clutter-timeout-pool.h"
#include "clutter-timeline.h"
#include "clutter-main.h"
#include "clutter-marshal.h"
#include "clutter-private.h"
#include "clutter-debug.h"
#include "clutter-enum-types.h"

G_DEFINE_TYPE (ClutterTimeline, clutter_timeline, G_TYPE_OBJECT);

#define FPS_TO_INTERVAL(f) (1000 / (f))
#define CLUTTER_TIMELINE_PRIORITY       (G_PRIORITY_DEFAULT + 30)

struct _ClutterTimelinePrivate
{
  ClutterTimelineDirection direction;

  guint timeout_id;
  guint delay_id;

  gint current_frame_num;

  guint fps;
  guint n_frames;
  guint delay;
  guint duration;

  gint skipped_frames;

  gulong last_frame_msecs;
  gulong start_frame_secs;
  guint  msecs_delta;

  guint loop : 1;
};

enum
{
  PROP_0,

  PROP_FPS,
  PROP_NUM_FRAMES,
  PROP_LOOP,
  PROP_DELAY,
  PROP_DURATION,
  PROP_DIRECTION
};

enum
{
  NEW_FRAME,
  STARTED,
  PAUSED,
  COMPLETED,

  LAST_SIGNAL
};

static guint               timeline_signals[LAST_SIGNAL] = { 0 };
static gint                timeline_use_pool = -1;
static ClutterTimeoutPool *timeline_pool = NULL;

static inline void
timeline_pool_init (void)
{
  if (timeline_use_pool == -1)
    {
      const gchar *timeline_env;

      timeline_env = g_getenv ("CLUTTER_TIMELINE");
      if (timeline_env && timeline_env[0] != '\0' &&
          strcmp (timeline_env, "no-pool") == 0)
        {
          timeline_use_pool = FALSE;
        }
      else
        {
          timeline_pool = clutter_timeout_pool_new (CLUTTER_TIMELINE_PRIORITY);
          timeline_use_pool = TRUE;
        }
    }
}

static guint
timeout_add (guint          interval,
             GSourceFunc    func,
             gpointer       data,
             GDestroyNotify notify)
{
  guint res;

  if (G_LIKELY (timeline_use_pool))
    {
      g_assert (timeline_pool != NULL);
      res = clutter_timeout_pool_add (timeline_pool,
                                      interval,
                                      func, data, notify);
    }
  else
    {
      res = clutter_threads_add_timeout_full (CLUTTER_TIMELINE_PRIORITY,
                                              interval,
                                              func, data, notify);
    }

  return res;
}

static void
timeout_remove (guint tag)
{
  if (G_LIKELY (timeline_use_pool))
    {
      g_assert (timeline_pool != NULL);
      clutter_timeout_pool_remove (timeline_pool, tag);
    }
  else
    g_source_remove (tag);
}

/* Object */

static void
clutter_timeline_set_property (GObject      *object,
			       guint         prop_id,
			       const GValue *value,
			       GParamSpec   *pspec)
{
  ClutterTimeline        *timeline;
  ClutterTimelinePrivate *priv;

  timeline = CLUTTER_TIMELINE(object);
  priv = timeline->priv;

  switch (prop_id)
    {
    case PROP_FPS:
      clutter_timeline_set_speed (timeline, g_value_get_uint (value));
      break;
    case PROP_NUM_FRAMES:
      priv->n_frames = g_value_get_uint (value);
      break;
    case PROP_LOOP:
      priv->loop = g_value_get_boolean (value);
      break;
    case PROP_DELAY:
      priv->delay = g_value_get_uint (value);
      break;
    case PROP_DURATION:
      clutter_timeline_set_duration (timeline, g_value_get_uint (value));
      break;
    case PROP_DIRECTION:
      clutter_timeline_set_direction (timeline, g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
clutter_timeline_get_property (GObject    *object,
			       guint       prop_id,
			       GValue     *value,
			       GParamSpec *pspec)
{
  ClutterTimeline        *timeline;
  ClutterTimelinePrivate *priv;

  timeline = CLUTTER_TIMELINE(object);
  priv = timeline->priv;

  switch (prop_id)
    {
    case PROP_FPS:
      g_value_set_uint (value, priv->fps);
      break;
    case PROP_NUM_FRAMES:
      g_value_set_uint (value, priv->n_frames);
      break;
    case PROP_LOOP:
      g_value_set_boolean (value, priv->loop);
      break;
    case PROP_DELAY:
      g_value_set_uint (value, priv->delay);
      break;
    case PROP_DURATION:
      g_value_set_uint (value, priv->duration);
      break;
    case PROP_DIRECTION:
      g_value_set_enum (value, priv->direction);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_timeline_finalize (GObject *object)
{
  G_OBJECT_CLASS (clutter_timeline_parent_class)->finalize (object);
}

static void
clutter_timeline_dispose (GObject *object)
{
  ClutterTimeline *self = CLUTTER_TIMELINE(object);
  ClutterTimelinePrivate *priv;

  priv = self->priv;

  if (priv->delay_id)
    {
      timeout_remove (priv->delay_id);
      priv->delay_id = 0;
    }

  if (priv->timeout_id)
    {
      timeout_remove (priv->timeout_id);
      priv->timeout_id = 0;
    }

  G_OBJECT_CLASS (clutter_timeline_parent_class)->dispose (object);
}

static void
clutter_timeline_class_init (ClutterTimelineClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  timeline_pool_init ();

  object_class->set_property = clutter_timeline_set_property;
  object_class->get_property = clutter_timeline_get_property;
  object_class->finalize     = clutter_timeline_finalize;
  object_class->dispose      = clutter_timeline_dispose;

  g_type_class_add_private (klass, sizeof (ClutterTimelinePrivate));

  /**
   * ClutterTimeline:fps:
   *
   * Timeline frames per second. Because of the nature of the main
   * loop used by Clutter this is to be considered a best approximation.
   */
  g_object_class_install_property (object_class,
                                   PROP_FPS,
                                   g_param_spec_uint ("fps",
                                                      "Frames Per Second",
                                                      "Timeline frames per second",
                                                      1, 1000,
                                                      60,
                                                      CLUTTER_PARAM_READWRITE));
  /**
   * ClutterTimeline:num-frames:
   *
   * Total number of frames for the timeline.
   */
  g_object_class_install_property (object_class,
                                   PROP_NUM_FRAMES,
                                   g_param_spec_uint ("num-frames",
                                                      "Total number of frames",
                                                      "Timelines total number of frames",
                                                      1, G_MAXUINT,
                                                      1,
                                                      CLUTTER_PARAM_READWRITE));
  /**
   * ClutterTimeline:loop:
   *
   * Whether the timeline should automatically rewind and restart.
   */
  g_object_class_install_property (object_class,
                                   PROP_LOOP,
                                   g_param_spec_boolean ("loop",
                                                         "Loop",
                                                         "Should the timeline automatically restart",
                                                         FALSE,
                                                         CLUTTER_PARAM_READWRITE));
  /**
   * ClutterTimeline:delay:
   *
   * A delay, in milliseconds, that should be observed by the
   * timeline before actually starting.
   *
   * Since: 0.4
   */
  g_object_class_install_property (object_class,
                                   PROP_DELAY,
                                   g_param_spec_uint ("delay",
                                                      "Delay",
                                                      "Delay before start",
                                                      0, G_MAXUINT,
                                                      0,
                                                      CLUTTER_PARAM_READWRITE));
  /**
   * ClutterTimeline:duration:
   *
   * Duration of the timeline in milliseconds, depending on the
   * ClutterTimeline:fps value.
   *
   * Since: 0.6
   */
  g_object_class_install_property (object_class,
                                   PROP_DURATION,
                                   g_param_spec_uint ("duration",
                                                      "Duration",
                                                      "Duration of the timeline in milliseconds",
                                                      0, G_MAXUINT,
                                                      1000,
                                                      CLUTTER_PARAM_READWRITE));
  /**
   * ClutterTimeline:direction:
   * 
   * The direction of the timeline, either %CLUTTER_TIMELINE_FORWARD or
   * %CLUTTER_TIMELINE_BACKWARD.
   *
   * Since: 0.6
   */
  g_object_class_install_property (object_class,
                                   PROP_DIRECTION,
                                   g_param_spec_enum ("direction",
                                                      "Direction",
                                                      "Direction of the timeline",
                                                      CLUTTER_TYPE_TIMELINE_DIRECTION,
                                                      CLUTTER_TIMELINE_FORWARD,
                                                      CLUTTER_PARAM_READWRITE));

  /**
   * ClutterTimeline::new-frame:
   * @timeline: the timeline which received the signal
   * @frame_num: the number of the new frame
   *
   * The ::new-frame signal is emitted each time a new frame in the
   * timeline is reached.
   */
  timeline_signals[NEW_FRAME] =
    g_signal_new ("new-frame",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterTimelineClass, new_frame),
		  NULL, NULL,
		  clutter_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1, G_TYPE_INT);
  /**
   * ClutterTimeline::completed:
   * @timeline: the #ClutterTimeline which received the signal
   *
   * The ::completed signal is emitted when the timeline reaches the
   * number of frames specified by the ClutterTimeline:num-frames property.
   */
  timeline_signals[COMPLETED] =
    g_signal_new ("completed",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterTimelineClass, completed),
		  NULL, NULL,
		  clutter_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
  /**
   * ClutterTimeline::started:
   * @timeline: the #ClutterTimeline which received the signal
   *
   * The ::started signal is emitted when the timeline starts its run.
   * This might be as soon as clutter_timeline_start() is invoked or
   * after the delay set in the ClutterTimeline:delay property has
   * expired.
   */
  timeline_signals[STARTED] =
    g_signal_new ("started",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterTimelineClass, started),
		  NULL, NULL,
		  clutter_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
  /**
   * ClutterTimeline::paused:
   * @timeline: the #ClutterTimeline which received the signal
   *
   * The ::paused signal is emitted when clutter_timeline_pause() is invoked.
   */
  timeline_signals[PAUSED] =
    g_signal_new ("paused",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterTimelineClass, paused),
		  NULL, NULL,
		  clutter_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
}

static void
clutter_timeline_init (ClutterTimeline *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
					    CLUTTER_TYPE_TIMELINE,
					    ClutterTimelinePrivate);

  self->priv->fps = clutter_get_default_frame_rate ();
  self->priv->n_frames = 0;
  self->priv->msecs_delta = 0;
}

static gboolean
timeline_timeout_func (gpointer data)
{
  ClutterTimeline        *timeline = data;
  ClutterTimelinePrivate *priv;
  GTimeVal                timeval;
  gint                    n_frames;
  gulong                  msecs;
  gboolean                retval = TRUE;

  priv = timeline->priv;

  if (!timeline)
    {
      CLUTTER_NOTE (SCHEDULER,
                    "The timeline [%p] has been disposed",
                    timeline);
      return FALSE;
    }

  if (!priv->timeout_id)
    {
      CLUTTER_NOTE (SCHEDULER,
                    "The timeline [%p] has been removed",
                    timeline);

      return FALSE;
    }

  g_object_ref (timeline);

  /* Figure out potential frame skips */
  g_get_current_time (&timeval);

  CLUTTER_TIMESTAMP (SCHEDULER, "Timeline [%p] activated (cur: %d)\n",
                     timeline,
                     priv->current_frame_num);

  /* Fire off signal */
  g_signal_emit (timeline, timeline_signals[NEW_FRAME], 0,
                 priv->current_frame_num);

  /* Signal removes source ? */
  if (!priv->timeout_id)
    {
      g_object_unref (timeline);
      return FALSE;
    }

  if (priv->last_frame_msecs)
    {
      /* Check time diff from out last call and adjust number
       * of frames to advance accordingly.
      */
      msecs = ((timeval.tv_sec - priv->start_frame_secs) * 1000)
              + (timeval.tv_usec / 1000);
      n_frames = (msecs - priv->last_frame_msecs)
                 / (1000 / priv->fps);
      priv->msecs_delta = msecs - priv->last_frame_msecs;

      if (n_frames <= 0)
        {
          n_frames = 1;
          priv->skipped_frames = 0;
        }
      else if (n_frames > 1)
	{
	  CLUTTER_TIMESTAMP (SCHEDULER,
			     "Timeline [%p], skipping %d frames\n",
			     timeline,
                             n_frames);

          priv->skipped_frames = n_frames - 1;
	}
      else
        priv->skipped_frames = 0;
    }
  else
    {
      /* First frame, set up timings.*/
      priv->start_frame_secs = timeval.tv_sec;
      priv->skipped_frames   = 0;
	  priv->msecs_delta = 0;

      msecs     = timeval.tv_usec / 1000;
      n_frames  = 1;
    }

  priv->last_frame_msecs = msecs;

  /* Advance frames */
  if (priv->direction == CLUTTER_TIMELINE_FORWARD)
    priv->current_frame_num += n_frames;
  else
    priv->current_frame_num -= n_frames;

  /* Handle loop or stop */
  if (((priv->direction == CLUTTER_TIMELINE_FORWARD) &&
       (priv->current_frame_num > priv->n_frames)) ||
      ((priv->direction == CLUTTER_TIMELINE_BACKWARD) &&
       (priv->current_frame_num < 0)))
    {
      if (priv->direction == CLUTTER_TIMELINE_FORWARD)
        {
          priv->current_frame_num = priv->n_frames;
        }
      else if (priv->direction == CLUTTER_TIMELINE_BACKWARD)
        {
          priv->current_frame_num = 0;
        }

      CLUTTER_NOTE (SCHEDULER,
                    "Timeline [%p] completed (cur: %d, tot: %d, drop: %d)",
                    timeline,
                    priv->current_frame_num,
                    priv->n_frames,
                    n_frames - 1);

      /* if we skipped some frame to get here let's see whether we still need
       * to emit the last new-frame signal with the last frame
       */
      if (n_frames > 1)
	{
	  g_signal_emit (timeline, timeline_signals[NEW_FRAME], 0,
                         priv->current_frame_num);
	}

      if (priv->loop)
	{
	  g_signal_emit (timeline, timeline_signals[COMPLETED], 0);
	  clutter_timeline_rewind (timeline);

          retval = TRUE;
	}
      else
	{
          if (priv->timeout_id)
            {
              timeout_remove (priv->timeout_id);
              priv->timeout_id = 0;
            }

          priv->last_frame_msecs = 0;

          g_signal_emit (timeline, timeline_signals[COMPLETED], 0);
          clutter_timeline_rewind (timeline);

	  retval = FALSE;
	}
    }

  g_object_unref (timeline);

  return retval;
}

static gboolean
delay_timeout_func (gpointer data)
{
  ClutterTimeline *timeline = data;
  ClutterTimelinePrivate *priv = timeline->priv;

  priv->delay_id = 0;

  priv->timeout_id = timeout_add (FPS_TO_INTERVAL (priv->fps),
                                  timeline_timeout_func,
                                  timeline, NULL);

  g_signal_emit (timeline, timeline_signals[STARTED], 0);

  return FALSE;
}

/**
 * clutter_timeline_start:
 * @timeline: A #ClutterTimeline
 *
 * Starts the #ClutterTimeline playing.
 **/
void
clutter_timeline_start (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = timeline->priv;

  if (priv->delay_id || priv->timeout_id)
    return;

  if (priv->n_frames == 0)
    return;

  if (priv->delay)
    {
      priv->delay_id = timeout_add (priv->delay,
                                    delay_timeout_func,
                                    timeline, NULL);
    }
  else
    {
      priv->timeout_id = timeout_add (FPS_TO_INTERVAL (priv->fps),
                                      timeline_timeout_func,
                                      timeline, NULL);

      g_signal_emit (timeline, timeline_signals[STARTED], 0);
    }
}

/**
 * clutter_timeline_pause:
 * @timeline: A #ClutterTimeline
 *
 * Pauses the #ClutterTimeline on current frame
 **/
void
clutter_timeline_pause (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = timeline->priv;

  if (priv->delay_id)
    {
      timeout_remove (priv->delay_id);
      priv->delay_id = 0;
    }

  if (priv->timeout_id)
    {
      timeout_remove (priv->timeout_id);
      priv->timeout_id = 0;
    }

  priv->last_frame_msecs = 0;

  g_signal_emit (timeline, timeline_signals[PAUSED], 0);
}

/**
 * clutter_timeline_stop:
 * @timeline: A #ClutterTimeline
 *
 * Stops the #ClutterTimeline and moves to frame 0
 **/
void
clutter_timeline_stop (ClutterTimeline *timeline)
{
  clutter_timeline_pause (timeline);
  clutter_timeline_rewind (timeline);
}

/**
 * clutter_timeline_set_loop:
 * @timeline: a #ClutterTimeline
 * @loop: %TRUE for enable looping
 *
 * Sets whether @timeline should loop.
 */
void
clutter_timeline_set_loop (ClutterTimeline *timeline,
			   gboolean         loop)
{
  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  if (timeline->priv->loop != loop)
    {
      timeline->priv->loop = loop;

      g_object_notify (G_OBJECT (timeline), "loop");
    }
}

/**
 * clutter_timeline_get_loop:
 * @timeline: a #ClutterTimeline
 *
 * Gets whether @timeline is looping
 *
 * Return value: %TRUE if the timeline is looping
 */
gboolean
clutter_timeline_get_loop (ClutterTimeline *timeline)
{
  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), FALSE);

  return timeline->priv->loop;
}

/**
 * clutter_timeline_rewind:
 * @timeline: A #ClutterTimeline
 *
 * Rewinds #ClutterTimeline to frame 0.
 **/
void
clutter_timeline_rewind (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = timeline->priv;

  if (priv->direction == CLUTTER_TIMELINE_FORWARD)
    clutter_timeline_advance (timeline, 0);
  else if (priv->direction == CLUTTER_TIMELINE_BACKWARD)
    clutter_timeline_advance (timeline, priv->n_frames);
}

/**
 * clutter_timeline_skip:
 * @timeline: A #ClutterTimeline
 * @n_frames: Number of frames to skip
 *
 * Advance timeline by requested number of frames.
 **/
void
clutter_timeline_skip (ClutterTimeline *timeline,
                       guint            n_frames)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = timeline->priv;

  priv->current_frame_num += n_frames;

  if (priv->direction == CLUTTER_TIMELINE_FORWARD)
    {
      priv->current_frame_num += n_frames;

      if (priv->current_frame_num > priv->n_frames)
        priv->current_frame_num = 1;
    }
  else if (priv->direction == CLUTTER_TIMELINE_BACKWARD)
    {
      priv->current_frame_num -= n_frames;

      if (priv->current_frame_num < 1)
        priv->current_frame_num = priv->n_frames - 1;
    }
}

/**
 * clutter_timeline_advance:
 * @timeline: A #ClutterTimeline
 * @frame_num: Frame number to advance to
 *
 * Advance timeline to requested frame number
 **/
void
clutter_timeline_advance (ClutterTimeline *timeline,
                          guint            frame_num)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = timeline->priv;

  priv->current_frame_num = CLAMP (frame_num, 0, priv->n_frames);
}

/**
 * clutter_timeline_get_current_frame:
 * @timeline: A #ClutterTimeline
 *
 * Request the current frame number of the timeline.
 *
 * Return Value: current frame number
 **/
gint
clutter_timeline_get_current_frame (ClutterTimeline *timeline)
{
  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0);

  return timeline->priv->current_frame_num;
}

/**
 * clutter_timeline_get_n_frames:
 * @timeline: A #ClutterTimeline
 *
 * Request the total number of frames for the #ClutterTimeline.
 *
 * Return Value: Number of frames for this #ClutterTimeline.
 **/
guint
clutter_timeline_get_n_frames (ClutterTimeline *timeline)
{
  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0);

  return timeline->priv->n_frames;
}

/**
 * clutter_timeline_set_n_frames:
 * @timeline: a #ClutterTimeline
 * @n_frames: the number of frames
 *
 * Sets the total number of frames for @timeline
 */
void
clutter_timeline_set_n_frames (ClutterTimeline *timeline,
                               guint            n_frames)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));
  g_return_if_fail (n_frames > 0);

  priv = timeline->priv;

  if (priv->n_frames != n_frames)
    {
      priv->n_frames = n_frames;

      g_object_notify (G_OBJECT (timeline), "num-frames");
    }
}

/**
 * clutter_timeline_set_speed:
 * @timeline: A #ClutterTimeline
 * @fps: New speed of timeline as frames per second
 *
 * Set the speed in frames per second of the timeline.
 **/
void
clutter_timeline_set_speed (ClutterTimeline *timeline,
                            guint            fps)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));
  g_return_if_fail (fps > 0);

  priv = timeline->priv;

  if (priv->fps != fps)
    {
      g_object_ref (timeline);

      priv->fps = fps;

      /* if the timeline is playing restart */
      if (priv->timeout_id)
        {
          timeout_remove (priv->timeout_id);

          priv->timeout_id = timeout_add (FPS_TO_INTERVAL (priv->fps),
                                          timeline_timeout_func,
                                          timeline, NULL);
        }

      g_object_notify (G_OBJECT (timeline), "fps");
      g_object_unref (timeline);
    }
}

/**
 * clutter_timeline_get_speed:
 * @timeline: a #ClutterTimeline
 *
 * Gets the frames per second played by @timeline
 *
 * Return value: the number of frames per second.
 */
guint
clutter_timeline_get_speed (ClutterTimeline *timeline)
{
  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0);

  return timeline->priv->fps;
}

/**
 * clutter_timeline_is_playing:
 * @timeline: A #ClutterTimeline
 *
 * Query state of a #ClutterTimeline instance.
 *
 * Return Value: TRUE if timeline is currently playing, FALSE if not.
 */
gboolean
clutter_timeline_is_playing (ClutterTimeline *timeline)
{
  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), FALSE);

  return (timeline->priv->timeout_id != 0);
}

/**
 * clutter_timeline_clone:
 * @timeline: #ClutterTimeline to duplicate.
 *
 * Create a new #ClutterTimeline instance which has property values
 * matching that of supplied timeline. The cloned timeline will not
 * be started and will not be positioned to the current position of
 * @timeline: you will have to start it with clutter_timeline_start().
 *
 * Return Value: a new #ClutterTimeline, cloned from @timeline
 *
 * Since 0.4
 */
ClutterTimeline *
clutter_timeline_clone (ClutterTimeline *timeline)
{
  ClutterTimeline *copy;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), NULL);

  copy = g_object_new (CLUTTER_TYPE_TIMELINE,
                       "fps", clutter_timeline_get_speed (timeline),
                       "num-frames", clutter_timeline_get_n_frames (timeline),
                       "loop", clutter_timeline_get_loop (timeline),
                       "delay", clutter_timeline_get_delay (timeline),
                       "direction", clutter_timeline_get_direction (timeline),
                       NULL);

  return copy;
}

/**
 * clutter_timeline_new_for_duration:
 * @msecs: Duration of the timeline in milliseconds
 *
 * Creates a new #ClutterTimeline with a duration of @msecs using
 * the value of the ClutterTimeline:fps property to compute the
 * equivalent number of frames.
 *
 * Return value: the newly created #ClutterTimeline
 *
 * Since: 0.6
 */
ClutterTimeline *
clutter_timeline_new_for_duration (guint msecs)
{
  return g_object_new (CLUTTER_TYPE_TIMELINE,
                       "duration", msecs,
                       NULL);
}

/**
 * clutter_timeline_new:
 * @n_frames: the number of frames
 * @fps: the number of frames per second
 *
 * Create a new  #ClutterTimeline instance.
 *
 * Return Value: a new #ClutterTimeline
 */
ClutterTimeline*
clutter_timeline_new (guint n_frames,
		      guint fps)
{
  g_return_val_if_fail (n_frames > 0, NULL);
  g_return_val_if_fail (fps > 0, NULL);

  return g_object_new (CLUTTER_TYPE_TIMELINE,
		       "fps", fps,
		       "num-frames", n_frames,
		       NULL);
}

/**
 * clutter_timeline_get_delay:
 * @timeline: a #ClutterTimeline
 *
 * Retrieves the delay set using clutter_timeline_set_delay().
 *
 * Return value: the delay in milliseconds.
 *
 * Since: 0.4
 */
guint
clutter_timeline_get_delay (ClutterTimeline *timeline)
{
  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0);

  return timeline->priv->delay;
}

/**
 * clutter_timeline_set_delay:
 * @timeline: a #ClutterTimeline
 * @msecs: delay in milliseconds
 *
 * Sets the delay, in milliseconds, before @timeline should start.
 *
 * Since: 0.4
 */
void
clutter_timeline_set_delay (ClutterTimeline *timeline,
                            guint            msecs)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));
  g_return_if_fail (msecs > 0);

  priv = timeline->priv;

  if (priv->delay != msecs)
    {
      priv->delay = msecs;
      g_object_notify (G_OBJECT (timeline), "delay");
    }
}

/**
 * clutter_timeline_get_duration:
 * @timeline: a #ClutterTimeline
 *
 * Retrieves the duration of a #ClutterTimeline in milliseconds.
 * See clutter_timeline_set_duration().
 *
 * Return value: the duration of the timeline, in milliseconds.
 *
 * Since: 0.6
 */
guint
clutter_timeline_get_duration (ClutterTimeline *timeline)
{
  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0);

  return timeline->priv->duration;
}

/**
 * clutter_timeline_set_duration:
 * @timeline: a #ClutterTimeline
 * @msecs: duration of the timeline in milliseconds
 *
 * Sets the duration of the timeline, in milliseconds. The speed
 * of the timeline depends on the ClutterTimeline:fps setting.
 *
 * Since: 0.6
 */
void
clutter_timeline_set_duration (ClutterTimeline *timeline,
                               guint            msecs)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = timeline->priv;

  if (priv->duration != msecs)
    {
      g_object_ref (timeline);

      g_object_freeze_notify (G_OBJECT (timeline));

      priv->duration = msecs;

      priv->n_frames = priv->duration * priv->fps / 1000;

      g_object_notify (G_OBJECT (timeline), "num-frames");
      g_object_notify (G_OBJECT (timeline), "duration");

      g_object_thaw_notify (G_OBJECT (timeline));
      g_object_unref (timeline);
    }
}

/**
 * clutter_timeline_get_progress:
 * @timeline: a #ClutterTimeline
 *
 * The position of the timeline in a [0, 1] interval.
 *
 * Return value: the position of the timeline.
 *
 * Since: 0.6
 */
gdouble
clutter_timeline_get_progress (ClutterTimeline *timeline)
{
  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0.);

  return CLUTTER_FIXED_TO_DOUBLE (clutter_timeline_get_progressx (timeline));
}

/**
 * clutter_timeline_get_progressx:
 * @timeline: a #ClutterTimeline
 *
 * Fixed point version of clutter_timeline_get_progress().
 *
 * Return value: the position of the timeline as a fixed point value
 *
 * Since: 0.6
 */
ClutterFixed
clutter_timeline_get_progressx (ClutterTimeline *timeline)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0);

  priv = timeline->priv;

  if (priv->direction == CLUTTER_TIMELINE_FORWARD)
    return CLUTTER_FIXED_DIV (CLUTTER_INT_TO_FIXED (priv->current_frame_num),
                              CLUTTER_INT_TO_FIXED (priv->n_frames));
  else
    return CLUTTER_FIXED_DIV (CLUTTER_INT_TO_FIXED (priv->n_frames),
                              CLUTTER_INT_TO_FIXED (priv->current_frame_num));
}

/**
 * clutter_timeline_get_direction:
 * @timeline: a #ClutterTimeline
 *
 * Retrieves the direction of the timeline set with
 * clutter_timeline_set_direction().
 *
 * Return value: the direction of the timeline
 *
 * Since: 0.6
 */
ClutterTimelineDirection
clutter_timeline_get_direction (ClutterTimeline *timeline)
{
  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), CLUTTER_TIMELINE_FORWARD);

  return timeline->priv->direction;
}

/**
 * clutter_timeline_set_direction:
 * @timeline: a #ClutterTimeline
 * @direction: the direction of the timeline
 *
 * Sets the direction of @timeline, either %CLUTTER_TIMELINE_FORWARD or
 * %CLUTTER_TIMELINE_BACKWARD.
 *
 * Since: 0.6
 */
void
clutter_timeline_set_direction (ClutterTimeline          *timeline,
                                ClutterTimelineDirection  direction)
{
  ClutterTimelinePrivate *priv;

  g_return_if_fail (CLUTTER_IS_TIMELINE (timeline));

  priv = timeline->priv;

  if (priv->direction != direction)
    {
      priv->direction = direction;

      if (priv->current_frame_num == 0)
        priv->current_frame_num = priv->n_frames;

      g_object_notify (G_OBJECT (timeline), "direction");
    }
}

/**
 * clutter_timeline_get_delta:
 * @timeline: a #ClutterTimeline
 * @msecs: return location for the milliseconds elapsed since the last
 *   frame, or %NULL
 *
 * Retrieves the number of frames and the amount of time elapsed since
 * the last ClutterTimeline::new-frame signal.
 *
 * This function is only useful inside handlers for the ::new-frame
 * signal, and its behaviour is undefined if the timeline is not
 * playing.
 *
 * Return value: the amount of frames elapsed since the last one
 *
 * Since: 0.6
 */
guint
clutter_timeline_get_delta (ClutterTimeline *timeline,
                            guint           *msecs)
{
  ClutterTimelinePrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_TIMELINE (timeline), 0);

  if (!clutter_timeline_is_playing (timeline))
    {
      if (msecs)
        *msecs = 0;

      return 0;
    }

  priv = timeline->priv;

  if (msecs)
    {
      *msecs = timeline->priv->msecs_delta;
    }

  return priv->skipped_frames + 1;
}
