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
#include <math.h>
#include <gmodule.h>
#include "config.h"
#include "gfsconfig.h"
#include "simulation.h"
#include "output.h"
#include "refine.h"
#include "solid.h"
#include "adaptive.h"
#include "source.h"
#include "vof.h"
#include "tension.h"

/* GfsSimulation: object */

static void simulation_destroy (GtsObject * object)
{
  GfsSimulation * sim = GFS_SIMULATION (object);

  if (sim->surface)
    gts_object_destroy (GTS_OBJECT (sim->surface));

  gts_container_foreach (GTS_CONTAINER (sim->refines),
			 (GtsFunc) gts_object_destroy, NULL);
  gts_object_destroy (GTS_OBJECT (sim->refines));

  gts_container_foreach (GTS_CONTAINER (sim->events),
			 (GtsFunc) gts_object_destroy, NULL);
  gts_object_destroy (GTS_OBJECT (sim->events));
  gts_object_destroy (GTS_OBJECT (sim->adapts));

  g_slist_foreach (sim->modules, (GFunc) g_module_close, NULL);
  g_slist_free (sim->modules);
  g_slist_free (sim->variables);
  g_slist_foreach (sim->globals, (GFunc) gts_object_destroy, NULL);
  g_slist_free (sim->globals);

  (* GTS_OBJECT_CLASS (gfs_simulation_class ())->parent_class->destroy) (object);
}

static void simulation_write (GtsObject * object, FILE * fp)
{
  GfsSimulation * sim = GFS_SIMULATION (object);
  GSList * i;
  GfsVariable * v;

  (* GTS_OBJECT_CLASS (gfs_simulation_class ())->parent_class->write)
    (object, fp);

  fputs (" {\n", fp);
  i = sim->globals;
  while (i) {
    fputs ("  ", fp);
    (* GTS_OBJECT (i->data)->klass->write) (i->data, fp);
    i = i->next;
  }
  fputs ("  GfsTime ", fp);
  gfs_time_write (&sim->time, fp);
  fputs ("\n  GfsPhysicalParams ", fp);
  gfs_physical_params_write (&sim->physical_params, fp);
  fputs ("\n  GfsAdvectionParams ", fp);
  gfs_advection_params_write (&sim->advection_params, fp);
  fputs ("\n  GfsApproxProjectionParams ", fp);
  gfs_multilevel_params_write (&sim->approx_projection_params, fp);
  fputs ("\n  GfsProjectionParams ", fp);
  gfs_multilevel_params_write (&sim->projection_params, fp);
  fputc ('\n', fp);

  i = sim->variables;
  while (i) {
    fputs ("  ", fp);
    (* GTS_OBJECT (i->data)->klass->write) (i->data, fp);
    fputc ('\n', fp);
    i = i->next;
  }

  i = sim->modules;
  while (i) {
    fprintf (fp, "  GModule %s\n", g_module_name (i->data));
    i = i->next;
  }

  i = GFS_DOMAIN (sim)->variables;
  while (i) {
    v = i->data;
    if (v->surface_bc) {
      fputs ("  ", fp);
      (* GTS_OBJECT (v->surface_bc)->klass->write) (GTS_OBJECT (v->surface_bc), fp);
      fputc ('\n', fp);
    }
    i = i->next;
  }

  if (GFS_DOMAIN (sim)->max_depth_write < -1) {
    i = sim->refines->items;
    while (i) {
      GtsObject * object = i->data;
      
      fputs ("  ", fp);
      g_assert (object->klass->write);
      (* object->klass->write) (object, fp);
      fputc ('\n', fp);
      i = i->next;
    }
  }

  i = sim->events->items;
  while (i) {
    GtsObject * object = i->data;
    GfsEvent * event = i->data;
    
    if (event->t < event->end && event->i < event->iend) {
      fputs ("  ", fp);
      g_assert (object->klass->write);
      (* object->klass->write) (object, fp);
      fputc ('\n', fp);
    }
    i = i->next;
  }

  if (sim->surface && sim->output_surface) {
    fputs ("  GtsSurface { ", fp);
    if (GFS_DOMAIN (sim)->binary) {
      gboolean binary = GTS_POINT_CLASS (sim->surface->vertex_class)->binary;
      GTS_POINT_CLASS (sim->surface->vertex_class)->binary = TRUE;
      gts_surface_write (sim->surface, fp);
      GTS_POINT_CLASS (sim->surface->vertex_class)->binary = binary;
    }
    else
      gts_surface_write (sim->surface, fp);
    fputs ("}\n", fp);
  }
  fputc ('}', fp);
}

static void check_solid_surface (GtsSurface * s, 
				 const gchar * fname,
				 GtsFile * fp)
{
  GString * name = g_string_new ("surface");

  if (fname) {
    g_string_append (name, " `");
    g_string_append (name, fname);
    g_string_append_c (name, '\'');
  }

  if (!gts_surface_is_orientable (s))
    gts_file_error (fp, "%s is not orientable", name->str);
  g_string_free (name, TRUE);
}

static GtsSurface * read_surface_file (GtsFile * fp, GtsSurface * surface)
{
  GtsSurface * s;
  FILE * fptr;
  GtsFile * fp1;
  
  gts_file_next_token (fp);
  if (fp->type != GTS_STRING) {
    gts_file_error (fp, "expecting a string (filename)");
    return NULL;
  }
  s = gts_surface_new (gts_surface_class (),
		       gts_face_class (),
		       gts_edge_class (),
		       surface ? surface->vertex_class : gts_vertex_class ());
  fptr = fopen (fp->token->str, "rt");
  if (fptr == NULL) {
    gts_file_error (fp, "cannot open file `%s'", fp->token->str);
    return NULL;
  }
  fp1 = gts_file_new (fptr);
  if (gts_surface_read (s, fp1)) {
    gts_file_error (fp, 
		    "file `%s' is not a valid GTS file\n"
		    "%s:%d:%d: %s",
		    fp->token->str, fp->token->str,
		    fp1->line, fp1->pos, fp1->error);
    gts_file_destroy (fp1);
    fclose (fptr);
    gts_object_destroy (GTS_OBJECT (s));
    return NULL;
  }
  gts_file_destroy (fp1);
  fclose (fptr);
  
  check_solid_surface (s, fp->token->str, fp);
  if (fp->type == GTS_ERROR) {
    gts_object_destroy (GTS_OBJECT (s));
    return NULL;
  }
  
  if (surface) {
    gts_surface_merge (surface, s);
    gts_object_destroy (GTS_OBJECT (s));
    return surface;
  }
  return s;
}

static GtsSurface * read_surface (GtsFile * fp, GtsSurface * surface)
{
  GtsSurface * s;

  gts_file_next_token (fp);
  if (fp->type != '{') {
    gts_file_error (fp, "expecting an opening brace");
    return NULL;
  }
  fp->scope_max++;
  gts_file_next_token (fp);
  
  s = gts_surface_new (gts_surface_class (),
		       gts_face_class (),
		       gts_edge_class (),
		       surface ? surface->vertex_class : gts_vertex_class ());
  
  if (gts_surface_read (s, fp)) {
    gts_object_destroy (GTS_OBJECT (s));
    return NULL;
  }
  if (fp->type != '}') {
    gts_object_destroy (GTS_OBJECT (s));
    gts_file_error (fp, "expecting a closing brace");
    return NULL;
  }
  fp->scope_max--;
  
  check_solid_surface (s, NULL, fp);
  if (fp->type == GTS_ERROR) {
    gts_object_destroy (GTS_OBJECT (s));
    return NULL;
  }
  
  if (surface) {
    gts_surface_merge (surface, s);
    gts_object_destroy (GTS_OBJECT (s));
    return surface;
  }
  return s;
}

static gboolean strmatch (const gchar * s, const gchar * s1)
{
  gboolean m = !strcmp (s, s1);

  if (!m) {
    gchar * s2 = g_strconcat ("Gfs", s, NULL);
    m = !strcmp (s2, s1);
    g_free (s2);
  }
  return m;
}

static void simulation_read (GtsObject ** object, GtsFile * fp)
{
  GfsSimulation * sim = GFS_SIMULATION (*object);
  
  (* GTS_OBJECT_CLASS (gfs_simulation_class ())->parent_class->read) (object, fp);
  if (fp->type == GTS_ERROR)
    return;

  if (fp->type != '{') {
    gts_file_error (fp, "expecting an opening brace");
    return;
  }
  fp->scope_max++;
  gts_file_next_token (fp);

  while (fp->type != GTS_ERROR && fp->type != '}') {
    if (fp->type == '\n') {
      gts_file_next_token (fp);
      continue;
    }
    if (fp->type != GTS_STRING) {
      gts_file_error (fp, "expecting a keyword");
      return;
    }

    /* ------------ GtsSurface ------------ */
    if (strmatch (fp->token->str, "GtsSurface")) {
      GtsSurface * s;
      
      if ((s = read_surface (fp, sim->surface)) == NULL)
	return;
      sim->surface = s;
      gts_file_next_token (fp);
    }

    /* ------------ GtsSurfaceFile ------------ */
    else if (strmatch (fp->token->str, "GtsSurfaceFile")) {
      GtsSurface * s;

      if ((s = read_surface_file (fp, sim->surface)) == NULL)
	return;
      sim->surface = s;
      gts_file_next_token (fp);
    }

    /* ------------ GModule ------------ */
    else if (strmatch (fp->token->str, "GModule")) {
      gts_file_next_token (fp);
      if (fp->type != GTS_STRING) {
	gts_file_error (fp, "expecting a string (filename)");
	return;
      }
      if (!g_module_supported ())
	g_warning ("modules are not supported on this system");
      else {
	gchar * name, * path;
	GModule * module;

	name = g_strconcat (fp->token->str, FTT_DIMENSION == 2 ? "2D" : "3D", NULL);
	path = g_module_build_path (GFS_MODULES_DIR, name);
	g_free (name);
	module = g_module_open (path, 0);
	g_free (path);
	if (module == NULL)
	  module = g_module_open (fp->token->str, 0);
	if (module == NULL) {
	  gts_file_error (fp, "cannot load module: %s", g_module_error ());
	  return;
	}
	g_module_make_resident (module);
	sim->modules = g_slist_prepend (sim->modules, module);
      }
      gts_file_next_token (fp);
    }

    /* ------------ GfsTime ------------ */
    else if (strmatch (fp->token->str, "GfsTime")) {
      gts_file_next_token (fp);
      gfs_time_read (&sim->time, fp);
      if (fp->type == GTS_ERROR)
	return;
    }

    /* ------------ GfsPhysicalParams ------------ */
    else if (strmatch (fp->token->str, "GfsPhysicalParams")) {
      gts_file_next_token (fp);
      gfs_physical_params_read (&sim->physical_params, GFS_DOMAIN (sim), fp);
      if (fp->type == GTS_ERROR)
	return;
    }

    /* ------------ GfsProjectionParams ------------ */
    else if (strmatch (fp->token->str, "GfsProjectionParams")) {
      gts_file_next_token (fp);
      gfs_multilevel_params_read (&sim->projection_params, fp);
      if (fp->type == GTS_ERROR)
	return;
    }

    /* ------------ GfsApproxProjectionParams ------------ */
    else if (strmatch (fp->token->str, "GfsApproxProjectionParams")) {
      gts_file_next_token (fp);
      gfs_multilevel_params_read (&sim->approx_projection_params, fp);
      if (fp->type == GTS_ERROR)
	return;
    }

    /* ------------ GfsAdvectionParams ------------ */
    else if (strmatch (fp->token->str, "GfsAdvectionParams")) {
      gts_file_next_token (fp);
      gfs_advection_params_read (&sim->advection_params, fp);
      if (fp->type == GTS_ERROR)
	return;
    }

    /* ------------ GtsObject ------------ */
    else {
      GtsObjectClass * klass = gfs_object_class_from_name (fp->token->str);
      GtsObject * object;

      if (klass == NULL ||
	  (!gts_object_class_is_from_class (klass, gfs_global_class ()) &&
	   !gts_object_class_is_from_class (klass, gfs_refine_class ()) &&
	   !gts_object_class_is_from_class (klass, gfs_event_class ()) &&
	   !gts_object_class_is_from_class (klass, gfs_surface_generic_bc_class ()))) {
	gts_file_error (fp, "unknown keyword `%s'", fp->token->str);
	return;
      }

      object = gts_object_new (klass);
      gfs_object_simulation_set (object, sim);

      g_assert (klass->read);
      (* klass->read) (&object, fp);
      if (fp->type == GTS_ERROR) {
	gts_object_destroy (object);
	return;
      }

      if (GFS_IS_GLOBAL (object))
	sim->globals = g_slist_append (sim->globals, object);
      else if (GFS_IS_REFINE (object))
	gts_container_add (GTS_CONTAINER (sim->refines), GTS_CONTAINEE (object));
      else if (GFS_IS_ADAPT (object)) {
	gts_container_add (GTS_CONTAINER (sim->adapts), GTS_CONTAINEE (object));
	gts_container_add (GTS_CONTAINER (sim->events), GTS_CONTAINEE (object));
      }
      else if (GFS_IS_EVENT (object))
	gts_container_add (GTS_CONTAINER (sim->events), GTS_CONTAINEE (object));
      else if (GFS_IS_SURFACE_GENERIC_BC (object))
	;
      else
	g_assert_not_reached ();
    }
  }
  
  if (fp->type != '}') {
    gts_file_error (fp, "expecting a closing brace");
    return;
  }
  fp->scope_max--;
  gts_file_next_token (fp);

  sim->refines->items = g_slist_reverse (sim->refines->items);
  sim->adapts->items = g_slist_reverse (sim->adapts->items);
  sim->events->items = g_slist_reverse (sim->events->items);
  sim->modules = g_slist_reverse (sim->modules);
}

static void advance_tracers (GfsDomain * domain, gdouble dt)
{
  GSList * i = domain->variables;
  while (i) {
    if (GFS_IS_VARIABLE_TRACER_VOF (i->data)) {
      GfsVariableTracer * t = i->data;
      
      t->advection.dt = dt;
      gfs_tracer_vof_advection (domain, &t->advection);
      gfs_domain_variable_centered_sources (domain, i->data, i->data, t->advection.dt);
    }
    else if (GFS_IS_VARIABLE_TRACER (i->data)) {
      GfsVariableTracer * t = i->data;
      
      t->advection.dt = dt;
      gfs_tracer_advection_diffusion (domain, &t->advection);
      gfs_domain_cell_traverse (domain,
				FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
				(FttCellTraverseFunc) GFS_VARIABLE1 (t)->fine_coarse, t);
    }
    i = i->next;
  }  
}

static void simulation_run (GfsSimulation * sim)
{
  GfsVariable * p, * pmac, * res = NULL;
  GfsDomain * domain;
  GSList * i;

  domain = GFS_DOMAIN (sim);

  p = gfs_variable_from_name (domain->variables, "P");
  g_assert (p);
  pmac = gfs_variable_from_name (domain->variables, "Pmac");
  g_assert (pmac);

  gfs_simulation_refine (sim);
  gfs_simulation_init (sim);

  i = domain->variables;
  while (i) {
    if (GFS_IS_VARIABLE_RESIDUAL (i->data))
      res = i->data;
    i = i->next;
  }

  gfs_simulation_set_timestep (sim);
  gfs_approximate_projection (domain,
      			      &sim->approx_projection_params,
      			      &sim->advection_params,
			      p, sim->physical_params.alpha, res);
  if (sim->time.i == 0)
    advance_tracers (domain, sim->advection_params.dt/2.);

  while (sim->time.t < sim->time.end &&
	 sim->time.i < sim->time.iend) {
    GfsVariable * g[FTT_DIMENSION];
    gdouble tstart = gfs_clock_elapsed (domain->timer);

    gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_do, sim);

    gfs_simulation_set_timestep (sim);

    gfs_predicted_face_velocities (domain, FTT_DIMENSION, &sim->advection_params);
    
    gfs_variables_swap (p, pmac);
    gfs_mac_projection (domain,
    			&sim->projection_params, 
    			&sim->advection_params,
			p, sim->physical_params.alpha, g);
    gfs_variables_swap (p, pmac);

    gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_half_do, sim);

    gfs_centered_velocity_advection_diffusion (domain,
					       FTT_DIMENSION,
					       &sim->advection_params,
					       g,
					       sim->physical_params.alpha);

    if (gfs_has_source_coriolis (domain)) {
      gfs_poisson_coefficients (domain, sim->physical_params.alpha);
      gfs_correct_normal_velocities (domain, 2, p, g, 0., NULL);
      gfs_correct_centered_velocities (domain, 2, g, sim->advection_params.dt);
      gfs_source_coriolis_implicit (domain, sim->advection_params.dt);
      gfs_correct_normal_velocities (domain, 2, p, g, 0., NULL);
      gfs_correct_centered_velocities (domain, 2, g, -sim->advection_params.dt);
    }

    gfs_domain_cell_traverse (domain,
			      FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
			      (FttCellTraverseFunc) gfs_cell_coarse_init, domain);
    gfs_simulation_adapt (sim);

    gfs_approximate_projection (domain,
   				&sim->approx_projection_params, 
    				&sim->advection_params, p, sim->physical_params.alpha, res);

    advance_tracers (domain, sim->advection_params.dt);

    sim->time.t = sim->tnext;
    sim->time.i++;

    gts_range_add_value (&domain->timestep, gfs_clock_elapsed (domain->timer) - tstart);
    gts_range_update (&domain->timestep);
    gts_range_add_value (&domain->size, gfs_domain_size (domain, FTT_TRAVERSE_LEAFS, -1));
    gts_range_update (&domain->size);
  }
  gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_do, sim);  
  gts_container_foreach (GTS_CONTAINER (sim->events),
			 (GtsFunc) gts_object_destroy, NULL);
}

static void gfs_simulation_class_init (GfsSimulationClass * klass)
{
  GTS_OBJECT_CLASS (klass)->write =   simulation_write;
  GTS_OBJECT_CLASS (klass)->read =    simulation_read;
  GTS_OBJECT_CLASS (klass)->destroy = simulation_destroy;

  klass->run = simulation_run;
}

/* Derived variables */

static gdouble cell_x (FttCell * cell, FttCellFace * face)
{
  FttVector p;

  g_return_val_if_fail (cell != NULL || face != NULL, 0.);

  if (face)
    gfs_face_ca (face, &p);
  else
    gfs_cell_cm (cell, &p);
  return p.x;
}

static gdouble cell_y (FttCell * cell, FttCellFace * face)
{
  FttVector p;

  g_return_val_if_fail (cell != NULL || face != NULL, 0.);

  if (face)
    gfs_face_ca (face, &p);
  else
    gfs_cell_cm (cell, &p);
  return p.y;
}

static gdouble cell_z (FttCell * cell, FttCellFace * face)
{
  FttVector p;

  g_return_val_if_fail (cell != NULL || face != NULL, 0.);

  if (face)
    gfs_face_ca (face, &p);
  else
    gfs_cell_cm (cell, &p);
  return p.z;
}

static gdouble cell_ax (FttCell * cell)
{
  g_return_val_if_fail (cell != NULL, 0.);

  return GFS_IS_MIXED (cell) ? GFS_STATE (cell)->solid->ca.x : 0.;
}

static gdouble cell_ay (FttCell * cell)
{
  g_return_val_if_fail (cell != NULL, 0.);

  return GFS_IS_MIXED (cell) ? GFS_STATE (cell)->solid->ca.y : 0.;
}

static gdouble cell_az (FttCell * cell)
{
  g_return_val_if_fail (cell != NULL, 0.);

  return GFS_IS_MIXED (cell) ? GFS_STATE (cell)->solid->ca.z : 0.;
}

static gdouble cell_cx (FttCell * cell, FttCellFace * face)
{
  FttVector p;

  g_return_val_if_fail (cell != NULL || face != NULL, 0.);

  if (face)
    ftt_face_pos (face, &p);
  else
    ftt_cell_pos (cell, &p);
  return p.x;
}

static gdouble cell_cy (FttCell * cell, FttCellFace * face)
{
  FttVector p;

  g_return_val_if_fail (cell != NULL || face != NULL, 0.);

  if (face)
    ftt_face_pos (face, &p);
  else
    ftt_cell_pos (cell, &p);
  return p.y;
}

static gdouble cell_cz (FttCell * cell, FttCellFace * face)
{
  FttVector p;

  g_return_val_if_fail (cell != NULL || face != NULL, 0.);

  if (face)
    ftt_face_pos (face, &p);
  else
    ftt_cell_pos (cell, &p);
  return p.z;
}

static gdouble cell_t (FttCell * cell, FttCellFace * face, GfsSimulation * sim)
{
  return sim->time.t;
}

static gdouble cell_vorticity (FttCell * cell, FttCellFace * face, GfsDomain * domain)
{
  return gfs_vorticity (cell, gfs_domain_velocity (domain));
}

static gdouble cell_divergence (FttCell * cell, FttCellFace * face, GfsDomain * domain)
{
  return gfs_divergence (cell, gfs_domain_velocity (domain));
}

static gdouble cell_velocity_norm (FttCell * cell, FttCellFace * face, GfsDomain * domain)
{
  return gfs_vector_norm (cell, gfs_domain_velocity (domain));
}

static gdouble cell_velocity_norm2 (FttCell * cell, FttCellFace * face, GfsDomain * domain)
{
  return gfs_vector_norm2 (cell, gfs_domain_velocity (domain));
}

static gdouble cell_level (FttCell * cell)
{
  return ftt_cell_level (cell);
}

static gdouble cell_fraction (FttCell * cell)
{
  g_return_val_if_fail (cell != NULL, 0.);
  return GFS_IS_MIXED (cell) ? GFS_STATE (cell)->solid->a : 1.;
}

static gdouble cell_solid_area (FttCell * cell)
{
  FttVector n;
  gfs_solid_normal (cell, &n);
  return ftt_vector_norm (&n);
}

static gdouble cell_velocity_lambda2 (FttCell * cell, FttCellFace * face, GfsDomain * domain)
{
  return gfs_vector_lambda2 (cell, gfs_domain_velocity (domain));
}

static gdouble cell_streamline_curvature (FttCell * cell, FttCellFace * face, GfsDomain * domain)
{
  return gfs_streamline_curvature (cell, gfs_domain_velocity (domain));
}

static gdouble cell_2nd_principal_invariant (FttCell * cell, FttCellFace * face, GfsDomain * domain)
{
  return gfs_2nd_principal_invariant (cell, gfs_domain_velocity (domain))/ftt_cell_size (cell);
}

static void simulation_init (GfsSimulation * object)
{
  GfsDomain * domain = GFS_DOMAIN (object);
  static GfsDerivedVariableInfo derived_variable[] = {
    { "x", "x-coordinate of the center of mass of the cell", cell_x },
    { "y", "y-coordinate of the center of mass of the cell", cell_y },
    { "z", "z-coordinate of the center of mass of the cell", cell_z },
    { "ax", "x-coordinate of the center of area of the solid surface", cell_ax },
    { "ay", "y-coordinate of the center of area of the solid surface", cell_ay },
    { "az", "z-coordinate of the center of area of the solid surface", cell_az },
    { "cx", "x-coordinate of the center of the cell", cell_cx },
    { "cy", "y-coordinate of the center of the cell", cell_cy },
    { "cz", "z-coordinate of the center of the cell", cell_cz },
    { "t",  "Physical time", cell_t },
    { "Vorticity", "Norm of the vorticity vector of the velocity field", cell_vorticity },
    { "Divergence", "Divergence of the velocity field", cell_divergence },
    { "Velocity", "Norm of the velocity vector", cell_velocity_norm },
    { "Velocity2", "Squared norm of the velocity vector", cell_velocity_norm2 },
    { "Level", "Quad/octree level of the cell", cell_level },
    { "A", "Fluid fraction of the cell", cell_fraction },
    { "S", "Area of the solid contained in the cell", cell_solid_area },
    { "Lambda2", "Vortex-detection criterion of Jeong & Hussein", cell_velocity_lambda2 },
    { "Curvature",  "Curvature of the local streamline", cell_streamline_curvature },
    { "D2", "Second principal invariant of the deformation tensor", cell_2nd_principal_invariant },
    { NULL, NULL, NULL}
  };
  GfsDerivedVariableInfo * v = derived_variable;

  gfs_domain_add_variable (domain, "P", "Approximate projection pressure")->centered = TRUE;
  gfs_domain_add_variable (domain, "Pmac", "MAC projection pressure")->centered = TRUE;
  gfs_variable_set_vector (gfs_domain_add_variable (domain, "U", 
						    "x-component of the velocity"), FTT_X);
  gfs_variable_set_vector (gfs_domain_add_variable (domain, "V",
						    "y-component of the velocity"), FTT_Y);
#if (!FTT_2D)
  gfs_variable_set_vector (gfs_domain_add_variable (domain, "W",
						    "z-component of the velocity"), FTT_Z);
#endif /* FTT_3D */

  while (v->name) {
    g_assert (gfs_domain_add_derived_variable (domain, *v));
    v++;
  }

  gfs_time_init (&object->time);
  gfs_physical_params_init (&object->physical_params);

  gfs_advection_params_init (&object->advection_params);
  object->advection_params.flux = gfs_face_velocity_advection_flux;
  object->advection_params.average = TRUE;

  gfs_multilevel_params_init (&object->projection_params);
  gfs_multilevel_params_init (&object->approx_projection_params);

  object->surface = NULL;
  object->output_surface = TRUE;

  object->refines = GTS_SLIST_CONTAINER (gts_container_new
					 (GTS_CONTAINER_CLASS
					  (gts_slist_container_class ())));
  object->adapts = GTS_SLIST_CONTAINER (gts_container_new
					(GTS_CONTAINER_CLASS
					 (gts_slist_container_class ())));
  gfs_adapt_stats_init (&object->adapts_stats);
  object->events = GTS_SLIST_CONTAINER (gts_container_new
					(GTS_CONTAINER_CLASS
					 (gts_slist_container_class ())));
  object->modules = NULL;
  
  object->tnext = 0.;
}

GfsSimulationClass * gfs_simulation_class (void)
{
  static GfsSimulationClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_simulation_info = {
      "GfsSimulation",
      sizeof (GfsSimulation),
      sizeof (GfsSimulationClass),
      (GtsObjectClassInitFunc) gfs_simulation_class_init,
      (GtsObjectInitFunc) simulation_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_domain_class ()),
				  &gfs_simulation_info);
  }

  return klass;
}

GfsSimulation * gfs_simulation_new (GfsSimulationClass * klass)
{
  GfsSimulation * object;

  object = GFS_SIMULATION (gts_graph_new (GTS_GRAPH_CLASS (klass),
					 GTS_GNODE_CLASS (gfs_box_class ()),
					 GTS_GEDGE_CLASS (gfs_gedge_class ())));

  return object;
}

static void init_non_variable (GfsEvent * event, GfsSimulation * sim)
{
  if (!GFS_IS_VARIABLE (event))
    gfs_event_init (event, sim);
}

/**
 * gfs_simulation_init:
 * @sim: a #GfsSimulation.
 *
 * Initialises @sim: matches boundary conditions, applies boundary
 * conditions and initialises all variables, etc...
 */
void gfs_simulation_init (GfsSimulation * sim)
{
  g_return_if_fail (sim != NULL);

  gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) init_non_variable, sim);

  GfsDomain * domain = GFS_DOMAIN (sim);
  gfs_domain_match (domain);
  gfs_set_merged (domain);
  GSList * i = domain->variables;
  while (i) {
    gfs_event_init (GFS_EVENT (i->data), sim);
    gfs_domain_bc (domain, FTT_TRAVERSE_LEAFS, -1, i->data);
    i = i->next;
  }
  gfs_domain_cell_traverse (domain,
			    FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
			    (FttCellTraverseFunc) gfs_cell_coarse_init, domain);
}

static void refine_cell_corner (FttCell * cell, GfsDomain * domain)
{
  if (ftt_refine_corner (cell))
    ftt_cell_refine_single (cell, (FttCellInitFunc) gfs_cell_fine_init, domain);
}

static void check_face (FttCellFace * f, guint * nf)
{
  GfsSolidVector * s = GFS_STATE (f->cell)->solid;

  if (s && !f->neighbor && s->s[f->d] > 0. && s->s[f->d] < 1.)
    (*nf)++;
}

static void check_solid_fractions (GfsBox * box, guint * nf)
{
  FttDirection d;

  gfs_cell_check_solid_fractions (box->root);
  for (d = 0; d < FTT_NEIGHBORS; d++)
    ftt_face_traverse_boundary (box->root, d, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
				(FttFaceTraverseFunc) check_face, nf);
}

static void is_diffusion (GfsSource * s, gboolean * diffusion)
{
  *diffusion = (GFS_IS_SOURCE_DIFFUSION (s) != NULL);
}

/**
 * gfs_simulation_refine:
 * @sim: a #GfsSimulation.
 *
 * Calls the @refine() methods of the #GfsRefine of @sim. Matches the
 * boundaries by calling gfs_domain_match().
 */
void gfs_simulation_refine (GfsSimulation * sim)
{
  GSList * i;
  guint depth, nf = 0;
  gint l;
  GfsDomain * domain;

  g_return_if_fail (sim != NULL);

  domain = GFS_DOMAIN (sim);

  gfs_domain_timer_start (domain, "simulation_refine");
  i = sim->refines->items;
  while (i) {
    GfsRefine * refine = i->data;
    GSList * next = i->next;
    
    g_assert (GFS_REFINE_CLASS (GTS_OBJECT (refine)->klass)->refine);
    (* GFS_REFINE_CLASS (GTS_OBJECT (refine)->klass)->refine) (refine, sim);
    i = next;
  }

  depth = gfs_domain_depth (domain);
  for (l = depth - 2; l >= 0; l--)
    gfs_domain_cell_traverse (domain,
			     FTT_PRE_ORDER, FTT_TRAVERSE_LEVEL, l,
			     (FttCellTraverseFunc) refine_cell_corner, 
			      domain);
  gfs_domain_match (domain);
  gfs_domain_timer_stop (domain, "simulation_refine");

  if (sim->surface) {
    gfs_domain_timer_start (domain, "solid_fractions");
    sim->thin = gfs_domain_init_solid_fractions (domain, sim->surface, TRUE,
						 (FttCellCleanupFunc) gfs_cell_cleanup, NULL, 
						 NULL);
    gfs_domain_match (domain);
    gfs_domain_timer_stop (domain, "solid_fractions");
  }
  gts_container_foreach (GTS_CONTAINER (sim), (GtsFunc) check_solid_fractions, &nf);
  if (nf > 0) {
    GSList * i = domain->variables;
    gboolean diffusion = FALSE;
    
    while (i && !diffusion) {
      GfsVariable * v = i->data;

      if (v->sources)
	gts_container_foreach (v->sources, (GtsFunc) is_diffusion, &diffusion);
      i = i->next;
    }
    if (diffusion)
      g_warning ("the solid surface cuts %d boundary cells,\n"
		 "this may cause errors for diffusion terms\n", nf);
  }
}

/**
 * gfs_simulation_read:
 * @fp: a #GtsFile.
 *
 * Reads a simulation file from @fp.
 *
 * Returns: the #GfsSimulation or %NULL if an error occured, in which
 * case the @pos and @error fields of @fp are set.
 */
GfsSimulation * gfs_simulation_read (GtsFile * fp)
{
  GfsDomain * d;

  g_return_val_if_fail (fp != NULL, NULL);

  d = gfs_domain_read (fp);
  if (d != NULL && !GFS_IS_SIMULATION (d)) {
    gts_file_error (fp, "parent graph is not a GfsSimulation");
    gts_object_destroy (GTS_OBJECT (d));
    return NULL;
  }
  return GFS_SIMULATION (d);
}

/**
 * gfs_simulation_write:
 * @sim: a #GfsSimulation.
 * @max_depth: the maximum depth at which to stop writing cell tree
 * data (-1 means no limit).
 * @fp: a file pointer.
 *
 * Writes in @fp a text representation of @sim. If @max_depth is
 * smaller or equal to -2, no cell tree data is written.  
 */
void gfs_simulation_write (GfsSimulation * sim,
			   gint max_depth,		  
			   FILE * fp)
{
  gint depth;
  GfsDomain * domain;

  g_return_if_fail (sim != NULL);
  g_return_if_fail (fp != NULL);

  fprintf (fp, "# Gerris Flow Solver %dD version %s\n",
	   FTT_DIMENSION, GFS_VERSION);
  domain = GFS_DOMAIN (sim);
  depth = domain->max_depth_write;
  domain->max_depth_write = max_depth;
  gts_graph_write (GTS_GRAPH (sim), fp);
  domain->max_depth_write = depth;
}

static gdouble min_cfl (GfsSimulation * sim)
{
  gdouble cfl = (sim->advection_params.scheme == GFS_NONE ?
		 G_MAXDOUBLE :
		 sim->advection_params.cfl);
  GSList * i = GFS_DOMAIN (sim)->variables;
  
  while (i) {
    GfsVariable * v = i->data;

    if (GFS_IS_VARIABLE_TRACER (v) && 
	GFS_VARIABLE_TRACER (v)->advection.scheme != GFS_NONE &&
	GFS_VARIABLE_TRACER (v)->advection.cfl < cfl)
      cfl = GFS_VARIABLE_TRACER (v)->advection.cfl;
    i = i->next;
  }

  return cfl;
}

/**
 * gfs_simulation_set_timestep:
 * @sim: a #GfsSimulation.
 *
 * Sets the time step for the next iteration of @sim using the CFL
 * (computed using gfs_domain_cfl()), the stability conditions for
 * source terms and taking into account the timings of the various
 * #GfsEvent associated to @sim.
 *
 * More precisely, the time step is adjusted (if necessary) so that
 * the time of the closest event is exactly reached after the
 * iteration.  
 */
void gfs_simulation_set_timestep (GfsSimulation * sim)
{
  gdouble t, cfl;

  g_return_if_fail (sim != NULL);

  t = sim->time.t;
  if ((cfl = min_cfl (sim)) < G_MAXDOUBLE)
    sim->advection_params.dt = cfl*gfs_domain_cfl (GFS_DOMAIN (sim), FTT_TRAVERSE_LEAFS, -1);
  else
    sim->advection_params.dt = G_MAXINT;
  if (sim->advection_params.dt > sim->time.dtmax)
    sim->advection_params.dt = sim->time.dtmax;

  GSList *  i = GFS_DOMAIN (sim)->variables;
  while (i) {
    GfsVariable * v = i->data;
    if (v->sources) {
      GSList * j = GTS_SLIST_CONTAINER (v->sources)->items;
      while (j) {
	GfsSourceGeneric * s = j->data;
	if (GFS_SOURCE_GENERIC_CLASS (GTS_OBJECT (s)->klass)->stability) {
	  gdouble dt = (* GFS_SOURCE_GENERIC_CLASS (GTS_OBJECT (s)->klass)->stability) (s, sim);
	  if (dt < sim->advection_params.dt)
	    sim->advection_params.dt = dt;
	}
	j = j->next;
      }
    }
    i = i->next;
  }

  gdouble tnext = G_MAXINT;
  i = sim->events->items;
  while (i) {
    GfsEvent * event = i->data;
    if (t < event->t && event->t < tnext)
      tnext = event->t + 1e-9;
    i = i->next;
  }
  if (sim->time.end < tnext)
    tnext = sim->time.end;

  gdouble n = ceil ((tnext - t)/sim->advection_params.dt);
  if (n > 0. && n < G_MAXINT) {
    sim->advection_params.dt = (tnext - t)/n;
    if (n == 1.)
      sim->tnext = tnext;
    else
      sim->tnext = t + sim->advection_params.dt;
  }
  else
    sim->tnext = t + sim->advection_params.dt;

  if (sim->advection_params.dt < 1e-9)
    sim->advection_params.dt = 1e-9;
}

/**
 * gfs_time_write:
 * @t: the time structure.
 * @fp: a file pointer.
 *
 * Writes in @fp a text representation of the time structure @t.
 */
void gfs_time_write (GfsTime * t, FILE * fp)
{
  g_return_if_fail (t != NULL);
  g_return_if_fail (fp != NULL);

  fprintf (fp, "{ i = %u t = %g ", t->i, t->t);
  if (t->start != 0.)
    fprintf (fp, "start = %g ", t->start);
  if (t->istart != 0)
    fprintf (fp, "start = %u ", t->istart);
  if (t->end < G_MAXDOUBLE)
    fprintf (fp, "end = %g ", t->end);
  if (t->iend < G_MAXINT)
    fprintf (fp, "iend = %u ", t->iend);
  if (t->dtmax < G_MAXDOUBLE)
    fprintf (fp, "dtmax = %g ", t->dtmax);
  fputc ('}', fp);
}

/**
 * gfs_time_init:
 * @t: the #GfsTime.
 *
 * Initializes the time structure @t with default values.
 */
void gfs_time_init (GfsTime * t)
{
  g_return_if_fail (t != NULL);
  
  t->t = t->start = 0.;
  t->end = G_MAXDOUBLE;

  t->i = t->istart = 0;
  t->iend = G_MAXINT;

  t->dtmax = G_MAXDOUBLE;
}

/**
 * gfs_time_read:
 * @t: the #GfsTime.
 * @fp: the #GtsFile.
 *
 * Reads a time structure from @fp and puts it in @t.
 */
void gfs_time_read (GfsTime * t, GtsFile * fp)
{
  GtsFileVariable var[] = {
    {GTS_DOUBLE, "t",      TRUE},
    {GTS_DOUBLE, "start",  TRUE},
    {GTS_DOUBLE, "end",    TRUE},
    {GTS_UINT,   "i",      TRUE},
    {GTS_UINT,   "istart", TRUE},
    {GTS_UINT,   "iend",   TRUE},
    {GTS_DOUBLE, "dtmax",  TRUE},
    {GTS_NONE}
  };

  g_return_if_fail (t != NULL);
  g_return_if_fail (fp != NULL);

  var[0].data = &t->t;
  var[1].data = &t->start;
  var[2].data = &t->end;
  var[3].data = &t->i;
  var[4].data = &t->istart;
  var[5].data = &t->iend;
  var[6].data = &t->dtmax;

  gfs_time_init (t);
  gts_file_assign_variables (fp, var);

  if (t->t < t->start)
    t->t = t->start;
  if (t->i < t->istart)
    t->i = t->istart;
}

/**
 * gfs_physical_params_write:
 * @p: the physical parameters structure.
 * @fp: a file pointer.
 *
 * Writes in @fp a text representation of the physical parameters
 * structure @p.  
 */
void gfs_physical_params_write (GfsPhysicalParams * p, FILE * fp)
{
  g_return_if_fail (p != NULL);
  g_return_if_fail (fp != NULL);

  fprintf (fp, "{ g = %g", p->g);
  if (p->alpha) {
    fputs (" alpha =", fp);
    gfs_function_write (p->alpha, fp);
  }
  fputs (" }", fp);
}

/**
 * gfs_physical_params_init:
 * @p: the #GfsPhysicalParams.
 *
 * Initializes the physical parameters structure @p with default values.
 */
void gfs_physical_params_init (GfsPhysicalParams * p)
{
  g_return_if_fail (p != NULL);
  
  p->g = 1.;
  p->alpha = NULL;
}

/**
 * gfs_physical_params_read:
 * @p: the #GfsPhysicalParams.
 * @domain: a #GfsDomain.
 * @fp: the #GtsFile.
 *
 * Reads a physical parameters structure from @fp and puts it in @p.
 */
void gfs_physical_params_read (GfsPhysicalParams * p, GfsDomain * domain, GtsFile * fp)
{
  g_return_if_fail (p != NULL);
  g_return_if_fail (domain != NULL);
  g_return_if_fail (fp != NULL);

  if (fp->type != '{') {
    gts_file_error (fp, "expecting an opening brace");
    return;
  }
  fp->scope_max++;
  gts_file_next_token (fp);
  while (fp->type != GTS_ERROR && fp->type != '}') {
    if (fp->type == '\n') {
      gts_file_next_token (fp);
      continue;
    }
    if (fp->type != GTS_STRING) {
      gts_file_error (fp, "expecting a keyword");
      return;
    }
    else {
      gchar * id = g_strdup (fp->token->str);

      gts_file_next_token (fp);
      if (fp->type != '=') {
	gts_file_error (fp, "expecting `='");
	return;
      }
      gts_file_next_token (fp);

      if (!strcmp (id, "g")) {
	if (fp->type != GTS_INT && fp->type != GTS_FLOAT) {
	  g_free (id);
	  gts_file_error (fp, "expecting a number");
	  return;
	}
	p->g = atof (fp->token->str);
	gts_file_next_token (fp);
      }
      else if (!strcmp (id, "alpha")) {
	p->alpha = gfs_function_new (gfs_function_class (), 0.);
	gfs_function_read (p->alpha, domain, fp);
	if (fp->type == GTS_ERROR) {
	  g_free (id);
	  gts_object_destroy (GTS_OBJECT (p->alpha));
	  return;
	}
      }
      else {
	g_free (id);
	gts_file_error (fp, "unknown keyword `%s'", id);
	return;
      }
      g_free (id);
    }
  }
  if (fp->type != '}') {
    gts_file_error (fp, "expecting a closing brace");
    return;
  }
  fp->scope_max--;
  gts_file_next_token (fp);
}

static void error_handler (const gchar *log_domain,
			   GLogLevelFlags log_level,
			   const gchar *message,
			   gpointer user_data)
{
  GfsDomain * domain = user_data;
  g_slist_free (domain->variables_io);
  domain->variables_io = NULL;
  GSList * i = domain->variables;
  while (i) {
    if (GFS_VARIABLE1 (i->data)->name)
      domain->variables_io = g_slist_append (domain->variables_io, i->data);
    i = i->next;
  }
  FILE * fp = fopen ("error.gfs", "w");
  if (fp) {
    gfs_simulation_write (GFS_SIMULATION (domain), -1, fp);
    fclose (fp);
  }

  g_log_default_handler (log_domain, log_level, message, NULL);
}

/**
 * gfs_simulation_run:
 * @sim: a #GfsSimulation.
 *
 * Runs @sim.
 */
void gfs_simulation_run (GfsSimulation * sim)
{
  g_return_if_fail (sim != NULL);

  guint id = g_log_set_handler ("Gfs", G_LOG_LEVEL_ERROR | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
				error_handler, sim);
  gfs_clock_start (GFS_DOMAIN (sim)->timer);
  gts_range_init (&GFS_DOMAIN (sim)->mpi_wait);
  (* GFS_SIMULATION_CLASS (GTS_OBJECT (sim)->klass)->run) (sim);
  gfs_clock_stop (GFS_DOMAIN (sim)->timer);
  g_log_remove_handler ("Gfs", id);
}

/* GfsAdvection: Object */

static void advection_run (GfsSimulation * sim)
{
  GfsDomain * domain = GFS_DOMAIN (sim);

  gfs_simulation_refine (sim);
  gfs_simulation_init (sim);

  while (sim->time.t < sim->time.end &&
	 sim->time.i < sim->time.iend) {
    gdouble tstart = gfs_clock_elapsed (domain->timer);

    gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_do, sim);

    gfs_simulation_set_timestep (sim);

    gfs_domain_face_traverse (domain, FTT_XYZ,
			      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			      (FttFaceTraverseFunc) gfs_face_reset_normal_velocity, NULL);
    gfs_domain_face_traverse (domain, FTT_XYZ,
			      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			      (FttFaceTraverseFunc) gfs_face_interpolated_normal_velocity,
			      gfs_domain_velocity (domain));

    advance_tracers (domain, sim->advection_params.dt);

    gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_half_do, sim);

    gfs_domain_cell_traverse (domain,
			      FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
			      (FttCellTraverseFunc) gfs_cell_coarse_init, domain);
    gfs_simulation_adapt (sim);

    sim->time.t = sim->tnext;
    sim->time.i++;

    gts_range_add_value (&domain->timestep, gfs_clock_elapsed (domain->timer) - tstart);
    gts_range_update (&domain->timestep);
    gts_range_add_value (&domain->size, gfs_domain_size (domain, FTT_TRAVERSE_LEAFS, -1));
    gts_range_update (&domain->size);
  }
  gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_do, sim);  
  gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gts_object_destroy, NULL);
}

static void gfs_advection_class_init (GfsSimulationClass * klass)
{
  klass->run = advection_run;
}

GfsSimulationClass * gfs_advection_class (void)
{
  static GfsSimulationClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_advection_info = {
      "GfsAdvection",
      sizeof (GfsSimulation),
      sizeof (GfsSimulationClass),
      (GtsObjectClassInitFunc) gfs_advection_class_init,
      (GtsObjectInitFunc) NULL,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_simulation_class ()), &gfs_advection_info);
  }

  return klass;
}

/* GfsPoisson: Object */

static void rescale_div (FttCell * cell, gpointer * data)
{
  GfsVariable * divu = data[0];
  GfsVariable * div = data[1];
  GtsRange * vol = data[2];
  gdouble size = ftt_cell_size (cell);

  GFS_VARIABLE (cell, div->i) = GFS_VARIABLE (cell, divu->i)*size*size*(GFS_IS_MIXED (cell) ?
									GFS_STATE (cell)->solid->a : 1.);
  if (GFS_IS_MIXED (cell))
    gts_range_add_value (vol, size*size*GFS_STATE (cell)->solid->a);
  else
    gts_range_add_value (vol, size*size);
}

static void add_ddiv (FttCell * cell, gpointer * data)
{
  GfsVariable * div = data[1];
  gdouble * ddiv = data[2];
  gdouble size = ftt_cell_size (cell);

  if (GFS_IS_MIXED (cell))
    GFS_VARIABLE (cell, div->i) += size*size*GFS_STATE (cell)->solid->a*(*ddiv);
  else
    GFS_VARIABLE (cell, div->i) += size*size*(*ddiv);
}

static void correct_div (GfsDomain * domain, GfsVariable * divu, GfsVariable * div)
{
  gpointer data[3];
  GtsRange vol;
  gdouble ddiv;

  gts_range_init (&vol);
  data[0] = divu;
  data[1] = div;
  data[2] = &vol;
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) rescale_div, data);
  gts_range_update (&vol);

  ddiv = - gfs_domain_stats_variable (domain, div, FTT_TRAVERSE_LEAFS, -1).mean/vol.mean;
  data[2] = &ddiv;
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) add_ddiv, data);
}

static void copy_res (FttCell * cell, gpointer * data)
{
  GfsVariable * res = data[0], * res1 = data[1];
  GFS_VARIABLE (cell, res->i) = GFS_VARIABLE (cell, res1->i);
}

static void poisson_run (GfsSimulation * sim)
{
  GfsDomain * domain = GFS_DOMAIN (sim);
  GfsVariable * dia, * div, * res = NULL, * res1, * p;
  GfsMultilevelParams * par = &sim->approx_projection_params;
  GSList * i;

  gfs_simulation_refine (sim);
  gfs_simulation_init (sim);

  i = domain->variables;
  while (i) {
    if (GFS_IS_VARIABLE_RESIDUAL (i->data))
      res = i->data;
    i = i->next;
  }

  p = gfs_variable_from_name (domain->variables, "P");
  div = gfs_temporary_variable (domain);
  correct_div (domain, gfs_variable_from_name (domain->variables, "Div"), div);
  gfs_poisson_coefficients (domain, NULL);
  res1 = gfs_temporary_variable (domain);
  dia = gfs_temporary_variable (domain);
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_ALL, -1,
			    (FttCellTraverseFunc) gfs_cell_reset, dia);
  par->depth = gfs_domain_depth (domain);
  gfs_residual (domain, par->dimension, FTT_TRAVERSE_LEAFS, -1, p, div, dia, res1);
  par->residual_before = par->residual = 
    gfs_domain_norm_residual (domain, FTT_TRAVERSE_LEAFS, -1, 1., res1);
  par->niter = 0;
  while (sim->time.t < sim->time.end &&
	 sim->time.i < sim->time.iend &&
	 sim->time.i < par->nitermax &&
	 par->residual.infty > par->tolerance) {
    gdouble tstart = gfs_clock_elapsed (domain->timer);

    if (res) {
      gpointer data[2];
      data[0] = res;
      data[1] = res1;
      gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
				(FttCellTraverseFunc) copy_res, data);
    }

    gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_do, sim);

    gfs_domain_timer_start (domain, "poisson_cycle");
    gfs_poisson_cycle (domain, par, p, div, dia, res1);
    par->residual = gfs_domain_norm_residual (domain, FTT_TRAVERSE_LEAFS, -1, 1., res1);
    gfs_domain_timer_stop (domain, "poisson_cycle");

    gfs_domain_cell_traverse (domain,
			      FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
			      (FttCellTraverseFunc) gfs_cell_coarse_init, domain);
    gfs_simulation_adapt (sim);

    par->niter++;
    sim->time.t = sim->tnext;
    sim->time.i++;

    gts_range_add_value (&domain->timestep, gfs_clock_elapsed (domain->timer) - tstart);
    gts_range_update (&domain->timestep);
    gts_range_add_value (&domain->size, gfs_domain_size (domain, FTT_TRAVERSE_LEAFS, -1));
    gts_range_update (&domain->size);
  }
  gts_container_foreach (GTS_CONTAINER (sim->events), (GtsFunc) gfs_event_do, sim);  
  gts_container_foreach (GTS_CONTAINER (sim->events),
			 (GtsFunc) gts_object_destroy, NULL);
  gts_object_destroy (GTS_OBJECT (dia));
  gts_object_destroy (GTS_OBJECT (div));
  gts_object_destroy (GTS_OBJECT (res1));
}

static void poisson_class_init (GfsSimulationClass * klass)
{
  klass->run = poisson_run;
}

static void poisson_init (GfsDomain * domain)
{
  gfs_domain_add_variable (domain, "Div", "Right-hand-side of the Poisson equation");
}

GfsSimulationClass * gfs_poisson_class (void)
{
  static GfsSimulationClass * klass = NULL;

  if (klass == NULL) {
    GtsObjectClassInfo gfs_poisson_info = {
      "GfsPoisson",
      sizeof (GfsSimulation),
      sizeof (GfsSimulationClass),
      (GtsObjectClassInitFunc) poisson_class_init,
      (GtsObjectInitFunc) poisson_init,
      (GtsArgSetFunc) NULL,
      (GtsArgGetFunc) NULL
    };
    klass = gts_object_class_new (GTS_OBJECT_CLASS (gfs_simulation_class ()), &gfs_poisson_info);
  }

  return klass;
}
