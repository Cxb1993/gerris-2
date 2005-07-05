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

#include <math.h>
#include "poisson.h"
#include "solid.h"
#include "source.h"

#define FACE_GRADIENT(x, y, z, u) (gfs_face_weighted_gradient (x, y, z, u))
//#define FACE_GRADIENT(x, y, z, u) (gfs_face_gradient_flux_centered (x, y, z, u))

void gfs_face_gradient_flux_centered (const FttCellFace * face,
				      GfsGradient * g,
				      guint v,
				      gint max_level)
{
  guint level;

  g_return_if_fail (face != NULL);

  g->a = g->b = 0.;
  if (face->neighbor == NULL)
    return;

  level = ftt_cell_level (face->cell);
  if (ftt_cell_level (face->neighbor) < level) {
    g_assert_not_implemented ();
#if 0
    /* neighbor is at a shallower level */
    Gradient gcf;
    gdouble w = GFS_STATE (face->cell)->f[face->d].v;

    gcf = gradient_flux_fine_coarse (face, v, max_level);
    g->a = w*gcf.a;
    g->b = w*(gcf.b*GFS_VARIABLE (face->neighbor, v) + gcf.c);
#endif
  }
  else {
    if (level == max_level || FTT_CELL_IS_LEAF (face->neighbor)) {
      /* neighbor is at the same level */
      gdouble w = GFS_STATE (face->cell)->f[face->d].v;

      if (!GFS_IS_MIXED (face->cell) || !GFS_IS_MIXED (face->neighbor)) {
	g->a = w;
	g->b = w*GFS_VARIABLE (face->neighbor, v);
      }
      else {
	FttComponent c = face->d/2;
	FttComponent cp = FTT_ORTHOGONAL_COMPONENT (c);
	FttCell * n[4];
	gdouble ye = (1. - GFS_FACE_FRACTION (face))/2.;
	GfsSolidVector * s = GFS_STATE (face->cell)->solid;

	n[0] = face->cell; n[1] = face->neighbor;
	if ((s->s[2*cp] == 1. && s->s[2*cp + 1] < 1.) ||
	    (s->s[2*cp + 1] == 0. && 
	     s->s[2*cp] < 1. && s->s[2*cp] > 0.)) {
	  n[2] = ftt_cell_neighbor (n[0], 2*cp);
	  n[3] = ftt_cell_neighbor (n[1], 2*cp);
	}
	else if ((s->s[2*cp + 1] == 1. && s->s[2*cp] < 1.) ||
		 (s->s[2*cp] == 0. && 
		  s->s[2*cp + 1] < 1. && s->s[2*cp + 1] > 0.)) {
	  n[2] = ftt_cell_neighbor (n[0], 2*cp + 1);
	  n[3] = ftt_cell_neighbor (n[1], 2*cp + 1);
	}
	else {
	  g->a = w;
	  g->b = w*GFS_VARIABLE (n[1], v);
	  return;
	}

	//	g_assert (n[2] && n[3]);
	if (n[2] && n[3]) {
	  g->a = w*(1. - ye);
	  g->b = w*(1. - ye)*GFS_VARIABLE (n[1], v) + 
	    w*ye*(GFS_VARIABLE (n[3], v) - GFS_VARIABLE (n[2], v));
	}
	else {
	  g->a = w;
	  g->b = w*GFS_VARIABLE (n[1], v);
	}
      }
    }
    else {
    g_assert_not_implemented ();
#if 0
      /* neighbor is at a deeper level */
      FttCellChildren children;
      FttCellFace f;
      guint i, n;
      
      f.d = FTT_OPPOSITE_DIRECTION (face->d);
      n = ftt_cell_children_direction (face->neighbor, f.d, &children);
      f.neighbor = face->cell;
      for (i = 0; i < n; i++) {
	Gradient gcf;
	gdouble w;

	f.cell = children.c[i];
	w = GFS_STATE (f.cell)->f[f.d].v;
	
	/* check for mixed cell refinement violation (topology.fig) */
	g_assert (f.cell);
	
	gcf = gradient_fine_coarse (&f, v, max_level);
	g->a += w*gcf.b;
	g->b += w*(gcf.a*GFS_VARIABLE (f.cell, v) - gcf.c);
      }
#endif
#if (!FTT_2D && !FTT_2D3)
      g->a /= 2.;
      g->b /= 2.;
#endif /* not 2D and not 2D3 */
    }
  }
}

typedef struct {
  guint u, rhs, dia, res;
  gint maxlevel;
} RelaxParams;

static void relax (FttCell * cell, RelaxParams * p)
{
  GfsGradient g;
  FttCellNeighbors neighbor;
  FttCellFace f;
  GfsGradient ng;

  g.a = GFS_VARIABLE (cell, p->dia);
  g.b = 0.;
  f.cell = cell;
  ftt_cell_neighbors (cell, &neighbor);
  for (f.d = 0; f.d < FTT_NEIGHBORS; f.d++) {
    f.neighbor = neighbor.c[f.d];
    if (f.neighbor) {
      FACE_GRADIENT (&f, &ng, p->u, p->maxlevel);
      g.a += ng.a;
      g.b += ng.b;
    }
  }
  if (g.a > 0.)
    GFS_VARIABLE (cell, p->u) = (g.b - GFS_VARIABLE (cell, p->rhs))/g.a;
  else
    GFS_VARIABLE (cell, p->u) = 0.;
}

static void relax2D (FttCell * cell, RelaxParams * p)
{
  GfsGradient g;
  FttCellNeighbors neighbor;
  FttCellFace f;
  GfsGradient ng;

  g.a = GFS_VARIABLE (cell, p->dia);
  g.b = 0.;
  f.cell = cell;
  ftt_cell_neighbors (cell, &neighbor);
  for (f.d = 0; f.d < FTT_NEIGHBORS_2D; f.d++) {
    f.neighbor = neighbor.c[f.d];
    if (f.neighbor) {
      FACE_GRADIENT (&f, &ng, p->u, p->maxlevel);
      g.a += ng.a;
      g.b += ng.b;
    }
  }
  if (g.a > 0.)
    GFS_VARIABLE (cell, p->u) = (g.b - GFS_VARIABLE (cell, p->rhs))/g.a;
  else
    GFS_VARIABLE (cell, p->u) = 0.;
}

/**
 * gfs_relax:
 * @domain: the domain to relax.
 * @d: number of dimensions (2 or 3).
 * @max_depth: the maximum depth of the domain to relax.
 * @u: the variable to use as left-hand side.
 * @rhs: the variable to use as right-hand side.
 * @dia: the diagonal weight.
 *
 * Apply one pass of a Jacobi relaxation to all the leaf cells of
 * @domain with a level inferior or equal to @max_depth and to all the
 * cells at level @max_depth. The relaxation should converge (if the
 * right-hand-side @rhs verifies the solvability conditions) toward
 * the solution of a Poisson equation for @u at the maximum depth.
 */
void gfs_relax (GfsDomain * domain,
		guint d,
		gint max_depth,
		GfsVariable * u,
		GfsVariable * rhs,
		GfsVariable * dia)
{
  RelaxParams p;

  g_return_if_fail (domain != NULL);
  g_return_if_fail (d > 1 && d <= 3);
  g_return_if_fail (u != NULL);
  g_return_if_fail (rhs != NULL);
  g_return_if_fail (dia != NULL);

  p.u = u->i;
  p.rhs = rhs->i;
  p.dia = dia->i;
  p.maxlevel = max_depth;
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, 
			    FTT_TRAVERSE_LEVEL | FTT_TRAVERSE_LEAFS,
			    max_depth,
			    (FttCellTraverseFunc) (d == 2 ? relax2D : relax), &p);
}

static void residual_set (FttCell * cell, RelaxParams * p)
{
  GfsGradient g;
  FttCellNeighbors neighbor;
  FttCellFace f;
  GfsGradient ng;

  g.a = GFS_VARIABLE (cell, p->dia);
  g.b = 0.;
  f.cell = cell;
  ftt_cell_neighbors (cell, &neighbor);
  for (f.d = 0; f.d < FTT_NEIGHBORS; f.d++) {
    f.neighbor = neighbor.c[f.d];
    if (f.neighbor) {
      FACE_GRADIENT (&f, &ng, p->u, -1);
      g.a += ng.a;
      g.b += ng.b;
    }
  }
  GFS_VARIABLE (cell, p->res) = GFS_VARIABLE (cell, p->rhs) - 
    (g.b - GFS_VARIABLE (cell, p->u)*g.a);
}

static void residual_set2D (FttCell * cell, RelaxParams * p)
{
  GfsGradient g;
  FttCellNeighbors neighbor;
  FttCellFace f;
  GfsGradient ng;

  g.a = GFS_VARIABLE (cell, p->dia);
  g.b = 0.;
  f.cell = cell;
  ftt_cell_neighbors (cell, &neighbor);
  for (f.d = 0; f.d < FTT_NEIGHBORS_2D; f.d++) {
    f.neighbor = neighbor.c[f.d];
    if (f.neighbor) {
      FACE_GRADIENT (&f, &ng, p->u, -1);
      g.a += ng.a;
      g.b += ng.b;
    }
  }
  GFS_VARIABLE (cell, p->res) = GFS_VARIABLE (cell, p->rhs) - 
    (g.b - GFS_VARIABLE (cell, p->u)*g.a);
}

/**
 * gfs_residual:
 * @domain: a domain.
 * @d: number of dimensions (2 or 3).
 * @flags: which types of cells are to be visited.
 * @max_depth: maximum depth of the traversal.
 * @u: the variable to use as left-hand side.
 * @rhs: the variable to use as right-hand side.
 * @dia: the diagonal weight.
 * @res: the variable to use to store the residual.
 *
 * For each cell of @domain, computes the sum of the residual over
 * the volume of the cell for a Poisson equation with @u as
 * left-hand-side and @rhs as right-hand-side. Stores the result in
 * @res.  
 */
void gfs_residual (GfsDomain * domain,
		   guint d,
		   FttTraverseFlags flags,
		   gint max_depth,
		   GfsVariable * u, GfsVariable * rhs, GfsVariable * dia,
		   GfsVariable * res)
{
  RelaxParams p;

  g_return_if_fail (domain != NULL);
  g_return_if_fail (d > 1 && d <= 3);
  g_return_if_fail (u != NULL);
  g_return_if_fail (rhs != NULL);
  g_return_if_fail (dia != NULL);
  g_return_if_fail (res != NULL);

  p.u = u->i;
  p.rhs = rhs->i;
  p.dia = dia->i;
  p.res = res->i;
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, flags, max_depth,
			    (FttCellTraverseFunc) (d == 2 ? residual_set2D : residual_set), &p);
}

static void reset_coeff (FttCell * cell)
{
  FttDirection d;
  GfsFaceStateVector * f = GFS_STATE (cell)->f;
  
  for (d = 0; d < FTT_NEIGHBORS; d++)
    f[d].v = 0.;
}

static void poisson_coeff (FttCellFace * face, gdouble * lambda2)
{
  GfsStateVector * s = GFS_STATE (face->cell);
  gdouble v = lambda2[face->d/2];

  if (GFS_IS_MIXED (face->cell))
    v *= s->solid->s[face->d];
  s->f[face->d].v = v;

  switch (ftt_face_type (face)) {
  case FTT_FINE_FINE:
    GFS_STATE (face->neighbor)->f[FTT_OPPOSITE_DIRECTION (face->d)].v = v;
    break;
  case FTT_FINE_COARSE:
    GFS_STATE (face->neighbor)->f[FTT_OPPOSITE_DIRECTION (face->d)].v += 
      v/FTT_CELLS_DIRECTION (face->d);
    break;
  default:
    g_assert_not_reached ();
  }
}

static void poisson_density_coeff (FttCellFace * face,
				   gpointer * data)
{
  GfsVariable * c = data[0];
  gdouble * rho = data[1];
  gdouble * lambda2 = data[2];
  gdouble v = lambda2[face->d/2], cval;
  GfsStateVector * s = GFS_STATE (face->cell);

  if (GFS_IS_MIXED (face->cell))
    v *= s->solid->s[face->d];
  cval = gfs_face_interpolated_value (face, c->i);
  v /= 1. + (cval > 1. ? 1. : cval < 0. ? 0. : cval)*(*rho - 1.);
  s->f[face->d].v = v;

  switch (ftt_face_type (face)) {
  case FTT_FINE_FINE:
    GFS_STATE (face->neighbor)->f[FTT_OPPOSITE_DIRECTION (face->d)].v = v;
    break;
  case FTT_FINE_COARSE:
    GFS_STATE (face->neighbor)->f[FTT_OPPOSITE_DIRECTION (face->d)].v +=
      v/FTT_CELLS_DIRECTION (face->d);
    break;
  default:
    g_assert_not_reached ();
  }
}

static void face_coeff_from_below (FttCell * cell)
{
  FttDirection d;
  GfsFaceStateVector * f = GFS_STATE (cell)->f;

  for (d = 0; d < FTT_NEIGHBORS; d++) {
    FttCellChildren child;
    guint i, n;

    f[d].v = 0.;
    n = ftt_cell_children_direction (cell, d, &child);
    for (i = 0; i < n; i++)
      if (child.c[i])
	f[d].v += GFS_STATE (child.c[i])->f[d].v;
    f[d].v /= n;
  }
}

/**
 * gfs_poisson_coefficients:
 * @domain: a #GfsDomain.
 * @c: the volume fraction.
 * @rho: the relative density.
 *
 * Initializes the face coefficients for the Poisson equation.
 */
void gfs_poisson_coefficients (GfsDomain * domain,
			       GfsVariable * c,
			       gdouble rho)
{
  gdouble lambda2[FTT_DIMENSION];
  FttComponent i;

  g_return_if_fail (domain != NULL);

  for (i = 0; i < FTT_DIMENSION; i++) {
    gdouble lambda = (&domain->lambda.x)[i];

    lambda2[i] = lambda*lambda;
  }
  gfs_domain_cell_traverse (domain,
			    FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) reset_coeff, NULL);
  if (c == NULL || rho == 1.)
    gfs_domain_face_traverse (domain, FTT_XYZ, 
			      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			      (FttFaceTraverseFunc) poisson_coeff, lambda2);
  else {
    gpointer data[3];

    data[0] = c;
    data[1] = &rho;
    data[2] = lambda2;
    gfs_domain_face_traverse (domain, FTT_XYZ, 
			      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			      (FttFaceTraverseFunc) poisson_density_coeff, 
			      data);
  }
  gfs_domain_cell_traverse (domain,
			    FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
			    (FttCellTraverseFunc) face_coeff_from_below, NULL);
}

static void correct (FttCell * cell, gpointer * data)
{
  GfsVariable * u = data[0];
  GfsVariable * dp = data[1];
  GFS_VARIABLE (cell, u->i) += GFS_VARIABLE (cell, dp->i);
}

/**
 * gfs_poisson_cycle:
 * @domain: the domain on which to solve the Poisson equation.
 * @d: number of dimensions (2 or 3).
 * @levelmin: the top level of the multigrid hierarchy.
 * @depth: the total depth of the domain.
 * @nrelax: the number of relaxations to apply at each level.
 * @u: the variable to use as left-hand side.
 * @rhs: the variable to use as right-hand side.
 * @dia: the diagonal weight.
 * @res: the residual.
 *
 * Apply one multigrid iteration to the Poisson equation defined by @u
 * and @rhs.
 *
 * The initial value of @res on the leaves of @root must be set to
 * the residual of the Poisson equation (using gfs_residual()).
 *
 * The face coefficients must be set using gfs_poisson_coefficients().
 *
 * The values of @u on the leaf cells are updated as well as the values
 * of @res (i.e. the cell tree is ready for another iteration).
 */
void gfs_poisson_cycle (GfsDomain * domain,
			guint d,
			guint levelmin,
			guint depth,
			guint nrelax,
			GfsVariable * u,
			GfsVariable * rhs,
			GfsVariable * dia,
			GfsVariable * res)
{
  guint n, l;
  GfsVariable * dp;
  gpointer data[2];
  
  g_return_if_fail (domain != NULL);
  g_return_if_fail (d > 1 && d <= 3);
  g_return_if_fail (u != NULL);
  g_return_if_fail (rhs != NULL);
  g_return_if_fail (dia != NULL);
  g_return_if_fail (res != NULL);

  dp = gfs_temporary_variable (domain);

  /* compute residual on non-leafs cells */
  gfs_domain_cell_traverse (domain, 
			    FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
			    (FttCellTraverseFunc) gfs_get_from_below_extensive, res);

  /* relax top level */
  gfs_domain_cell_traverse (domain, 
			    FTT_PRE_ORDER, FTT_TRAVERSE_LEVEL, levelmin,
			    (FttCellTraverseFunc) gfs_cell_reset, dp);
  for (n = 0; n < 10*nrelax; n++) {
    gfs_domain_homogeneous_bc (domain,
			       FTT_TRAVERSE_LEVEL | FTT_TRAVERSE_LEAFS,
			       levelmin, dp, u);
    gfs_relax (domain, d, levelmin, dp, res, dia);
  }

  /* relax from top to bottom */
  for (l = levelmin + 1; l <= depth; l++) {
    /* get initial guess from coarser grid */ 
    gfs_domain_cell_traverse (domain, 
			      FTT_PRE_ORDER, FTT_TRAVERSE_LEVEL, l,
			      (FttCellTraverseFunc) gfs_get_from_above, dp);
    for (n = 0; n < nrelax; n++) {
      gfs_domain_homogeneous_bc (domain, 
				 FTT_TRAVERSE_LEVEL | FTT_TRAVERSE_LEAFS,
				 l, dp, u);
      gfs_relax (domain, d, l, dp, res, dia);
    }
  }
  /* correct on leaf cells */
  data[0] = u;
  data[1] = dp;
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) correct, data);
  gfs_domain_bc (domain, FTT_TRAVERSE_LEAFS, -1, u);
  /* compute new residual on leaf cells */
  gfs_residual (domain, d, FTT_TRAVERSE_LEAFS, -1, u, rhs, dia, res);

  gts_object_destroy (GTS_OBJECT (dp));
}

static void diffusion_coef (FttCellFace * face, gpointer * data)
{
  GfsSourceDiffusion * d = data[0];
  gdouble * lambda2 = data[1];
  gdouble * dt = data[2];
  GfsStateVector * s = GFS_STATE (face->cell);
  gdouble v = lambda2[face->d/2]*(*dt)*gfs_source_diffusion_face (d, face);

  if (GFS_IS_MIXED (face->cell))
    v *= s->solid->s[face->d];
  s->f[face->d].v = v;

  switch (ftt_face_type (face)) {
  case FTT_FINE_FINE:
    GFS_STATE (face->neighbor)->f[FTT_OPPOSITE_DIRECTION (face->d)].v = v;
    break;
  case FTT_FINE_COARSE:
    GFS_STATE (face->neighbor)->f[FTT_OPPOSITE_DIRECTION (face->d)].v +=
      v/FTT_CELLS_DIRECTION (face->d);
    break;
  default:
    g_assert_not_reached ();
  }
}

static void diffusion_mixed_coef (FttCell * cell, gpointer * data)
{
  reset_coeff (cell);
  if (GFS_IS_MIXED (cell)) {
    GfsSourceDiffusion * d = data[0];
    gdouble * dt = data[2];

    GFS_STATE (cell)->solid->v = *dt*gfs_source_diffusion_cell (d, cell);
  }
  GFS_VARIABLE (cell, GFS_VARIABLE1 (data[3])->i) = 1.;
}

/**
 * gfs_diffusion_coefficients:
 * @domain: a #GfsDomain.
 * @d: a #GfsSourceDiffusion.
 * @dt: the time-step.
 * @dia: where to store the diagonal weight.
 *
 * Initializes the face coefficients for the diffusion equation.
 */
void gfs_diffusion_coefficients (GfsDomain * domain,
				 GfsSourceDiffusion * d,
				 gdouble dt,
				 GfsVariable * dia)
{
  gdouble lambda2[FTT_DIMENSION];
  gpointer data[4];
  FttComponent i;

  g_return_if_fail (domain != NULL);
  g_return_if_fail (d != NULL);
  g_return_if_fail (dia != NULL);

  for (i = 0; i < FTT_DIMENSION; i++) {
    gdouble lambda = (&domain->lambda.x)[i];

    lambda2[i] = lambda*lambda;
  }
  data[0] = d;
  data[1] = lambda2;
  data[2] = &dt;
  data[3] = dia;
  gfs_domain_cell_traverse (domain,
			    FTT_PRE_ORDER, FTT_TRAVERSE_ALL, -1,
			    (FttCellTraverseFunc) diffusion_mixed_coef, data);
  gfs_domain_face_traverse (domain, FTT_XYZ, 
			    FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttFaceTraverseFunc) diffusion_coef, data);
  gfs_domain_cell_traverse (domain,
			    FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
			    (FttCellTraverseFunc) face_coeff_from_below, 
			    NULL);
}

static void density (FttCell * cell, gpointer * data)
{
  GfsVariable * v = data[0];
  gdouble c = GFS_VARIABLE (cell, v->i);
  gdouble * rho = data[1];

  GFS_VARIABLE (cell, GFS_VARIABLE1 (data[2])->i) = 1. + *rho*(c > 1. ? 1. : c < 0. ? 0. : c);
}

/**
 * gfs_viscosity_coefficients:
 * @domain: a #GfsDomain.
 * @d: a #GfsSourceDiffusion.
 * @dt: the time-step.
 * @c: the volume fraction (at t+dt/2).
 * @rho: the relative density.
 * @dia: where to store the diagonal weight.
 *
 * Initializes the face coefficients for the diffusion equation for
 * the velocity.
 */
void gfs_viscosity_coefficients (GfsDomain * domain,
				 GfsSourceDiffusion * d,
				 gdouble dt,
				 GfsVariable * c,
				 gdouble rho,
				 GfsVariable * dia)
{
  g_return_if_fail (domain != NULL);
  g_return_if_fail (d != NULL);
  g_return_if_fail (c != NULL);
  g_return_if_fail (dia != NULL);

  gfs_diffusion_coefficients (domain, d, dt, dia);
  if (rho != 1.) {
    gpointer data[3];

    rho -= 1.;
    data[0] = c;
    data[1] = &rho;
    data[2] = dia;
    gfs_domain_cell_traverse (domain,
			      FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			      (FttCellTraverseFunc) density, data);
    gfs_domain_cell_traverse (domain,
			      FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
			      (FttCellTraverseFunc) gfs_get_from_below_intensive, dia);
  }
}

static void diffusion_rhs (FttCell * cell, RelaxParams * p)
{
  gdouble a, f, h, val;
  FttCellNeighbors neighbor;
  FttCellFace face;
  
  if (GFS_IS_MIXED (cell)) {
    a = GFS_STATE (cell)->solid->a*GFS_VARIABLE (cell, p->dia);
    if (((cell)->flags & GFS_FLAG_DIRICHLET) != 0)
      f = gfs_cell_dirichlet_gradient_flux (cell, p->u, -1, GFS_STATE (cell)->solid->fv);
    else
      f = GFS_STATE (cell)->solid->fv;
  }
  else {
    a = GFS_VARIABLE (cell, p->dia);
    f = 0.; /* Neumann condition by default */
  }
  h = ftt_cell_size (cell);
  val = GFS_VARIABLE (cell, p->u);
  face.cell = cell;
  ftt_cell_neighbors (cell, &neighbor);
  for (face.d = 0; face.d < FTT_NEIGHBORS; face.d++) {
    GfsGradient g;

    face.neighbor = neighbor.c[face.d];
    gfs_face_gradient_flux (&face, &g, p->u, -1);
    f += g.b - g.a*val;
  }
  GFS_VARIABLE (cell, p->rhs) += val + f/(2.*h*h*a);
}

/**
 * gfs_diffusion_rhs:
 * @domain: a #GfsDomain.
 * @v: a #GfsVariable.
 * @rhs: a #GfsVariable.
 * @dia: the diagonal weight.
 *
 * Adds to the @rhs variable of @cell the right-hand side of the
 * diffusion equation for variable @v.
 *
 * The diffusion coefficients must have been already set using
 * gfs_diffusion_coefficients().
 */
void gfs_diffusion_rhs (GfsDomain * domain, GfsVariable * v, GfsVariable * rhs, GfsVariable * dia)
{
  RelaxParams p;

  g_return_if_fail (domain != NULL);
  g_return_if_fail (v != NULL);
  g_return_if_fail (rhs != NULL);
  g_return_if_fail (dia != NULL);

  p.u = v->i;
  p.rhs = rhs->i;
  p.dia = dia->i;
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) diffusion_rhs, &p);
}

static void diffusion_relax (FttCell * cell, RelaxParams * p)
{
  gdouble a;
  GfsGradient g = { 0., 0. };
  gdouble h = ftt_cell_size (cell);
  FttCellNeighbors neighbor;
  FttCellFace face;

  if (GFS_IS_MIXED (cell)) {
    a = GFS_STATE (cell)->solid->a*GFS_VARIABLE (cell, p->dia);
    if (((cell)->flags & GFS_FLAG_DIRICHLET) != 0)
      g.b = gfs_cell_dirichlet_gradient_flux (cell, p->u, p->maxlevel, 0.);
  }
  else
    a = GFS_VARIABLE (cell, p->dia);

  face.cell = cell;
  ftt_cell_neighbors (cell, &neighbor);
  for (face.d = 0; face.d < FTT_NEIGHBORS; face.d++) {
    GfsGradient ng;

    face.neighbor = neighbor.c[face.d];
    gfs_face_gradient_flux (&face, &ng, p->u, p->maxlevel);
    g.a += ng.a;
    g.b += ng.b;
  }
  a *= 2.*h*h;
  g_assert (a > 0.);
  g.a = 1. + g.a/a;
  g.b = GFS_VARIABLE (cell, p->res) + g.b/a;
  g_assert (g.a > 0.);
  GFS_VARIABLE (cell, p->u) = g.b/g.a;
}

static void diffusion_residual (FttCell * cell, RelaxParams * p)
{
  gdouble a;
  GfsGradient g = { 0., 0. };
  gdouble h;
  FttCellNeighbors neighbor;
  FttCellFace face;

  h = ftt_cell_size (cell);
  if (GFS_IS_MIXED (cell)) {
    a = GFS_STATE (cell)->solid->a*GFS_VARIABLE (cell, p->dia);
    if (((cell)->flags & GFS_FLAG_DIRICHLET) != 0)
      g.b = gfs_cell_dirichlet_gradient_flux (cell, p->u, -1, GFS_STATE (cell)->solid->fv);
    else
      g.b = GFS_STATE (cell)->solid->fv;
  }
  else
    a = GFS_VARIABLE (cell, p->dia);

  face.cell = cell;
  ftt_cell_neighbors (cell, &neighbor);
  for (face.d = 0; face.d < FTT_NEIGHBORS; face.d++) {
    GfsGradient ng;

    face.neighbor = neighbor.c[face.d];
    gfs_face_gradient_flux (&face, &ng, p->u, -1);
    g.a += ng.a;
    g.b += ng.b;
  }
  a *= 2.*h*h;
  g_assert (a > 0.);
  g.a = 1. + g.a/a;
  g.b = GFS_VARIABLE (cell, p->rhs) + g.b/a;
  GFS_VARIABLE (cell, p->res) = g.b - g.a*GFS_VARIABLE (cell, p->u);
}

/**
 * gfs_diffusion_residual:
 * @domain: a #GfsDomain.
 * @u: the variable to use as left-hand side.
 * @rhs: the right-hand side.
 * @dia: the diagonal weight.
 * @res: the residual.
 *
 * Sets the @res variable of each leaf cell of @domain to the residual
 * of the diffusion equation for @v.
 *
 * The diffusion coefficients must have been set using
 * gfs_diffusion_coefficients() and the right-hand side using
 * gfs_diffusion_rhs().
 */
void gfs_diffusion_residual (GfsDomain * domain,
			     GfsVariable * u,
			     GfsVariable * rhs,
			     GfsVariable * dia,
			     GfsVariable * res)
{
  RelaxParams p;

  g_return_if_fail (domain != NULL);
  g_return_if_fail (u != NULL);
  g_return_if_fail (rhs != NULL);
  g_return_if_fail (dia != NULL);
  g_return_if_fail (res != NULL);

  p.u = u->i;
  p.rhs = rhs->i;
  p.dia = dia->i;
  p.res = res->i;
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) diffusion_residual, &p);
}

/**
 * gfs_diffusion_cycle:
 * @domain: the domain on which to solve the diffusion equation.
 * @levelmin: the top level of the multigrid hierarchy.
 * @depth: the total depth of the domain.
 * @nrelax: the number of relaxations to apply at each level.
 * @u: the variable to use as left-hand side.
 * @rhs: the right-hand side.
 * @dia: the diagonal weight.
 * @res: the residual.
 *
 * Apply one multigrid iteration to the diffusion equation for @u.
 *
 * The initial value of @res on the leaves of @root must be set to
 * the residual of the diffusion equation using gfs_diffusion_residual().
 *
 * The diffusion coefficients must be set using gfs_diffusion_coefficients().
 *
 * The values of @u on the leaf cells are updated as well as the values
 * of @res (i.e. the cell tree is ready for another iteration).
 */
void gfs_diffusion_cycle (GfsDomain * domain,
			  guint levelmin,
			  guint depth,
			  guint nrelax,
			  GfsVariable * u,
			  GfsVariable * rhs,
			  GfsVariable * dia,
			  GfsVariable * res)
{
  guint n;
  GfsVariable * dp;
  RelaxParams p;
  gpointer data[2];

  g_return_if_fail (domain != NULL);
  g_return_if_fail (u != NULL);
  g_return_if_fail (rhs != NULL);
  g_return_if_fail (dia != NULL);
  g_return_if_fail (res != NULL);

  dp = gfs_temporary_variable (domain);

  /* compute residual on non-leafs cells */
  gfs_domain_cell_traverse (domain, 
			    FTT_POST_ORDER, FTT_TRAVERSE_NON_LEAFS, -1,
			    (FttCellTraverseFunc) gfs_get_from_below_intensive, res);

  /* relax top level */
  gfs_domain_cell_traverse (domain, 
			    FTT_PRE_ORDER, FTT_TRAVERSE_LEVEL, levelmin,
			    (FttCellTraverseFunc) gfs_cell_reset, dp);
  p.maxlevel = levelmin;
  p.u = dp->i;
  p.res = res->i;
  p.dia = dia->i;
  for (n = 0; n < 10*nrelax; n++) {
    gfs_domain_homogeneous_bc (domain, 
			       FTT_TRAVERSE_LEVEL | FTT_TRAVERSE_LEAFS,
			       levelmin, dp, u);
    gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, 
			      FTT_TRAVERSE_LEVEL | FTT_TRAVERSE_LEAFS, 
			      levelmin,
			      (FttCellTraverseFunc) diffusion_relax, &p);
  }

  /* relax from top to bottom */
  for (p.maxlevel = levelmin + 1; p.maxlevel <= depth; p.maxlevel++) {
    /* get initial guess from coarser grid */ 
    gfs_domain_cell_traverse (domain, 
			      FTT_PRE_ORDER, FTT_TRAVERSE_LEVEL, p.maxlevel,
			      (FttCellTraverseFunc) gfs_get_from_above, dp);
    for (n = 0; n < nrelax; n++) {
      gfs_domain_homogeneous_bc (domain, 
				 FTT_TRAVERSE_LEVEL | FTT_TRAVERSE_LEAFS,
				 p.maxlevel, dp, u);
      gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, 
				FTT_TRAVERSE_LEVEL | FTT_TRAVERSE_LEAFS, p.maxlevel,
				(FttCellTraverseFunc) diffusion_relax, &p);
    }
  }
  /* correct on leaf cells */
  data[0] = u;
  data[1] = dp;
  gfs_domain_cell_traverse (domain, FTT_PRE_ORDER, FTT_TRAVERSE_LEAFS, -1,
			    (FttCellTraverseFunc) correct, data);
  gfs_domain_bc (domain, FTT_TRAVERSE_LEAFS, -1, u);
  /* compute new residual on leaf cells */
  gfs_diffusion_residual (domain, u, rhs, dia, res);

  gts_object_destroy (GTS_OBJECT (dp));
}

