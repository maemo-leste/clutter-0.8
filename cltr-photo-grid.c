#include "cltr-photo-grid.h"

/* this likely shouldn'y go here */
static GMutex *Mutex_GRID = NULL;

ClutterPhotoGridCell*
cltr_photo_grid_cell_new(ClutterPhotoGrid *grid,
			 Pixbuf           *pixb,
			 const gchar      *filename)
{
  ClutterPhotoGridCell *cell = NULL;
  int                   maxw = grid->width, maxh = grid->height;
  int                   neww = 0, newh = 0;

  cell = g_malloc0(sizeof(ClutterPhotoGridCell));

  if (pixb->width > pixb->height) /* landscape */
    {
      if (pixb->width > maxw)
	{
	  neww = maxw;
	  newh = (neww * pixb->height) / pixb->width;
	}
    }
  else                            /* portrait */
    {
      if (pixb->height > maxh)
	{
	  newh = maxh;
	  neww = (newh * pixb->width) / pixb->height;
	}
    }

  if (neww || newh)
    {
      cell->pixb = pixbuf_scale_down(pixb, neww, newh);
      pixbuf_unref(pixb);
    }
  else cell->pixb = pixb;

  CLTR_DBG ("loaded %s at %ix%i", filename, neww, newh);

  cell->angle = 6.0 - (rand()%12);

  cell->anim_step = 15;
  cell->state = CLTR_PHOTO_GRID_CELL_STATE_APPEARING;

  return cell;
}

void
cltr_photo_grid_append_cell(ClutterPhotoGrid     *grid,
			    ClutterPhotoGridCell *cell)
{
  grid->cells_tail = g_list_append(grid->cells_tail, cell);


} 

static void
ctrl_photo_grid_cell_to_coords(ClutterPhotoGrid *grid,
			       GList            *cell,
			       int              *x,
			       int              *y)
{
  int idx;

  idx = g_list_position(grid->cells_tail, cell);

  *y = idx / grid->n_cols;
  *x = idx % grid->n_cols;

  CLTR_DBG("idx: %i x: %i, y: %i", idx, *x , *y);
}

static void
ctrl_photo_grid_get_zoomed_coords(ClutterPhotoGrid *grid,
				  int              x,
				  int              y,
				  float           *tx,
				  float           *ty)
{
  /* 
   * figure out translate co-ords for the cell at x,y to get translated
   * so its centered for glScale to zoom in on it.
  */

  *tx = (float)grid->cell_width  * (grid->zoom_max) * x * -1.0;
  *ty = (float)grid->cell_height * (grid->zoom_max) * y * -1.0;
}

gboolean
cltr_photo_grid_idle_cb(gpointer data)
{
  ClutterPhotoGrid *grid = (ClutterPhotoGrid *)data;

  cltr_photo_grid_redraw(grid);

  switch(grid->state)
    {
    case CLTR_PHOTO_GRID_STATE_LOADING:
    case CLTR_PHOTO_GRID_STATE_LOAD_COMPLETE:
    case CLTR_PHOTO_GRID_STATE_ZOOM_IN:
    case CLTR_PHOTO_GRID_STATE_ZOOM_OUT:
    case CLTR_PHOTO_GRID_STATE_ZOOMED_MOVE:
      return TRUE;
    case CLTR_PHOTO_GRID_STATE_ZOOMED:
    case CLTR_PHOTO_GRID_STATE_BROWSE:        
    default:
      return FALSE;  /* no need for rapid updates now  */
    }
}


void
cltr_photo_grid_navigate(ClutterPhotoGrid *grid,
			 CltrDirection     direction) 
{
  GList *cell_orig = grid->cell_active;

  switch (direction)
    {
    case CLTR_SOUTH:
      if (g_list_nth(grid->cell_active, grid->n_cols))
	grid->cell_active = g_list_nth(grid->cell_active, grid->n_cols);
      break;
    case CLTR_NORTH:
      if (g_list_nth_prev(grid->cell_active, grid->n_cols))
	grid->cell_active = g_list_nth_prev(grid->cell_active, grid->n_cols);
      break;
    case CLTR_EAST:
      if (g_list_next(grid->cell_active))
	grid->cell_active = g_list_next(grid->cell_active);
      break;
    case CLTR_WEST:
      if (g_list_previous(grid->cell_active))
	grid->cell_active = g_list_previous(grid->cell_active);
      break;
    }

  if (cell_orig != grid->cell_active) /* we've moved */
    {
      int   x, y;
      float zoom = grid->zoom_min;

      if (grid->state == CLTR_PHOTO_GRID_STATE_ZOOMED)
	{
	  grid->state      = CLTR_PHOTO_GRID_STATE_ZOOMED_MOVE;
	  grid->view_min_x = grid->view_max_x; 
	  grid->view_min_y = grid->view_max_y ;
	  grid->anim_step  = 0;
	  zoom             = grid->zoom_max;

	  g_idle_add(cltr_photo_grid_idle_cb, grid);
	}
	  
      ctrl_photo_grid_cell_to_coords(grid, grid->cell_active, &x, &y);

      ctrl_photo_grid_get_zoomed_coords(grid, x, y,
					&grid->view_max_x,
					&grid->view_max_y);
				       
      CLTR_DBG("x: %f, y: %f", grid->view_max_x , grid->view_max_y);

      cltr_photo_grid_redraw(grid);
    }
}

void 				/* bleh badly named */
cltr_photo_grid_activate_cell(ClutterPhotoGrid *grid)
{
  if (grid->state == CLTR_PHOTO_GRID_STATE_BROWSE)
    {
      grid->state = CLTR_PHOTO_GRID_STATE_ZOOM_IN;

      g_idle_add(cltr_photo_grid_idle_cb, grid);
    }
  else if (grid->state == CLTR_PHOTO_GRID_STATE_ZOOMED)
    {
      grid->state = CLTR_PHOTO_GRID_STATE_ZOOM_OUT;
	/* reset - zoomed moving will have reset */
      grid->view_min_x = 0.0; 
      grid->view_min_y = 0.0;

      g_idle_add(cltr_photo_grid_idle_cb, grid);
    }

  /* que a draw ? */
}			      


gpointer
cltr_photo_grid_populate(gpointer data) 
{
  ClutterPhotoGrid *grid = (ClutterPhotoGrid *)data;
  GDir             *dir;
  GError           *error;
  const gchar      *entry = NULL;
  gchar            *fullpath = NULL;
  int               n_pixb = 0, i =0;
  ClutterFont      *font = NULL;

  font = font_new("Sans Bold 96");

  if ((dir = g_dir_open (grid->img_path, 0, &error)) == NULL)
    {
      /* handle this much better */
      fprintf(stderr, "failed to open '%s'\n", grid->img_path);
      return NULL;
    }

  while ((entry = g_dir_read_name (dir)) != NULL)
    {
      if (!strcasecmp(&entry[strlen(entry)-4], ".png")
	  || !strcasecmp(&entry[strlen(entry)-4], ".jpg")
	  || !strcasecmp(&entry[strlen(entry)-5], ".jpeg"))
	n_pixb++;
    }

  CLTR_DBG("estamited %i pixb's\n", n_pixb);

  /*
  grid->texs = util_malloc0(sizeof(GLuint)*n_pixb);
  glGenTextures(n_pixb, grid->texs);
  */

  g_dir_rewind (dir);

  while ((entry = g_dir_read_name (dir)) != NULL)
    {
      Pixbuf *pixb = NULL; 
      fullpath = g_strconcat(grid->img_path, "/", entry, NULL);
 
      pixb = pixbuf_new_from_file(fullpath);

      if (pixb)
	{
	  ClutterPhotoGridCell *cell;
	  gchar                 buf[24];

	  cell = cltr_photo_grid_cell_new(grid, pixb, entry);

	  g_snprintf(&buf[0], 24, "%i", i);
	  font_draw(font, cell->pixb, buf, 10, 10);

	  g_mutex_lock(Mutex_GRID);

	  cell->img = cltr_image_new(cell->pixb);

	  g_mutex_unlock(Mutex_GRID);

	  cltr_photo_grid_append_cell(grid, cell);

	  i++;
	}

      g_free(fullpath);
    }

  g_dir_close (dir);

  g_mutex_lock(Mutex_GRID);

  grid->cell_active = g_list_first(grid->cells_tail);

  grid->state = CLTR_PHOTO_GRID_STATE_LOAD_COMPLETE;

  g_mutex_unlock(Mutex_GRID);

  cltr_photo_grid_redraw(grid);

  return NULL;
}

void
cltr_photo_grid_redraw(ClutterPhotoGrid *grid)
{
  int x = 0, y = 0, rows = grid->n_rows, cols = 0, i =0;
  GList *cell_item;
  float zoom, trans_x, trans_y;

  /* CLTR_MARK();*/

  glPushMatrix();

  glClear(GL_COLOR_BUFFER_BIT);

  if (grid->cells_tail == NULL)
    {
      /* No pictures to paint yet */
      glColor3f(0.6, 0.6, 0.62);
      glRecti(0, 0, 640, 480);

      glPopMatrix();
      glXSwapBuffers(CltrCntx.xdpy, grid->parent->xwin);  
      return;
    }

  /*
   * Using GL_POLYGON_SMOOTH with 'regular' alpha blends causes ugly seems
   * in the textures and texture tile borders. We therefore do this 'saturate'
   * trick painting front -> back.
   * 
   * see http://blog.metawrap.com/blog/PermaLink.aspx?guid=db82f92e-9fc8-4635-b3e5-e37a1ca6ee0a 
   * for more info
   *
   * Is there a better way.?
  */

  glClearColor( 0.0, 0.0, 0.0, 0.0 ); /* needed for saturate to work */

  glEnable(GL_BLEND);

  glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST); /* needed  */

  glEnable(GL_POLYGON_SMOOTH);

  glDisable(GL_LIGHTING); 
  glDisable(GL_DEPTH_TEST);

  glBlendFunc(GL_SRC_ALPHA_SATURATE,GL_ONE);

  /* Assume zoomed out */
  zoom    = grid->zoom_min;
  trans_x = grid->view_min_x;
  trans_y = grid->view_min_y;

  if (grid->state != CLTR_PHOTO_GRID_STATE_BROWSE
      && grid->state != CLTR_PHOTO_GRID_STATE_LOADING
      && grid->state != CLTR_PHOTO_GRID_STATE_LOAD_COMPLETE)
    {
      /* Assume zoomed in */
      zoom    = grid->zoom_max; 
      trans_x = grid->view_max_x;
      trans_y = grid->view_max_y;

      if (grid->state == CLTR_PHOTO_GRID_STATE_ZOOM_IN)
	{
	  grid->anim_step++;

	  /* Are we zoomed all the way in > */
	  if (grid->anim_step >= grid->anim_n_steps)
	    {
	      grid->state     = CLTR_PHOTO_GRID_STATE_ZOOMED;
	      grid->anim_step = 0;
	      /* zoom            = grid->zoom_max; set above */
	    }
	  else 
	    {
	      float f = (float)grid->anim_step/grid->anim_n_steps;

	      zoom = grid->zoom_min + ((grid->zoom_max - grid->zoom_min) * f);
	      trans_x = (grid->view_max_x - grid->view_min_x) * f;
	      trans_y = (grid->view_max_y - grid->view_min_y) * f;
	    }
	
	} 
      else if (grid->state == CLTR_PHOTO_GRID_STATE_ZOOM_OUT)
	{
	  grid->anim_step++;
	  
	  if (grid->anim_step >= grid->anim_n_steps)
	    {
	      zoom            = grid->zoom_min;
	      grid->anim_step = 0;
	      trans_x         = grid->view_min_x;
	      trans_y         = grid->view_min_y;
	      grid->state     = CLTR_PHOTO_GRID_STATE_BROWSE;
	    }
	  else 
	    {
	      float f = (float)(grid->anim_n_steps - grid->anim_step ) 
		        / grid->anim_n_steps;

	      zoom = grid->zoom_min + (grid->zoom_max - grid->zoom_min) * f;
	      trans_x = (grid->view_max_x - grid->view_min_x) * f;
	      trans_y = (grid->view_max_y - grid->view_min_y) * f;
	    }
	}
      else if (grid->state == CLTR_PHOTO_GRID_STATE_ZOOMED_MOVE)
	{
	  grid->anim_step++;

	  if (grid->anim_step >= grid->anim_n_steps)
	    {
	      grid->state     = CLTR_PHOTO_GRID_STATE_ZOOMED;
	      grid->anim_step = 0;
	    }
	  else
	    {
	      float f = (float)grid->anim_step/grid->anim_n_steps;

	      trans_x = grid->view_min_x + ((grid->view_max_x - grid->view_min_x) * f);
	      trans_y = grid->view_min_y + ((grid->view_max_y - grid->view_min_y) * f);

	    }
	}


    }

  glTranslatef( trans_x, trans_y, 0.0);
  glScalef( zoom, zoom, 0.0);

  cell_item = g_list_first(grid->cells_tail);

  while (rows--)
    {
      cols = grid->n_cols;
      x = 0; 
      while (cols--)
	{
	  ClutterPhotoGridCell *cell = (ClutterPhotoGridCell *)cell_item->data;
	  Pixbuf *pixb = NULL;
	  float   tx, ty;
	  int     x1, x2, y1, y2, thumb_w, thumb_h;
	  int     ns_border, ew_border;

	  pixb = cell->pixb;

	  thumb_w = (pixb->width  / grid->n_cols);
	  thumb_h = (pixb->height / grid->n_rows);

	  if (cell->state == CLTR_PHOTO_GRID_CELL_STATE_APPEARING)
	    {
	      cell->anim_step -= 4;

	      if (cell->anim_step <= 0)
		{
		  cell->state = CLTR_PHOTO_GRID_CELL_STATE_STATIC;
		}
	      else
		{
		  thumb_w = thumb_w + cell->anim_step;
		  thumb_h = thumb_h + cell->anim_step;
		}

	      cell->anim_step = 0;
	    }

	  ew_border = thumb_w/8;
	  ns_border = thumb_h/8; 

	  thumb_w -= (2 * ew_border);
	  thumb_h -= (2 * ns_border);

	  x1 = x + ((grid->cell_width - thumb_w)/2);
	  y1 = y + ((grid->cell_height - thumb_h)/2);

	  x2 = x1 + thumb_w;
	  y2 = y1 + thumb_h;

	  tx = (float) pixb->width  / grid->tex_w;
	  ty = (float) pixb->height / grid->tex_h;

	  glPushMatrix();

	  /* Translate origin to rotation point ( photo center ) */
	  glTranslatef( x1 + ((x2-x1)/2), y1 + ((y2-y1)/2), 0.0);

	  if (cell->state != CLTR_PHOTO_GRID_CELL_STATE_APPEARING)
	    /* Rotate around Z axis */
	    glRotatef ( cell->angle, 0.0, 0.0, 1.0);

	  glEnable(GL_TEXTURE_2D);

	  g_mutex_lock(Mutex_GRID);

	  cltr_image_render_to_gl_quad(cell->img,
				       -(thumb_w/2),
				       -(thumb_h/2),
				       (thumb_w/2),
				       (thumb_h/2));

	  g_mutex_unlock(Mutex_GRID);

	  glDisable(GL_TEXTURE_2D);

	  if (cell_item == grid->cell_active 
	      && grid->state == CLTR_PHOTO_GRID_STATE_BROWSE)
	    glColor4f(1.0, 1.0, 1.0, 1.0);
	  else
	    glColor4f(0.9, 0.95, 0.95, 1.0);

	  /* Draw with origin in center of photo */

	  glRecti(-(thumb_w/2)-4, -(thumb_h/2)-4, 
		  (thumb_w/2)+4, (thumb_h/2)+ns_border);

	  glEnable(GL_TEXTURE_2D);

	    /* back to regular non translated matrix */
	   glPopMatrix();

	  cell_item = g_list_next(cell_item);

	  if (!cell_item)
	    goto finish;

	  x += grid->cell_width;
	  i++;
	}
      y += grid->cell_height;
    }

 finish:

  glPopMatrix();

  /* finally paint background  */
  glDisable(GL_TEXTURE_2D);
  glColor3f(0.6, 0.6, 0.62);
  glRecti(0, 0, 640, 480);

  glXSwapBuffers(CltrCntx.xdpy, grid->parent->xwin);  

  g_mutex_lock(Mutex_GRID);

  /* Hack, so final item get painted via threaded load */
  if (grid->state == CLTR_PHOTO_GRID_STATE_LOAD_COMPLETE)
    grid->state = CLTR_PHOTO_GRID_STATE_BROWSE;

  g_mutex_unlock(Mutex_GRID);

}

ClutterPhotoGrid*
cltr_photo_grid_new(ClutterWindow *win, 
		    int            n_cols,
		    int            n_rows,
		    const gchar   *img_path)
{
  ClutterPhotoGrid *grid = NULL;
  GThread          *loader_thread; 

  grid = util_malloc0(sizeof(ClutterPhotoGrid));

  grid->img_path = strdup(img_path);

  grid->width  = win->width;
  grid->height = win->height;
  grid->n_cols = n_cols;
  grid->n_rows = n_rows;
  grid->parent = win;

  grid->cell_width  = grid->width  / n_cols;
  grid->cell_height = grid->height / n_rows;

  grid->state = CLTR_PHOTO_GRID_STATE_LOADING;

  grid->anim_n_steps = 50; /* value needs to be calced dep on rows */
  grid->anim_step    = 0;

  /* 
     grid->zoom_step = 0.05;
     grid->zoom      = 1.0;
  */
  grid->zoom_min  = 1.0;		      
  grid->view_min_x = 0.0;
  grid->view_min_y = 0.0;


  /* Assmes cols == rows */
  grid->zoom_max  = /* 1.0 + */  (float) (n_rows * 1.0) - 0.3;

  /* Below needs to go else where - some kind of texture manager/helper */

  grid->tex_w    = 1024;   
  grid->tex_h    = 512;

  grid->tex_data = malloc (grid->tex_w * grid->tex_h * 4);

  Mutex_GRID = g_mutex_new();

  /* Load  */

  loader_thread = g_thread_create (cltr_photo_grid_populate,
				   (gpointer)grid,
				   TRUE,
				   NULL);

  /*
  ctrl_photo_grid_cell_to_coords(grid, grid->cell_active, &x, &y);


  ctrl_photo_grid_get_zoomed_coords(grid, grid->zoom_max,
				    x, y, 
				    &grid->view_max_x,
				    &grid->view_max_y);
  */

  g_idle_add(cltr_photo_grid_idle_cb, grid);

  return grid;
}
