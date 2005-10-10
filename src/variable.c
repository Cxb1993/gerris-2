/* Gerris - The GNU Flow Solver
 * Copyright (C) 2001 National Institute of Water and Atmospheric Research
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.  
 */

#include <stdlib.h>
#include "variable.h"

/* GfsVariable: Object */

static void variable_init_domain (GfsVariable * v, GfsDomain * domain)
{
   v->i = gfs_domain_alloc (domain);
   v->domain = domain;
}

static void gfs_variable_read (GtsObject ** o, GtsFile * fp)
{
  GfsDomain * domain;
  GfsVariable * v, * old;

  if (GTS_OBJECT_CLASS (gfs_variable_class ())->parent_class->read)
    (* GTS_OBJECT_CLASS (gfs_variable_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  if (fp->type != GTS_STRING) {
    gts_file_error (fp, "expecting a string (name)");
    return;
  }
  v = GFS_VARIABLE1 (*o);
  v->name = g_strdup (fp->token->str);
  gts_file_next_token (fp);

  domain = (*o)->reserved;
  if ((old = gfs_variable_from_name (domain->variables, v->name))) {
    GSList * i;
    if ((i = g_slist_find (domain->variables_io, old)))
      i->data = v;
    domain->variables = g_slist_remove (domain->variables, old);
    gts_object_destroy (GTS_OBJECT (old));
  }
  variable_init_domain (v, domain);
  domain->variables = g_slist_append (domain->variables, v);
}

static void gfs_variable_write (GtsObject * o, FILE * fp)
{
  if (GTS_OBJECT_CLASS (gfs_variable_class ())->parent_class->write)
    (* GTS_OBJECT_CLASS (gfs_variable_class ())->parent_class->write) (o, fp);
  fprintf (fp, " %s", GFS_VARIABLE1 (o)->name);
}

static void gfs_variable_destroy (GtsObject * object)
{
  GfsVariable * v = GFS_VARIABLE1 (object);

  g_free (v->name);
  if (v->sources)
    gts_object_destroy (GTS_OBJECT (v->sources));
  if (v->surface_bc)
    gts_object_destroy (GTS_OBJECT (v->surface_bc));
  if (v->domain) {
    gfs_domain_free (v->domain, v->i);
    v->domain->variables = g_slist_remove (v->domain->variables, v);
  }

  (* GTS_OBJECT_CLASS (gfs_variable_class ())->parent_class->destroy) (object);
}

static void gfs_variable_class_init (GfsVariableClass * klass)
{
  GTS_OBJECT_CLASS (klass)->read = gfs_variable_read;
  GTS_OBJECT_CLASS (klass)->write = gfs_variable_write;
  GTS_OBJECT_CLASS (klass)->destroy = gfs_variable_destroy;
}

static void gfs_variable_init (GfsVariable * v)
{
  GFS_EVENT (v)->istep = 1;
  v->centered = FALSE;
  v->component = FTT_DIMENSION;
  v->fine_coarse = (GfsVariableFineCoarseFunc) gfs_get_from_below_intensive;
}

GfsVariableClass * gfs_variable_class (void)
{
  static GfsVariableClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_variable_info = {
      "GfsVariable",
      sizeof (GfsVariable),
      sizeof (GfsVariableClass),
      (GtsObjectClassInitFunc) gfs_variable_class_init,
      (GtsObjectInitFunc) gfs_variable_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_event_class ()), &gfs_variable_info);
  }

  return klass;
}

/**
 * gfs_variable_new:
 * @klass: a #GfsVariableClass.
 * @domain: a #GfsDomain.
 * @name: the name of the variable or %NULL.
 *
 * Returns: a newly allocated #GfsVariable or %NULL if a variable
 * named @name already exists in @domain.
 */
GfsVariable * gfs_variable_new (GfsVariableClass * klass,
				GfsDomain * domain,
				const gchar * name)
{
  GfsVariable * v;

  g_return_val_if_fail (klass != NULL, NULL);
  g_return_val_if_fail (domain != NULL, NULL);

  if (name &&
      (gfs_variable_from_name (domain->variables, name) ||
       gfs_derived_variable_from_name (domain->derived_variables, name)))
    return NULL;

  v = GFS_VARIABLE1 (gts_object_new (GTS_OBJECT_CLASS (klass)));
  if (name)
    v->name = g_strdup (name);
  variable_init_domain (v, domain);

  return v;
}

/**
 * gfs_variable_from_name:
 * @i: the list of available #GfsVariable.
 * @name: the name of the variable to find.
 *
 * Returns: the #GfsVariable @name or %NULL if this variable name does
 * not exist.  
 */
GfsVariable * gfs_variable_from_name (GSList * i,
				      const gchar * name)
{
  g_return_val_if_fail (name != NULL, NULL);

  while (i && strcmp (name, GFS_VARIABLE1 (i->data)->name))
    i = i->next;
  return i ? GFS_VARIABLE1 (i->data) : NULL;
}

/**
 * gfs_variables_from_list:
 * @i: the list of available #GfsVariable.
 * @list: a malloc'ed string containing comma separated variable names.
 * @error: where to return the variable name in case of error.
 *
 * Returns: a list of variables or %NULL in case of error, in which
 * case *@error points to the name of the unknown variable.  
 */
GSList * gfs_variables_from_list (GSList * i,
				  gchar * list,
				  gchar ** error)
{
  gchar * s;
  GSList * var = NULL;

  g_return_val_if_fail (i != NULL, NULL);
  g_return_val_if_fail (error != NULL, NULL);

  s = strtok (list, ",");
  while (s) {
    GfsVariable * v = gfs_variable_from_name (i, s);

    if (v == NULL) {
      *error = s;
      g_slist_free (var);
      return NULL;
    }
    var = g_slist_append (var, v);
    s = strtok (NULL, ",");
  }
  return var;
}

/* GfsVariableTracer: object */

static void variable_tracer_read (GtsObject ** o, GtsFile * fp)
{
  (* GTS_OBJECT_CLASS (gfs_variable_tracer_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  if (fp->type == '{')
    gfs_advection_params_read (&GFS_VARIABLE_TRACER (*o)->advection, fp);
  if (fp->type == '{')
    g_warning ("%d:%d: specifying diffusion parameters is not done here anymore!",
	       fp->line, fp->pos);
}

static void variable_tracer_write (GtsObject * o, FILE * fp)
{
  (* GTS_OBJECT_CLASS (gfs_variable_tracer_class ())->parent_class->write) (o, fp);

  fputc (' ', fp);
  gfs_advection_params_write (&GFS_VARIABLE_TRACER (o)->advection, fp);
}

static void variable_tracer_class_init (GtsObjectClass * klass)
{
  klass->read = variable_tracer_read;
  klass->write = variable_tracer_write;
}

static void variable_tracer_init (GfsVariableTracer * v)
{
  gfs_advection_params_init (&v->advection);
  v->advection.gradient = gfs_center_van_leer_gradient;
  v->advection.flux = gfs_face_advection_flux;
  v->advection.v = GFS_VARIABLE1 (v);
  v->advection.fv = NULL;
}

GfsVariableClass * gfs_variable_tracer_class (void)
{
  static GfsVariableClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_variable_tracer_info = {
      "GfsVariableTracer",
      sizeof (GfsVariableTracer),
      sizeof (GfsVariableClass),
      (GtsObjectClassInitFunc) variable_tracer_class_init,
      (GtsObjectInitFunc) variable_tracer_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_variable_class ()), 
				  &gfs_variable_tracer_info);
  }

  return klass;
}

/* GfsVariableResidual: Object */

static void scale_residual (FttCell * cell, GfsVariable * res)
{
  gdouble size = ftt_cell_size (cell);
  gdouble dt = GFS_SIMULATION (res->domain)->advection_params.dt;
  GFS_VARIABLE (cell, res->i) *= dt/(size*size);
}

static gboolean variable_residual_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (gfs_variable_class ())->event) (event, sim)) {
    gfs_domain_cell_traverse (GFS_DOMAIN (sim), FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			      (FttCellTraverseFunc) scale_residual, event);
    return TRUE;
  }
  return FALSE;
}

static void variable_residual_class_init (GfsEventClass * klass)
{
  klass->event = variable_residual_event;
}

GfsVariableClass * gfs_variable_residual_class (void)
{
  static GfsVariableClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_variable_residual_info = {
      "GfsVariableResidual",
      sizeof (GfsVariable),
      sizeof (GfsVariableClass),
      (GtsObjectClassInitFunc) variable_residual_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_variable_class ()), 
				  &gfs_variable_residual_info);
  }

  return klass;
}

/* GfsVariableFiltered: object */

static void variable_filtered_read (GtsObject ** o, GtsFile * fp)
{
  GfsDomain * domain;

  (* GTS_OBJECT_CLASS (gfs_variable_filtered_class ())->parent_class->read) (o, fp);
  if (fp->type == GTS_ERROR)
    return;

  if (fp->type != GTS_STRING) {
    gts_file_error (fp, "expecting a string (v)");
    return;
  }
  domain = GFS_DOMAIN (gfs_object_simulation (*o));
  if (!(GFS_VARIABLE_FILTERED (*o)->v = 
	gfs_variable_from_name (domain->variables, fp->token->str))) {
    gts_file_error (fp, "unknown variable `%s'", fp->token->str);
    return;
  }
  gts_file_next_token (fp);

  if (fp->type != GTS_INT) {
    gts_file_error (fp, "expecting a number (niter)");
    return;
  }
  GFS_VARIABLE_FILTERED (*o)->niter = atoi (fp->token->str);
  gts_file_next_token (fp);
}

static void variable_filtered_write (GtsObject * o, FILE * fp)
{
  (* GTS_OBJECT_CLASS (gfs_variable_filtered_class ())->parent_class->write) (o, fp);

  fprintf (fp, " %s %d", GFS_VARIABLE_FILTERED (o)->v->name, GFS_VARIABLE_FILTERED (o)->niter);
}

static void filter (FttCell * cell, gpointer * data)
{
  FttDirection d[4*(FTT_DIMENSION - 1)][FTT_DIMENSION] = {
#if FTT_2D
    {FTT_RIGHT, FTT_TOP}, {FTT_RIGHT, FTT_BOTTOM}, {FTT_LEFT, FTT_TOP}, {FTT_LEFT, FTT_BOTTOM}
#else
    {FTT_RIGHT, FTT_TOP, FTT_FRONT}, {FTT_RIGHT, FTT_BOTTOM, FTT_FRONT}, 
    {FTT_LEFT, FTT_TOP, FTT_FRONT}, {FTT_LEFT, FTT_BOTTOM, FTT_FRONT},
    {FTT_RIGHT, FTT_TOP, FTT_BACK}, {FTT_RIGHT, FTT_BOTTOM, FTT_BACK}, 
    {FTT_LEFT, FTT_TOP, FTT_BACK}, {FTT_LEFT, FTT_BOTTOM, FTT_BACK}
#endif
  };
  guint i;
  gdouble val = 0.;
  GfsVariable * a = data[0];
  GfsVariable * b = data[1];

  for (i = 0; i < 4*(FTT_DIMENSION - 1); i++)
    val += gfs_cell_corner_value (cell, d[i], a, -1);
  GFS_VARIABLE (cell, b->i) = val/(4*(FTT_DIMENSION - 1));
}

static void variable_filtered_event_half (GfsEvent * event, GfsSimulation * sim)
{
  guint n, niter = 2*GFS_VARIABLE_FILTERED (event)->niter;
  gpointer data[2], tmp;

  data[0] = GFS_VARIABLE_FILTERED (event)->v;
  data[1] = event;
  gfs_domain_cell_traverse (GFS_DOMAIN (sim), FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) filter, data);
  gfs_domain_copy_bc (GFS_DOMAIN (sim), FTT_TRAVERSE_LEAFS, -1,
		      GFS_VARIABLE_FILTERED (event)->v, data[1]);
  data[0] = event;
  data[1] = gfs_temporary_variable (GFS_DOMAIN (sim));
  for (n = 0; n < niter; n++) {
    gfs_domain_cell_traverse (GFS_DOMAIN (sim), FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			      (FttCellTraverseFunc) filter, data);
    gfs_domain_copy_bc (GFS_DOMAIN (sim), FTT_TRAVERSE_LEAFS, -1, 
			GFS_VARIABLE_FILTERED (event)->v, data[1]);
    tmp = data[0]; data[0] = data[1]; data[1] = tmp;
  }
  gts_object_destroy (data[1]);  
}

static gboolean variable_filtered_event (GfsEvent * event, GfsSimulation * sim)
{
  if ((* GFS_EVENT_CLASS (GTS_OBJECT_CLASS (gfs_variable_filtered_class ())->parent_class)->event)
      (event, sim)) {
    variable_filtered_event_half (event, sim);
    return TRUE;
  }
  return FALSE;
}

static void variable_filtered_class_init (GtsObjectClass * klass)
{
  klass->read = variable_filtered_read;
  klass->write = variable_filtered_write;
  GFS_EVENT_CLASS (klass)->event = variable_filtered_event;
  GFS_EVENT_CLASS (klass)->event_half = variable_filtered_event_half;
}

GfsVariableClass * gfs_variable_filtered_class (void)
{
  static GfsVariableClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_variable_filtered_info = {
      "GfsVariableFiltered",
      sizeof (GfsVariableFiltered),
      sizeof (GfsVariableClass),
      (GtsObjectClassInitFunc) variable_filtered_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_variable_class ()), 
				  &gfs_variable_filtered_info);
  }

  return klass;
}
