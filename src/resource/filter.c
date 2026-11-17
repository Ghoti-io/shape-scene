/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Shape Scene.
 *
 * Ghoti.io Shape Scene is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Shape Scene is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * The filter resource (filter.h, FL1 to FL6, FL12): its validity, canonical form, hash, equality, dump and reading back.  The
 * canonical form is one byte string (`serialize`) of the fields each kind uses, so that the hash, the equality and the format of
 * the content are one definition.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <string.h>

/** The format of a filter's hash and dump. */
#define FILTER_FORMAT_VERSION 1u

#define HEX(x) ((unsigned long long)(x))

/** The accounted bytes of each primitive of a filter and of each value it holds (resource.h). */
#define BYTES_PER_PRIMITIVE ((size_t)512)
#define BYTES_PER_VALUE ((size_t)8)

/* --- the field groups of a kind (FL2, FL6) --------------------------------------------------------------------------- */

enum {
  G_OFFSET = 1u << 0,   /* dx, dy */
  G_COLOR = 1u << 1,    /* color */
  G_OPACITY = 1u << 2,  /* opacity */
  G_MODE = 1u << 3,     /* mode */
  G_K = 1u << 4,        /* k[4] (arithmetic only) */
  G_VALUES = 1u << 5,   /* value_count, values */
  G_TRANSFER = 1u << 6, /* transfer[4] */
  G_SXY = 1u << 7,      /* sx, sy */
  G_EDGE = 1u << 8,     /* edge_mode */
  G_CONV = 1u << 9,     /* order, target, divisor, bias, preserve_alpha */
  G_DISP = 1u << 10,    /* scale, x_channel, y_channel */
  G_NOISE = 1u << 11,   /* octaves, seed, stitch_tiles */
  G_LIGHT = 1u << 12,   /* surface_scale, constant, light */
  G_SPEC = 1u << 13,    /* exponent */
  G_KUL = 1u << 14,     /* kernel_unit_length */
  G_IMAGE = 1u << 15    /* image, placement, rendering */
};

static uint32_t groups_of(uint32_t kind, uint32_t mode) {
  switch (kind) {
    case GSHSCENE_FILTER_OFFSET:
      return G_OFFSET;
    case GSHSCENE_FILTER_FLOOD:
      return G_COLOR | G_OPACITY;
    case GSHSCENE_FILTER_MERGE:
      return 0;
    case GSHSCENE_FILTER_BLEND:
      return G_MODE;
    case GSHSCENE_FILTER_COMPOSITE:
      return G_MODE | (mode == GSHSCENE_FILTER_COMPOSITE_ARITHMETIC ? G_K : 0u);
    case GSHSCENE_FILTER_COLOR_MATRIX:
      return G_MODE | G_VALUES;
    case GSHSCENE_FILTER_COMPONENT_TRANSFER:
      return G_TRANSFER;
    case GSHSCENE_FILTER_GAUSSIAN_BLUR:
      return G_SXY | G_EDGE;
    case GSHSCENE_FILTER_DROP_SHADOW:
      return G_OFFSET | G_SXY | G_COLOR | G_OPACITY;
    case GSHSCENE_FILTER_MORPHOLOGY:
      return G_MODE | G_SXY;
    case GSHSCENE_FILTER_CONVOLVE_MATRIX:
      return G_VALUES | G_EDGE | G_CONV | G_KUL;
    case GSHSCENE_FILTER_TILE:
      return 0;
    case GSHSCENE_FILTER_IMAGE:
      return G_IMAGE;
    case GSHSCENE_FILTER_DISPLACEMENT_MAP:
      return G_DISP;
    case GSHSCENE_FILTER_TURBULENCE:
      return G_MODE | G_SXY | G_NOISE;
    case GSHSCENE_FILTER_DIFFUSE_LIGHTING:
      return G_COLOR | G_LIGHT | G_KUL;
    case GSHSCENE_FILTER_SPECULAR_LIGHTING:
      return G_COLOR | G_LIGHT | G_SPEC | G_KUL;
    default:
      return 0;
  }
}

static int zero_d(double x) {
  return x == 0.0;
}

static int color_is_zero(const GSHSCENE_Color * c) {
  return zero_d(c->rgba[0]) && zero_d(c->rgba[1]) && zero_d(c->rgba[2]) && zero_d(c->rgba[3]) &&
      gshscene_color_space_is_target(&c->space);
}

static int transfer_is_zero(const GSHSCENE_Filter_Transfer * t) {
  return t->type == 0 && t->count == 0 && !t->table && zero_d(t->slope) && zero_d(t->intercept) && zero_d(t->amplitude) &&
      zero_d(t->exponent) && zero_d(t->offset);
}

static int light_is_zero(const GSHSCENE_Filter_Light * l) {
  return l->kind == 0 && zero_d(l->azimuth) && zero_d(l->elevation) && zero_d(l->position[0]) && zero_d(l->position[1]) &&
      zero_d(l->position[2]) && zero_d(l->points_at[0]) && zero_d(l->points_at[1]) && zero_d(l->points_at[2]) &&
      zero_d(l->spot_exponent) && l->has_cone == 0 && zero_d(l->cone_angle);
}

/** Whether every field of the groups NOT in @p used is zero (FL1: a field that cannot matter must not split a handle). */
static int unused_zero(const GSHSCENE_Filter_Primitive * p, uint32_t used) {
  if (!(used & G_OFFSET) && !(zero_d(p->dx) && zero_d(p->dy))) {
    return 0;
  }
  if (!(used & G_COLOR) && !color_is_zero(&p->color)) {
    return 0;
  }
  if (!(used & G_OPACITY) && !zero_d(p->opacity)) {
    return 0;
  }
  if (!(used & G_MODE) && p->mode != 0) {
    return 0;
  }
  if (!(used & G_K) && !(zero_d(p->k[0]) && zero_d(p->k[1]) && zero_d(p->k[2]) && zero_d(p->k[3]))) {
    return 0;
  }
  if (!(used & G_VALUES) && (p->value_count != 0 || p->values)) {
    return 0;
  }
  if (!(used & G_TRANSFER)) {
    for (int i = 0; i < 4; i++) {
      if (!transfer_is_zero(&p->transfer[i])) {
        return 0;
      }
    }
  }
  if (!(used & G_SXY) && !(zero_d(p->sx) && zero_d(p->sy))) {
    return 0;
  }
  if (!(used & G_EDGE) && p->edge_mode != 0) {
    return 0;
  }
  if (!(used & G_CONV) && !(p->order_x == 0 && p->order_y == 0 && p->target_x == 0 && p->target_y == 0 && zero_d(p->divisor) &&
                              zero_d(p->bias) && p->preserve_alpha == 0)) {
    return 0;
  }
  if (!(used & G_DISP) && !(zero_d(p->scale) && p->x_channel == 0 && p->y_channel == 0)) {
    return 0;
  }
  if (!(used & G_NOISE) && !(p->octaves == 0 && zero_d(p->seed) && p->stitch_tiles == 0)) {
    return 0;
  }
  if (!(used & G_LIGHT) && !(zero_d(p->surface_scale) && zero_d(p->constant) && light_is_zero(&p->light))) {
    return 0;
  }
  if (!(used & G_SPEC) && !zero_d(p->exponent)) {
    return 0;
  }
  if (!(used & G_KUL) && !(zero_d(p->kernel_unit_length[0]) && zero_d(p->kernel_unit_length[1]))) {
    return 0;
  }
  if (!(used & G_IMAGE) &&
      !(p->image == NULL && p->placement.dim == 0 && p->rendering == 0 && p->use_picture == 0 && p->slot == 0)) {
    return 0;
  }
  return 1;
}

/* --- validity (FL1, FL2, FL6, D-FL10) ------------------------------------------------------------------------------- */

typedef struct Tally {
  size_t values; /* table entries, matrix values and kernel entries over the whole filter */
  int limit;     /* a bound was passed: LIMIT, decided after every INVALID */
  int unsupported;
} Tally;

static int in_range(double x, double lo, double hi) {
  return gshscene_finite(x) && x >= lo && x <= hi;
}

/** A finite number; past the magnitude bound it marks LIMIT. */
static int number(double x, Tally * t) {
  if (!gshscene_finite(x)) {
    return 0;
  }
  if (x > GSHSCENE_FILTER_VALUE_MAX || x < -GSHSCENE_FILTER_VALUE_MAX) {
    t->limit = 1;
  }
  return 1;
}

static int nonneg(double x, Tally * t) {
  return number(x, t) && x >= 0.0;
}

static int input_arity_ok(const GSHSCENE_Filter_Primitive * p) {
  switch (p->kind) {
    case GSHSCENE_FILTER_FLOOD:
    case GSHSCENE_FILTER_TURBULENCE:
    case GSHSCENE_FILTER_IMAGE:
      return p->input_count == 0;
    case GSHSCENE_FILTER_BLEND:
    case GSHSCENE_FILTER_COMPOSITE:
    case GSHSCENE_FILTER_DISPLACEMENT_MAP:
      return p->input_count == 2;
    case GSHSCENE_FILTER_MERGE:
      return p->input_count >= 1;
    default:
      return p->input_count == 1;
  }
}

static int check_transfer(const GSHSCENE_Filter_Transfer * t, Tally * tally) {
  switch (t->type) {
    case GSHSCENE_FILTER_TRANSFER_IDENTITY:
      return transfer_is_zero(t);
    case GSHSCENE_FILTER_TRANSFER_TABLE:
    case GSHSCENE_FILTER_TRANSFER_DISCRETE:
      if (t->count < 1 || !t->table || !zero_d(t->slope) || !zero_d(t->intercept) || !zero_d(t->amplitude) ||
          !zero_d(t->exponent) || !zero_d(t->offset)) {
        return 0;
      }
      for (size_t i = 0; i < t->count; i++) {
        if (!number(t->table[i], tally)) {
          return 0;
        }
      }
      tally->values = gshscene_sat_add(tally->values, t->count);
      return 1;
    case GSHSCENE_FILTER_TRANSFER_LINEAR:
      return t->count == 0 && !t->table && zero_d(t->amplitude) && zero_d(t->exponent) && zero_d(t->offset) &&
          number(t->slope, tally) && number(t->intercept, tally);
    case GSHSCENE_FILTER_TRANSFER_GAMMA:
      return t->count == 0 && !t->table && zero_d(t->slope) && zero_d(t->intercept) && number(t->amplitude, tally) &&
          in_range(t->exponent, 0.0, GSHSCENE_FILTER_VALUE_MAX) && number(t->offset, tally);
    default:
      return 0;
  }
}

static int check_light(const GSHSCENE_Filter_Light * l, Tally * t) {
  switch (l->kind) {
    case GSHSCENE_FILTER_LIGHT_DISTANT:
      return number(l->azimuth, t) && number(l->elevation, t) && zero_d(l->position[0]) && zero_d(l->position[1]) &&
          zero_d(l->position[2]) && zero_d(l->points_at[0]) && zero_d(l->points_at[1]) && zero_d(l->points_at[2]) &&
          zero_d(l->spot_exponent) && l->has_cone == 0 && zero_d(l->cone_angle);
    case GSHSCENE_FILTER_LIGHT_POINT:
      return zero_d(l->azimuth) && zero_d(l->elevation) && number(l->position[0], t) && number(l->position[1], t) &&
          number(l->position[2], t) && zero_d(l->points_at[0]) && zero_d(l->points_at[1]) && zero_d(l->points_at[2]) &&
          zero_d(l->spot_exponent) && l->has_cone == 0 && zero_d(l->cone_angle);
    case GSHSCENE_FILTER_LIGHT_SPOT:
      return zero_d(l->azimuth) && zero_d(l->elevation) && number(l->position[0], t) && number(l->position[1], t) &&
          number(l->position[2], t) && number(l->points_at[0], t) && number(l->points_at[1], t) &&
          number(l->points_at[2], t) && gshscene_finite(l->spot_exponent) && l->spot_exponent > 0.0 &&
          l->spot_exponent <= GSHSCENE_FILTER_VALUE_MAX && l->has_cone <= 1u &&
          (l->has_cone ? number(l->cone_angle, t) : zero_d(l->cone_angle));
    default:
      return 0;
  }
}

/** The primitive's own fields (FL6), after the common ones.  0 for invalid. */
static int check_params(const GSHSCENE_Store * store, const GSHSCENE_Filter * f, const GSHSCENE_Filter_Primitive * p, Tally * t) {
  const uint32_t used = groups_of(p->kind, p->mode);
  if (!unused_zero(p, used)) {
    return 0;
  }
  if (used & G_OFFSET) {
    if (!number(p->dx, t) || !number(p->dy, t)) {
      return 0;
    }
  }
  if (used & G_COLOR) {
    GSHSCENE_Result r = gshscene_color_check(&p->color);
    if (r == GSHSCENE_ERR_LIMIT) {
      t->limit = 1;
    } else if (r != GSHSCENE_OK) {
      return 0;
    }
  }
  if ((used & G_OPACITY) && !in_range(p->opacity, 0.0, 1.0)) {
    return 0;
  }
  if (used & G_MODE) {
    switch (p->kind) {
      case GSHSCENE_FILTER_BLEND:
        /* normal (source-over) or one of P3's blend modes, the standard's sixteen without normal */
        if (!(p->mode == (uint32_t)GSHSCENE_COMPOSITE_SRC_OVER ||
                (p->mode >= (uint32_t)GSHSCENE_COMPOSITE_SCREEN && p->mode < (uint32_t)GSHSCENE_COMPOSITE_COUNT))) {
          return 0;
        }
        break;
      case GSHSCENE_FILTER_COMPOSITE:
        if (p->mode >= (uint32_t)GSHSCENE_FILTER_COMPOSITE_COUNT) {
          return 0;
        }
        break;
      case GSHSCENE_FILTER_COLOR_MATRIX:
        if (p->mode >= (uint32_t)GSHSCENE_FILTER_MATRIX_COUNT) {
          return 0;
        }
        break;
      case GSHSCENE_FILTER_MORPHOLOGY:
        if (p->mode >= (uint32_t)GSHSCENE_FILTER_MORPHOLOGY_COUNT) {
          return 0;
        }
        break;
      case GSHSCENE_FILTER_TURBULENCE:
        if (p->mode >= (uint32_t)GSHSCENE_FILTER_NOISE_COUNT) {
          return 0;
        }
        break;
      default:
        break;
    }
  }
  if (used & G_K) {
    for (int i = 0; i < 4; i++) {
      if (!number(p->k[i], t)) {
        return 0;
      }
    }
  }
  if (used & G_VALUES) {
    if (p->kind == GSHSCENE_FILTER_COLOR_MATRIX) {
      const size_t want = p->mode == GSHSCENE_FILTER_MATRIX_MATRIX ? 20u
          : (p->mode == GSHSCENE_FILTER_MATRIX_SATURATE || p->mode == GSHSCENE_FILTER_MATRIX_HUE_ROTATE) ? 1u : 0u;
      if (p->value_count != want || (want && !p->values) || (!want && p->values)) {
        return 0;
      }
      if (p->mode == GSHSCENE_FILTER_MATRIX_SATURATE && p->values && !(p->values[0] >= 0.0)) {
        return 0;
      }
    } else { /* CONVOLVE_MATRIX: order_x * order_y values */
      const uint64_t cells = (uint64_t)p->order_x * p->order_y;
      if (p->order_x < 1 || p->order_y < 1 || (uint64_t)p->value_count != cells || !p->values) {
        return 0;
      }
      if (cells > GSHSCENE_FILTER_MAX_KERNEL) {
        t->limit = 1;
      }
    }
    if (p->values) {
      for (size_t i = 0; i < p->value_count; i++) {
        if (!number(p->values[i], t)) {
          return 0;
        }
      }
    }
    t->values = gshscene_sat_add(t->values, p->value_count);
  }
  if (used & G_TRANSFER) {
    for (int i = 0; i < 4; i++) {
      if (!check_transfer(&p->transfer[i], t)) {
        return 0;
      }
    }
  }
  if (used & G_SXY) {
    if (!nonneg(p->sx, t) || !nonneg(p->sy, t)) {
      return 0;
    }
  }
  if ((used & G_EDGE) && p->edge_mode >= (uint32_t)GSHSCENE_FILTER_EDGE_COUNT) {
    return 0;
  }
  if (used & G_CONV) {
    if (p->target_x >= p->order_x || p->target_y >= p->order_y || !number(p->divisor, t) || p->divisor == 0.0 ||
        !number(p->bias, t) || p->preserve_alpha > 1u) {
      return 0;
    }
  }
  if (used & G_DISP) {
    if (!number(p->scale, t) || p->x_channel >= (uint32_t)GSHSCENE_FILTER_CHANNEL_COUNT ||
        p->y_channel >= (uint32_t)GSHSCENE_FILTER_CHANNEL_COUNT) {
      return 0;
    }
  }
  if (used & G_NOISE) {
    if (p->octaves > GSHSCENE_FILTER_MAX_OCTAVES) {
      t->limit = 1;
    }
    if (!number(p->seed, t) || p->stitch_tiles > 1u) {
      return 0;
    }
  }
  if (used & G_LIGHT) {
    if (!number(p->surface_scale, t) || !nonneg(p->constant, t) || !check_light(&p->light, t)) {
      return 0;
    }
  }
  if ((used & G_SPEC) && !in_range(p->exponent, 1.0, 128.0)) {
    return 0;
  }
  if (used & G_KUL) {
    if (!nonneg(p->kernel_unit_length[0], t) || !nonneg(p->kernel_unit_length[1], t)) {
      return 0;
    }
  }
  if (used & G_IMAGE) {
    if (p->use_picture > 1u) {
      return 0;
    }
    if (p->use_picture) {
      /* FL25: a slot of the filter's picture; no image, no placement (the slot's instance map places it), no rendering. */
      if (p->image || p->placement.dim != 0 || p->rendering != 0 || !f->picture ||
          p->slot >= gshscene_list_count(gshscene_resource_picture(f->picture))) {
        return 0;
      }
    } else {
      if (p->slot != 0 || !p->image || p->image->kind != GSHSCENE_RESOURCE_IMAGE || p->image->store != store ||
          p->rendering >= (uint32_t)GSHSCENE_IMAGE_FILTER_COUNT) {
        return 0;
      }
    }
    if (p->use_picture) {
      /* nothing to check on the placement */
    } else if (p->placement.dim == 3) {
      t->unsupported = 1;
    } else if (p->placement.dim != 0) {
      GSHGEOM_Map canonical;
      if (gshscene_list_canonical_map(&p->placement, &canonical) != GSHSCENE_OK || canonical.dim != 2) {
        return 0;
      }
    }
  }
  return 1;
}

/** FL25: the picture is present exactly when a primitive draws a slot of it, is of this store, and holds only instances. */
static int picture_ok(const GSHSCENE_Store * store, const GSHSCENE_Filter * f, size_t look) {
  int used = 0;
  for (size_t i = 0; i < look; i++) {
    used |= f->primitives[i].kind == GSHSCENE_FILTER_IMAGE && f->primitives[i].use_picture != 0;
  }
  if (!f->picture) {
    return 1; /* a slot without a picture was refused with its primitive */
  }
  if (!used || f->picture->kind != GSHSCENE_RESOURCE_PICTURE || f->picture->store != store) {
    return 0;
  }
  const GSHSCENE_List * list = gshscene_resource_picture(f->picture);
  for (size_t i = 0; i < gshscene_list_count(list); i++) {
    const GSHSCENE_Item * item = NULL;
    if (gshscene_list_item(list, i, &item) != GSHSCENE_OK || item->kind != GSHSCENE_ITEM_INSTANCE) {
      return 0;
    }
  }
  return 1;
}

static GSHSCENE_Result check(const GSHSCENE_Store * store, const GSHSCENE_Filter * f, Tally * tally) {
  memset(tally, 0, sizeof *tally);
  if (f->region_mode >= (uint32_t)GSHSCENE_FILTER_REGION_COUNT || (f->color_space != GSHSCENE_FILTER_SPACE_LINEAR_RGB &&
                                                                    f->color_space != GSHSCENE_FILTER_SPACE_SRGB) ||
      f->primitive_count == 0 || !f->primitives) {
    return GSHSCENE_ERR_INVALID;
  }
  if (f->region_mode == (uint32_t)GSHSCENE_FILTER_REGION_AUTO) {
    for (int i = 0; i < 4; i++) {
      if (!zero_d(f->region[i])) {
        return GSHSCENE_ERR_INVALID;
      }
    }
  } else {
    for (int i = 0; i < 4; i++) {
      if (!number(f->region[i], tally)) {
        return GSHSCENE_ERR_INVALID;
      }
    }
    if (f->region[2] < 0.0 || f->region[3] < 0.0) {
      return GSHSCENE_ERR_INVALID;
    }
  }
  if (f->primitive_count > GSHSCENE_FILTER_MAX_PRIMITIVES) {
    tally->limit = 1;
  }
  const size_t look = f->primitive_count < GSHSCENE_FILTER_MAX_PRIMITIVES ? f->primitive_count : GSHSCENE_FILTER_MAX_PRIMITIVES;
  for (size_t i = 0; i < look; i++) {
    const GSHSCENE_Filter_Primitive * p = &f->primitives[i];
    if (p->kind == 0 || p->kind >= (uint32_t)GSHSCENE_FILTER_KIND_COUNT ||
        p->color_space >= (uint32_t)GSHSCENE_FILTER_SPACE_COUNT || !input_arity_ok(p) ||
        (p->input_count && !p->inputs) || (!p->input_count && p->inputs) || p->subregion_mask > 15u) {
      return GSHSCENE_ERR_INVALID;
    }
    if (p->kind == GSHSCENE_FILTER_MERGE && p->input_count > GSHSCENE_FILTER_MAX_MERGE) {
      tally->limit = 1;
    } else {
      for (size_t k = 0; k < p->input_count; k++) {
        const GSHSCENE_Filter_Input * in = &p->inputs[k];
        if (in->kind >= (uint32_t)GSHSCENE_FILTER_INPUT_COUNT ||
            (in->kind == (uint32_t)GSHSCENE_FILTER_INPUT_RESULT ? in->index >= i : in->index != 0)) {
          return GSHSCENE_ERR_INVALID;
        }
      }
    }
    for (int k = 0; k < 4; k++) {
      if (p->subregion_mask & (1u << k)) {
        if (!number(p->subregion[k], tally) || (k >= 2 && p->subregion[k] < 0.0)) {
          return GSHSCENE_ERR_INVALID;
        }
      } else if (!zero_d(p->subregion[k])) {
        return GSHSCENE_ERR_INVALID;
      }
    }
    if (!check_params(store, f, p, tally)) {
      return GSHSCENE_ERR_INVALID;
    }
  }
  if (!picture_ok(store, f, look)) {
    return GSHSCENE_ERR_INVALID;
  }
  if (tally->values > GSHSCENE_FILTER_MAX_VALUES) {
    tally->limit = 1;
  }
  if (tally->unsupported) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }
  return tally->limit ? GSHSCENE_ERR_LIMIT : GSHSCENE_OK;
}

/* --- the canonical byte string (FL12) -------------------------------------------------------------------------------- */

typedef struct Out {
  uint8_t * buf; /* NULL: only count */
  size_t n;
} Out;

static void put8(Out * o, uint8_t v) {
  if (o->buf) {
    o->buf[o->n] = v;
  }
  o->n++;
}

static void put32(Out * o, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    put8(o, (uint8_t)(v >> (8 * i)));
  }
}

static void put64(Out * o, uint64_t v) {
  for (int i = 0; i < 8; i++) {
    put8(o, (uint8_t)(v >> (8 * i)));
  }
}

/** A number, `-0` as `+0` (one spelling for one number). */
static void putf(Out * o, double v) {
  /* a negative zero is a zero; selected on the bits, since a conditional on doubles compiles to x87 selects on a 32-bit x86 build */
  uint64_t bits = gshgeom_f64_bits(v);
  if (v == 0.0) {
    bits = 0;
  }
  put64(o, bits);
}

static void put_color(Out * o, const GSHSCENE_Color * c) {
  for (int i = 0; i < 4; i++) {
    putf(o, c->rgba[i]);
  }
  put8(o, (uint8_t)c->space.primaries);
  put8(o, (uint8_t)c->space.transfer);
  putf(o, c->space.gamma);
}

static void put_map(Out * o, const GSHGEOM_Map * map) {
  GSHGEOM_Map m;
  if (map->dim == 0) {
    gshgeom_map_identity(2, &m);
  } else {
    (void)gshscene_list_canonical_map(map, &m);
  }
  put32(o, (uint32_t)m.dim);
  put32(o, (uint32_t)m.kind);
  for (int i = 0; i < m.dim; i++) {
    for (int j = 0; j < m.dim; j++) {
      putf(o, m.m[i][j]);
    }
    putf(o, m.t[i]);
  }
}

static void put_transfer(Out * o, const GSHSCENE_Filter_Transfer * t) {
  put8(o, (uint8_t)t->type);
  put64(o, (uint64_t)t->count);
  for (size_t i = 0; i < t->count; i++) {
    putf(o, t->table[i]);
  }
  putf(o, t->slope);
  putf(o, t->intercept);
  putf(o, t->amplitude);
  putf(o, t->exponent);
  putf(o, t->offset);
}

static void put_light(Out * o, const GSHSCENE_Filter_Light * l) {
  put8(o, (uint8_t)l->kind);
  putf(o, l->azimuth);
  putf(o, l->elevation);
  for (int i = 0; i < 3; i++) {
    putf(o, l->position[i]);
  }
  for (int i = 0; i < 3; i++) {
    putf(o, l->points_at[i]);
  }
  putf(o, l->spot_exponent);
  put8(o, (uint8_t)l->has_cone);
  putf(o, l->cone_angle);
}

static void put_primitive(Out * o, const GSHSCENE_Filter_Primitive * p) {
  put8(o, (uint8_t)p->kind);
  put8(o, (uint8_t)p->color_space);
  put64(o, (uint64_t)p->input_count);
  for (size_t i = 0; i < p->input_count; i++) {
    put8(o, (uint8_t)p->inputs[i].kind);
    put32(o, p->inputs[i].index);
  }
  put8(o, (uint8_t)p->subregion_mask);
  for (int i = 0; i < 4; i++) {
    putf(o, p->subregion[i]);
  }
  const uint32_t used = groups_of(p->kind, p->mode);
  if (used & G_OFFSET) {
    putf(o, p->dx);
    putf(o, p->dy);
  }
  if (used & G_COLOR) {
    put_color(o, &p->color);
  }
  if (used & G_OPACITY) {
    putf(o, p->opacity);
  }
  if (used & G_MODE) {
    put32(o, p->mode);
  }
  if (used & G_K) {
    for (int i = 0; i < 4; i++) {
      putf(o, p->k[i]);
    }
  }
  if (used & G_VALUES) {
    put64(o, (uint64_t)p->value_count);
    for (size_t i = 0; i < p->value_count; i++) {
      putf(o, p->values[i]);
    }
  }
  if (used & G_TRANSFER) {
    for (int i = 0; i < 4; i++) {
      put_transfer(o, &p->transfer[i]);
    }
  }
  if (used & G_SXY) {
    putf(o, p->sx);
    putf(o, p->sy);
  }
  if (used & G_EDGE) {
    put32(o, p->edge_mode);
  }
  if (used & G_CONV) {
    put32(o, p->order_x);
    put32(o, p->order_y);
    put32(o, p->target_x);
    put32(o, p->target_y);
    putf(o, p->divisor);
    putf(o, p->bias);
    put32(o, p->preserve_alpha);
  }
  if (used & G_DISP) {
    putf(o, p->scale);
    put32(o, p->x_channel);
    put32(o, p->y_channel);
  }
  if (used & G_NOISE) {
    put32(o, p->octaves);
    putf(o, p->seed);
    put32(o, p->stitch_tiles);
  }
  if (used & G_LIGHT) {
    putf(o, p->surface_scale);
    putf(o, p->constant);
    put_light(o, &p->light);
  }
  if (used & G_SPEC) {
    putf(o, p->exponent);
  }
  if (used & G_KUL) {
    putf(o, p->kernel_unit_length[0]);
    putf(o, p->kernel_unit_length[1]);
  }
  if (used & G_IMAGE) {
    if (p->use_picture) {
      GSHGEOM_Map identity;
      gshgeom_map_identity(2, &identity);
      put64(o, 0);
      put_map(o, &identity);
      put32(o, UINT32_MAX);
      put32(o, p->slot);
    } else {
      put64(o, gshscene_resource_hash(p->image));
      put_map(o, &p->placement);
      put32(o, p->rendering);
    }
  }
}

/** The canonical string of @p f; its length. @p buf may be NULL to measure. */
static size_t serialize(const GSHSCENE_Filter * f, uint8_t * buf) {
  Out o = {buf, 0};
  put32(&o, FILTER_FORMAT_VERSION);
  put32(&o, (uint32_t)GSHSCENE_RESOURCE_FILTER);
  put32(&o, f->region_mode);
  for (int i = 0; i < 4; i++) {
    putf(&o, f->region[i]);
  }
  put8(&o, (uint8_t)f->color_space);
  put64(&o, (uint64_t)f->primitive_count);
  for (size_t i = 0; i < f->primitive_count; i++) {
    put_primitive(&o, &f->primitives[i]);
  }
  if (f->picture) {
    put64(&o, gshscene_resource_hash(f->picture)); /* FL25 */
  }
  return o.n;
}

static uint64_t bytes_hash(const uint8_t * bytes, size_t n) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, FILTER_FORMAT_VERSION);
  gshgeom_hash_u32(&h, (uint32_t)GSHSCENE_RESOURCE_FILTER);
  gshgeom_hash_u64(&h, (uint64_t)n);
  gshgeom_hash_bytes(&h, bytes, n);
  return gshgeom_hash_value(&h);
}

/* --- interning ---------------------------------------------------------------------------------------------------- */

typedef struct Source {
  const GSHSCENE_Filter * filter;
  uint8_t * canon;   /* the canonical string of the probe, owned by the caller of intern */
  size_t canon_len;
} Source;

int gshscene_filter_matches(const GSHSCENE_Resource * resource, const void * source) {
  const Source * want = (const Source *)source;
  if (resource->u.filter.canon_len != want->canon_len ||
      memcmp(resource->u.filter.canon, want->canon, want->canon_len) != 0) {
    return 0;
  }
  /* the string holds an image's content hash; the handle decides (a hash never does) */
  for (size_t i = 0; i < want->filter->primitive_count; i++) {
    if (resource->u.filter.f.primitives[i].image != want->filter->primitives[i].image) {
      return 0;
    }
  }
  return resource->u.filter.f.picture == want->filter->picture;
}

static GSHSCENE_Result build(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  const Source * src = (const Source *)source;
  const GSHSCENE_Filter * in = src->filter;
  /* one block: the primitives, then every input array, then every double array, in primitive order */
  size_t inputs = 0, doubles = 0;
  for (size_t i = 0; i < in->primitive_count; i++) {
    const GSHSCENE_Filter_Primitive * p = &in->primitives[i];
    inputs += p->input_count;
    doubles += p->value_count;
    for (int k = 0; k < 4; k++) {
      doubles += p->transfer[k].count;
    }
  }
  const size_t bytes = in->primitive_count * sizeof(GSHSCENE_Filter_Primitive) + inputs * sizeof(GSHSCENE_Filter_Input) +
      doubles * sizeof(double);
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_FILTER);
  if (!resource) {
    return GSHSCENE_ERR_OOM;
  }
  uint8_t * arena = gshscene_alloc(&store->alloc, bytes);
  uint8_t * canon = gshscene_alloc(&store->alloc, src->canon_len);
  if (!arena || !canon) {
    gshscene_free(&store->alloc, arena);
    gshscene_free(&store->alloc, canon);
    gshscene_store_discard_resource(resource);
    return GSHSCENE_ERR_OOM;
  }
  memcpy(canon, src->canon, src->canon_len);
  GSHSCENE_Filter_Primitive * prims = (GSHSCENE_Filter_Primitive *)(void *)arena;
  const size_t at_inputs = in->primitive_count * sizeof *prims;
  GSHSCENE_Filter_Input * in_at = (GSHSCENE_Filter_Input *)(void *)(arena + at_inputs);
  double * d_at = (double *)(void *)(arena + at_inputs + inputs * sizeof *in_at);
  for (size_t i = 0; i < in->primitive_count; i++) {
    GSHSCENE_Filter_Primitive * q = &prims[i];
    *q = in->primitives[i];
    const uint32_t used = groups_of(q->kind, q->mode);
    /* the canonical copy has the unused fields zero (they were required to be) and the used ones as the string spells them */
    if (q->input_count) {
      memcpy(in_at, in->primitives[i].inputs, q->input_count * sizeof *in_at);
      q->inputs = in_at;
      in_at += q->input_count;
    }
    if (q->value_count) {
      for (size_t k = 0; k < q->value_count; k++) {
        d_at[k] = in->primitives[i].values[k] == 0.0 ? 0.0 : in->primitives[i].values[k];
      }
      q->values = d_at;
      d_at += q->value_count;
    }
    for (int k = 0; k < 4; k++) {
      GSHSCENE_Filter_Transfer * t = &q->transfer[k];
      if (t->count) {
        for (size_t m = 0; m < t->count; m++) {
          d_at[m] = in->primitives[i].transfer[k].table[m] == 0.0 ? 0.0 : in->primitives[i].transfer[k].table[m];
        }
        t->table = d_at;
        d_at += t->count;
      }
    }
    {
      /* every number with `-0` as `+0`: one spelling for one number, as the string spells it */
      double * scalars[] = {&q->dx, &q->dy, &q->opacity, &q->sx, &q->sy, &q->divisor, &q->bias, &q->scale, &q->seed,
          &q->surface_scale, &q->constant, &q->exponent, &q->kernel_unit_length[0], &q->kernel_unit_length[1], &q->k[0],
          &q->k[1], &q->k[2], &q->k[3], &q->subregion[0], &q->subregion[1], &q->subregion[2], &q->subregion[3],
          &q->color.rgba[0], &q->color.rgba[1], &q->color.rgba[2], &q->color.rgba[3], &q->color.space.gamma,
          &q->light.azimuth, &q->light.elevation, &q->light.position[0], &q->light.position[1], &q->light.position[2],
          &q->light.points_at[0], &q->light.points_at[1], &q->light.points_at[2], &q->light.spot_exponent, &q->light.cone_angle};
      for (size_t n = 0; n < sizeof scalars / sizeof scalars[0]; n++) {
        *scalars[n] = *scalars[n] == 0.0 ? 0.0 : *scalars[n];
      }
      for (int k = 0; k < 4; k++) {
        double * t[] = {&q->transfer[k].slope, &q->transfer[k].intercept, &q->transfer[k].amplitude, &q->transfer[k].exponent,
            &q->transfer[k].offset};
        for (size_t n = 0; n < 5; n++) {
          *t[n] = *t[n] == 0.0 ? 0.0 : *t[n];
        }
      }
    }
    if (used & G_IMAGE) {
      gshscene_resource_retain(q->image);
      if (q->use_picture) {
        /* FL25: the placement stays zero (the slot's instance places it), so a copy re-interns the same content */
      } else if (q->placement.dim == 0) {
        gshgeom_map_identity(2, &q->placement);
      } else {
        (void)gshscene_list_canonical_map(&in->primitives[i].placement, &q->placement);
      }
    }
  }
  resource->u.filter.f = *in;
  resource->u.filter.f.primitives = prims;
  gshscene_resource_retain(in->picture);
  for (int i = 0; i < 4; i++) {
    resource->u.filter.f.region[i] = in->region[i] == 0.0 ? 0.0 : in->region[i];
  }
  resource->u.filter.arena = arena;
  resource->u.filter.canon = canon;
  resource->u.filter.canon_len = src->canon_len;
  *out = resource;
  return GSHSCENE_OK;
}

void gshscene_filter_free(GSHSCENE_Resource * resource) {
  const GSHSCENE_Filter * f = &resource->u.filter.f;
  for (size_t i = 0; i < f->primitive_count; i++) {
    if (groups_of(f->primitives[i].kind, f->primitives[i].mode) & G_IMAGE) {
      gshscene_resource_release(f->primitives[i].image);
    }
  }
  gshscene_resource_release(f->picture);
  gshscene_free(&resource->store->alloc, resource->u.filter.arena);
  gshscene_free(&resource->store->alloc, resource->u.filter.canon);
}

GSHSCENE_Result gshscene_store_intern_filter(
    GSHSCENE_Store * store, const GSHSCENE_Filter * filter, const GSHSCENE_Resource ** out) {
  if (!store || !filter || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  Tally tally;
  GSHSCENE_Result r = check(store, filter, &tally);
  if (r != GSHSCENE_OK) {
    return r;
  }
  const size_t len = serialize(filter, NULL);
  uint8_t * canon = gshscene_alloc(&store->alloc, len);
  if (!canon) {
    return GSHSCENE_ERR_OOM;
  }
  serialize(filter, canon);
  Source src = {filter, canon, len};
  const size_t bytes = gshscene_sat_add(GSHSCENE_BYTES_BASE,
      gshscene_sat_add(gshscene_sat_mul(filter->primitive_count, BYTES_PER_PRIMITIVE),
          gshscene_sat_mul(tally.values, BYTES_PER_VALUE)));
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_FILTER, bytes_hash(canon, len), bytes, &src, build};
  r = gshscene_store_intern(store, &probe, out);
  gshscene_free(&store->alloc, canon);
  return r;
}

GSHSCENE_Result gshscene_resource_filter(const GSHSCENE_Resource * resource, GSHSCENE_Filter * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_FILTER) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = resource->u.filter.f;
  return GSHSCENE_OK;
}

/* --- what a sink needs (D-FL11) ----------------------------------------------------------------------------------- */

uint32_t gshscene_filter_required(const GSHSCENE_Resource * resource) {
  if (!resource || resource->kind != GSHSCENE_RESOURCE_FILTER) {
    return 0;
  }
  const GSHSCENE_Filter * f = &resource->u.filter.f;
  uint32_t need = 0;
  if (f->region_mode == (uint32_t)GSHSCENE_FILTER_REGION_AUTO) {
    need |= GSHSCENE_FILTER_NATIVE_REGION_AUTO;
  }
  if (f->picture) {
    need |= GSHSCENE_FILTER_NATIVE_PICTURE;
  }
  for (size_t i = 0; i < f->primitive_count; i++) {
    const GSHSCENE_Filter_Primitive * p = &f->primitives[i];
    need |= UINT32_C(1) << p->kind;
    if ((p->kind == GSHSCENE_FILTER_GAUSSIAN_BLUR || p->kind == GSHSCENE_FILTER_CONVOLVE_MATRIX) &&
        (p->edge_mode == (uint32_t)GSHSCENE_FILTER_EDGE_DUPLICATE || p->edge_mode == (uint32_t)GSHSCENE_FILTER_EDGE_WRAP)) {
      need |= GSHSCENE_FILTER_NATIVE_EDGE_DUPLICATE_WRAP;
    }
  }
  return need;
}

int gshscene_filter_axis_aligned(const GSHGEOM_Map * map) {
  if (!map || map->dim != 2) {
    return 0;
  }
  const double a = map->m[0][0], b = map->m[0][1], c = map->m[1][0], d = map->m[1][1];
  return (b == 0.0 && c == 0.0) || (a == 0.0 && d == 0.0);
}

int gshscene_filter_needs_canonical(const GSHGEOM_Map * instance_maps, size_t instance_count, const GSHGEOM_Map * target_map) {
  GSHGEOM_Map acc;
  gshgeom_map_identity(2, &acc);
  GSHGEOM_Result r = GSHGEOM_OK;
  for (size_t i = 0; r == GSHGEOM_OK && i < instance_count; i++) {
    r = gshgeom_map_compose(&acc, &instance_maps[i], &acc);
  }
  if (r == GSHGEOM_OK && target_map && target_map->dim != 0) {
    r = gshgeom_map_compose(target_map, &acc, &acc);
  }
  return r != GSHGEOM_OK || !gshscene_filter_axis_aligned(&acc);
}

/* --- the dump (FL12) ---------------------------------------------------------------------------------------------- */

static int dump_input(FILE * out, const GSHSCENE_Filter_Input * in, int first) {
  return fprintf(out, "%s%u:%u", first ? "" : ",", in->kind, in->index);
}

int gshscene_filter_dump(const GSHSCENE_Resource * resource, FILE * out) {
  const GSHSCENE_Filter * f = &resource->u.filter.f;
  if (fprintf(out, "filter region=%u x=%016llx y=%016llx w=%016llx h=%016llx cspace=%u primitives=%llu\n", f->region_mode,
          HEX(gshgeom_f64_bits(f->region[0])), HEX(gshgeom_f64_bits(f->region[1])), HEX(gshgeom_f64_bits(f->region[2])),
          HEX(gshgeom_f64_bits(f->region[3])), f->color_space, HEX(f->primitive_count)) < 0) {
    return -1;
  }
  for (size_t i = 0; i < f->primitive_count; i++) {
    const GSHSCENE_Filter_Primitive * p = &f->primitives[i];
    if (fprintf(out, "fp i=%llu kind=%u cspace=%u sub=%u %016llx %016llx %016llx %016llx in=", HEX(i), p->kind, p->color_space,
            p->subregion_mask, HEX(gshgeom_f64_bits(p->subregion[0])), HEX(gshgeom_f64_bits(p->subregion[1])),
            HEX(gshgeom_f64_bits(p->subregion[2])), HEX(gshgeom_f64_bits(p->subregion[3]))) < 0) {
      return -1;
    }
    if (p->input_count == 0 && fputc('-', out) == EOF) {
      return -1;
    }
    for (size_t k = 0; k < p->input_count; k++) {
      if (dump_input(out, &p->inputs[k], k == 0) < 0) {
        return -1;
      }
    }
    /* the fields of the kind, in the order of the canonical string, as hexadecimal bits; one more line of them */
    Out o = {NULL, 0};
    put_primitive(&o, p);
    uint8_t * bytes = gshscene_alloc(&resource->store->alloc, o.n);
    if (!bytes) {
      return -1;
    }
    o.buf = bytes;
    o.n = 0;
    put_primitive(&o, p);
    GSHGEOM_Hash h;
    gshgeom_hash_init(&h);
    gshgeom_hash_bytes(&h, bytes, o.n);
    const uint64_t digest = gshgeom_hash_value(&h);
    gshscene_free(&resource->store->alloc, bytes);
    if (fprintf(out, " params=%016llx\n", HEX(digest)) < 0) {
      return -1;
    }
  }
  return 0;
}
