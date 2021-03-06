#ifndef __CLUTTER_STAGE_EGL_H__
#define __CLUTTER_STAGE_EGL_H__

#include <glib-object.h>
#include <clutter/clutter-stage.h>

#include "clutter-egl-headers.h"
#include "clutter-backend-egl.h"

#define CLUTTER_TYPE_STAGE_EGL                  (clutter_stage_egl_get_type ())
#define CLUTTER_STAGE_EGL(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_STAGE_EGL, ClutterStageEGL))
#define CLUTTER_IS_STAGE_EGL(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_STAGE_EGL))
#define CLUTTER_STAGE_EGL_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_STAGE_EGL, ClutterStageEGLClass))
#define CLUTTER_IS_STAGE_EGL_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_STAGE_EGL))
#define CLUTTER_STAGE_EGL_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_STAGE_EGL, ClutterStageEGLClass))

typedef struct _ClutterStageEGL         ClutterStageEGL;
typedef struct _ClutterStageEGLClass    ClutterStageEGLClass;

struct _ClutterStageEGL
{
  ClutterActor parent_instance;

  /* from the backend */
  gint         surface_width;
  gint         surface_height;

  EGLSurface   egl_surface;

  /* the stage wrapper */
  ClutterStage      *wrapper;
  ClutterBackendEGL *backend;
};

struct _ClutterStageEGLClass
{
  ClutterActorClass parent_class;
};

GType clutter_stage_egl_get_type (void) G_GNUC_CONST;

#endif /* __CLUTTER_STAGE_EGL_H__ */
