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

#ifndef __FLUID_H__
#define __FLUID_H__

#include <gts.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "ftt.h"

typedef struct _GfsVariable               GfsVariable;
typedef struct _GfsDerivedVariable        GfsDerivedVariable;

typedef struct _GfsStateVector     GfsStateVector;
typedef struct _GfsSolidVector     GfsSolidVector;
typedef struct _GfsFaceStateVector GfsFaceStateVector;

struct _GfsFaceStateVector {
  gdouble un;
  gdouble v;
};

struct _GfsStateVector {
  /* temporary face variables */
  GfsFaceStateVector f[FTT_NEIGHBORS];

  /* solid boundaries */
  GfsSolidVector * solid;

  gdouble place_holder;
};

struct _GfsSolidVector {
  gdouble s[FTT_NEIGHBORS];
  gdouble a, v, fv;
  FttCell * merged;
  FttVector cm, ca;
};

typedef enum {
  GFS_FLAG_USED =              1 <<  FTT_FLAG_USER,
  GFS_FLAG_BOUNDARY          = 1 << (FTT_FLAG_USER + 1),
  GFS_FLAG_DIRICHLET         = 1 << (FTT_FLAG_USER + 2),
  GFS_FLAG_GRADIENT_BOUNDARY = 1 << (FTT_FLAG_USER + 3),
  GFS_FLAG_USER =                    FTT_FLAG_USER + 4 /* user flags start here */
} GfsFlags;

#define GFS_STATE(cell)               ((GfsStateVector *) (cell)->data)
#define GFS_VARIABLE(cell, index)     ((&GFS_STATE (cell)->place_holder)[index])

#define GFS_FACE_NORMAL_VELOCITY(fa)\
  (GFS_STATE ((fa)->cell)->f[(fa)->d].un)
#define GFS_FACE_NORMAL_VELOCITY_LEFT(fa)\
  (GFS_STATE ((fa)->cell)->f[(fa)->d].un)
#define GFS_FACE_NORMAL_VELOCITY_RIGHT(fa)\
  (GFS_STATE ((fa)->neighbor)->f[FTT_OPPOSITE_DIRECTION ((fa)->d)].un)

#define GFS_FACE_FRACTION(fa) (GFS_IS_MIXED ((fa)->cell) ?\
                               GFS_STATE ((fa)->cell)->solid->s[(fa)->d] : 1.)
#define GFS_FACE_FRACTION_LEFT(fa) GFS_FACE_FRACTION(fa)
#define GFS_FACE_FRACTION_RIGHT(fa) (GFS_IS_MIXED ((fa)->neighbor) ?\
                 GFS_STATE ((fa)->neighbor)->solid->s[FTT_OPPOSITE_DIRECTION ((fa)->d)] : 1.)

#define GFS_IS_FLUID(cell)      ((cell) != NULL &&\
                                 GFS_STATE (cell)->solid == NULL)
#define GFS_IS_MIXED(cell)      ((cell) != NULL &&\
                                 GFS_STATE (cell)->solid != NULL)
#define GFS_CELL_IS_BOUNDARY(cell) (((cell)->flags & GFS_FLAG_BOUNDARY) != 0)
#define GFS_CELL_IS_GRADIENT_BOUNDARY(cell) (((cell)->flags & GFS_FLAG_GRADIENT_BOUNDARY) != 0)

FttCellFace           gfs_cell_face                 (FttCell * cell,
						     FttDirection d);
void                  gfs_cell_cleanup              (FttCell * cell);
void                  gfs_cell_reset                (FttCell * cell, 
						     GfsVariable * v);
void                  gfs_get_from_below_intensive  (FttCell * cell, 
						     const GfsVariable * v);
void                  gfs_get_from_below_extensive  (FttCell * cell, 
						     const GfsVariable * v);
void                  gfs_cell_coarse_fine          (FttCell * cell,
						     GfsVariable * v);
gdouble               gfs_face_interpolated_value   (const FttCellFace * face,
						     guint v);
typedef gdouble    (* GfsCenterGradient)            (FttCell * cell,
						     FttComponent c,
						     guint v);
gdouble               gfs_center_gradient           (FttCell * cell,
						     FttComponent c,
						     guint v);
gdouble               gfs_center_van_leer_gradient  (FttCell * cell,
						     FttComponent c,
						     guint v);

typedef struct _GfsGradient GfsGradient;

struct _GfsGradient {
  gdouble a, b;
};

void                  gfs_face_gradient              (const FttCellFace * face,
						      GfsGradient * g,
						      guint v,
						      gint max_level);
void                  gfs_face_weighted_gradient     (const FttCellFace * face,
						      GfsGradient * g,
						      guint v,
						      gint max_level);
void                  gfs_face_gradient_flux         (const FttCellFace * face,
						      GfsGradient * g,
						      guint v,
						      gint max_level);
void                  gfs_cell_dirichlet_gradient    (FttCell * cell,
						      guint v,
						      gint max_level,
						      gdouble v0,
						      FttVector * grad);
void                  gfs_mixed_cell_gradient        (FttCell * cell,
						      GfsVariable * v,
						      FttVector * g);
gdouble               gfs_cell_dirichlet_gradient_flux (FttCell * cell,
							guint v,
							gint max_level,
							gdouble v0);
gdouble               gfs_cell_dirichlet_value         (FttCell * cell,
							GfsVariable * v,
							gint max_level);
void                  gfs_face_gradient_flux_centered(const FttCellFace * face,
						      GfsGradient * g,
						      guint v,
						      gint max_level);

void                  gfs_normal_divergence          (FttCell * cell,
						      GfsVariable * v);
void                  gfs_normal_divergence_2D       (FttCell * cell,
						      GfsVariable * v);
gdouble               gfs_divergence                 (FttCell * cell,
						      GfsVariable ** v);
gdouble               gfs_vorticity                  (FttCell * cell,
						      GfsVariable ** v);
gdouble               gfs_vector_norm                (FttCell * cell,
						      GfsVariable ** v);
gdouble               gfs_vector_norm2               (FttCell * cell,
						      GfsVariable ** v);
gdouble               gfs_vector_lambda2             (FttCell * cell,
						      GfsVariable ** v);
void                  gfs_pressure_force             (FttCell * cell,
						      GfsVariable * p,
						      FttVector * f);
GtsRange              gfs_stats_variable             (FttCell * root, 
						      GfsVariable * v, 
						      FttTraverseFlags flags,
						      gint max_depth);

#define               gfs_cell_volume(cell)   (ftt_cell_volume (cell)*(GFS_IS_MIXED (cell) ?\
					       GFS_STATE (cell)->solid->a : 1.))

typedef struct _GfsNorm GfsNorm;

struct _GfsNorm {
  gdouble bias, first, second, infty, w;
};

void                  gfs_norm_init                 (GfsNorm * n);
void                  gfs_norm_reset                (GfsNorm * n);
void                  gfs_norm_add                  (GfsNorm * n, 
						     gdouble val,
						     gdouble weight);
void                  gfs_norm_update               (GfsNorm * n);

GfsNorm               gfs_norm_variable             (FttCell * root, 
						     GfsVariable * v, 
						     FttTraverseFlags flags,
						     gint max_depth);
  
void                  gfs_cell_traverse_mixed       (FttCell * root,
						     FttTraverseType order,
						     FttTraverseFlags flags,
						     FttCellTraverseFunc func,
						     gpointer data);
GtsSurface *          gfs_cell_is_cut               (FttCell * cell,
						     GtsSurface * s,
						     gboolean flatten);
typedef void       (* FttCellTraverseCutFunc)       (FttCell * cell,
						     GtsSurface * s,
						     gpointer data);
void                  gfs_cell_traverse_cut         (FttCell * root,
						     GtsSurface * s,
						     FttTraverseType order,
						     FttTraverseFlags flags,
						     FttCellTraverseCutFunc func,
						     gpointer data);
void                  gfs_cell_traverse_cut_2D      (FttCell * root,
						     GtsSurface * s,
						     FttTraverseType order,
						     FttTraverseFlags flags,
						     FttCellTraverseCutFunc func,
						     gpointer data);
gdouble               gfs_interpolate               (FttCell * cell,
						     FttVector p,
						     GfsVariable * v);
void                  ftt_cell_refine_corners       (FttCell * cell,
						     FttCellInitFunc init,
						     gpointer data);
gdouble               gfs_center_curvature          (FttCell * cell,
						     FttComponent c,
						     guint v);
gdouble               gfs_streamline_curvature      (FttCell * cell,
						     GfsVariable ** v);
void                  gfs_shear_strain_rate_tensor  (FttCell * cell, 
						     GfsVariable ** u,
						     gdouble t[FTT_DIMENSION][FTT_DIMENSION]);
gdouble               gfs_2nd_principal_invariant   (FttCell * cell, 
						     GfsVariable ** u);

typedef struct {
#if FTT_2D
  FttCell * c[7];
  gdouble w[7];
#else  /* 3D */
  FttCell * c[29];
  gdouble w[29];
#endif /* 3D */
  guint n;  
} GfsInterpolator;

void                  gfs_cell_corner_interpolator  (FttCell * cell,
						     FttDirection d[FTT_DIMENSION],
						     gint max_level,
						     gboolean centered,
						     GfsInterpolator * inter);
gdouble               gfs_cell_corner_value         (FttCell * cell,
						     FttDirection * d,
						     GfsVariable * v,
						     gint max_level);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __FLUID_H__ */
