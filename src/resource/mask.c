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
 * The mask (mask.h, MK1 to MK4, MK13): the resource, its canonical form, hash, equality and dump, the coefficient sets,
 * the transfer's one evaluation and the features a mask needs of a sink.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <ghoti.io/math/math.h>

#include <string.h>

/** The format of a mask's hash and dump (resource.h). */
#define MASK_FORMAT_VERSION 1u

#define HEX(x) ((unsigned long long)(x))

/** `-0` as `+0`: one spelling for one number. */
static double pz(double x) {
  return x == 0.0 ? 0.0 : x;
}

static int bits_equal(double a, double b) {
  return gshgeom_f64_bits(a) == gshgeom_f64_bits(b);
}

/** `clamp01`, with a NaN as 0 (mask.h). */
static double clamp01(double x) {
  return !(x > 0.0) ? 0.0 : (x > 1.0 ? 1.0 : x);
}

/* --- the canonical form (MK1, MK4) --------------------------------------------------------- */

/** What the checks of a transfer find that is not a validity failure. */
typedef struct Tally {
  size_t pieces;
  size_t samples;
  int limit;
} Tally;

/** The fields of one leaf (a whole transfer of kind identity, exponential or table, or a stitching's leaf). */
typedef struct Leaf {
  uint32_t kind;
  double c0, c1, n;
  size_t count;
  const double * samples;
} Leaf;

static GSHSCENE_Result check_leaf(const Leaf * leaf, Tally * tally) {
  switch (leaf->kind) {
    case GSHSCENE_TRANSFER_IDENTITY:
      if (leaf->c0 != 0.0 || leaf->c1 != 0.0 || leaf->n != 0.0 || leaf->count != 0 || leaf->samples) {
        return GSHSCENE_ERR_INVALID;
      }
      return GSHSCENE_OK;
    case GSHSCENE_TRANSFER_EXPONENTIAL:
      if (!gshscene_finite(leaf->c0) || !gshscene_finite(leaf->c1) || !gshscene_finite(leaf->n) || leaf->n < 0.0 ||
          leaf->count != 0 || leaf->samples) {
        return GSHSCENE_ERR_INVALID;
      }
      if (leaf->n > GSHSCENE_MASK_MAX_EXPONENT) {
        tally->limit = 1;
      }
      return GSHSCENE_OK;
    case GSHSCENE_TRANSFER_TABLE:
      if (leaf->count < 2 || !leaf->samples || leaf->c0 != 0.0 || leaf->c1 != 0.0 || leaf->n != 0.0) {
        return GSHSCENE_ERR_INVALID;
      }
      for (size_t i = 0; i < leaf->count; i++) {
        if (!gshscene_finite(leaf->samples[i])) {
          return GSHSCENE_ERR_INVALID;
        }
      }
      tally->samples = gshscene_sat_add(tally->samples, leaf->count);
      return GSHSCENE_OK;
    default:
      return GSHSCENE_ERR_INVALID;
  }
}

static GSHSCENE_Result check_transfer(const GSHSCENE_Transfer * t, Tally * tally) {
  if (t->kind != GSHSCENE_TRANSFER_STITCHING) {
    if (t->piece_count != 0 || t->pieces) {
      return GSHSCENE_ERR_INVALID;
    }
    Leaf leaf = {t->kind, t->c0, t->c1, t->n, t->sample_count, t->samples};
    return check_leaf(&leaf, tally);
  }
  if (t->piece_count < 1 || !t->pieces || t->c0 != 0.0 || t->c1 != 0.0 || t->n != 0.0 || t->sample_count != 0 ||
      t->samples) {
    return GSHSCENE_ERR_INVALID;
  }
  double expect_lo = 0.0;
  for (size_t i = 0; i < t->piece_count; i++) {
    const GSHSCENE_Transfer_Piece * p = &t->pieces[i];
    if (!gshscene_finite(p->lo) || !gshscene_finite(p->hi) || !gshscene_finite(p->e0) || !gshscene_finite(p->e1) ||
        p->lo != expect_lo || !(p->lo < p->hi) || !(p->e0 >= 0.0 && p->e0 <= 1.0) || !(p->e1 >= 0.0 && p->e1 <= 1.0) ||
        p->leaf_kind == (uint32_t)GSHSCENE_TRANSFER_STITCHING) {
      return GSHSCENE_ERR_INVALID;
    }
    Leaf leaf = {p->leaf_kind, p->c0, p->c1, p->n, p->sample_count, p->samples};
    GSHSCENE_Result r = check_leaf(&leaf, tally);
    if (r != GSHSCENE_OK) {
      return r;
    }
    expect_lo = p->hi;
  }
  if (expect_lo != 1.0) {
    return GSHSCENE_ERR_INVALID;
  }
  tally->pieces = t->piece_count;
  return GSHSCENE_OK;
}

/** MK1: validity first, then unsupported spaces, then the bounds; @p tally receives the transfer's counts. */
static GSHSCENE_Result check(const GSHSCENE_Store * store, const GSHSCENE_Mask * in, Tally * tally) {
  const GSHSCENE_Resource * picture = in->picture;
  if (!picture || picture->kind != GSHSCENE_RESOURCE_PICTURE || picture->store != store) {
    return GSHSCENE_ERR_INVALID;
  }
  if (in->mode == 0 || in->mode >= (uint32_t)GSHSCENE_MASK_MODE_COUNT ||
      in->coefficients >= (uint32_t)GSHSCENE_MASK_COEFF_COUNT || in->acts_on >= (uint32_t)GSHSCENE_MASK_ACTS_ON_COUNT) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Color_Space * spaces[3] = {&in->space, &in->render_space, &in->backdrop_space};
  int unsupported = 0;
  for (int i = 0; i < 3; i++) {
    GSHSCENE_Result r = gshscene_color_space_check(spaces[i]);
    if (r == GSHSCENE_ERR_INVALID) {
      return r;
    }
    unsupported |= r == GSHSCENE_ERR_UNSUPPORTED;
  }
  for (int i = 0; i < 3; i++) {
    if (!gshscene_finite(in->backdrop[i])) {
      return GSHSCENE_ERR_INVALID;
    }
  }
  if (in->mode == (uint32_t)GSHSCENE_MASK_ALPHA) {
    /* One spelling: alpha has no color, so a field that cannot matter must not split a handle. */
    if (in->coefficients != 0 || !gshscene_color_space_is_target(&in->space) ||
        !gshscene_color_space_is_target(&in->backdrop_space) || in->backdrop[0] != 0.0 || in->backdrop[1] != 0.0 ||
        in->backdrop[2] != 0.0) {
      return GSHSCENE_ERR_INVALID;
    }
  }
  memset(tally, 0, sizeof *tally);
  GSHSCENE_Result r = check_transfer(&in->transfer, tally);
  if (r != GSHSCENE_OK) {
    return r;
  }
  if (unsupported) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }
  if (!(in->backdrop[0] >= -GSHSCENE_MASK_BACKDROP_MAX && in->backdrop[0] <= GSHSCENE_MASK_BACKDROP_MAX &&
          in->backdrop[1] >= -GSHSCENE_MASK_BACKDROP_MAX && in->backdrop[1] <= GSHSCENE_MASK_BACKDROP_MAX &&
          in->backdrop[2] >= -GSHSCENE_MASK_BACKDROP_MAX && in->backdrop[2] <= GSHSCENE_MASK_BACKDROP_MAX) ||
      tally->limit || tally->pieces > GSHSCENE_MASK_MAX_PIECES ||
      tally->samples > GSHSCENE_MASK_MAX_SAMPLES) {
    return GSHSCENE_ERR_LIMIT;
  }
  return GSHSCENE_OK;
}

/* --- hash (MK13) ------------------------------------------------------------------------------ */

static void hash_leaf(GSHGEOM_Hash * h, const Leaf * leaf) {
  gshgeom_hash_u8(h, (uint8_t)leaf->kind);
  if (leaf->kind == GSHSCENE_TRANSFER_EXPONENTIAL) {
    gshgeom_hash_f64(h, pz(leaf->c0));
    gshgeom_hash_f64(h, pz(leaf->c1));
    gshgeom_hash_f64(h, pz(leaf->n));
  } else if (leaf->kind == GSHSCENE_TRANSFER_TABLE) {
    gshgeom_hash_u64(h, (uint64_t)leaf->count);
    for (size_t i = 0; i < leaf->count; i++) {
      gshgeom_hash_f64(h, pz(leaf->samples[i]));
    }
  }
}

static uint64_t content_hash(const GSHSCENE_Mask * m) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, MASK_FORMAT_VERSION);
  gshgeom_hash_u32(&h, (uint32_t)GSHSCENE_RESOURCE_MASK);
  gshgeom_hash_u64(&h, gshscene_resource_hash(m->picture));
  gshgeom_hash_u8(&h, (uint8_t)m->mode);
  gshgeom_hash_u8(&h, (uint8_t)m->coefficients);
  gshscene_space_hash(&h, &m->space);
  gshscene_space_hash(&h, &m->render_space);
  for (int i = 0; i < 3; i++) {
    gshgeom_hash_f64(&h, pz(m->backdrop[i]));
  }
  gshscene_space_hash(&h, &m->backdrop_space);
  gshgeom_hash_u8(&h, (uint8_t)m->acts_on);
  const GSHSCENE_Transfer * t = &m->transfer;
  if (t->kind == GSHSCENE_TRANSFER_STITCHING) {
    gshgeom_hash_u8(&h, (uint8_t)t->kind);
    gshgeom_hash_u64(&h, (uint64_t)t->piece_count);
    for (size_t i = 0; i < t->piece_count; i++) {
      const GSHSCENE_Transfer_Piece * p = &t->pieces[i];
      gshgeom_hash_f64(&h, pz(p->lo));
      gshgeom_hash_f64(&h, pz(p->hi));
      gshgeom_hash_f64(&h, pz(p->e0));
      gshgeom_hash_f64(&h, pz(p->e1));
      Leaf leaf = {p->leaf_kind, p->c0, p->c1, p->n, p->sample_count, p->samples};
      hash_leaf(&h, &leaf);
    }
  } else {
    Leaf leaf = {t->kind, t->c0, t->c1, t->n, t->sample_count, t->samples};
    hash_leaf(&h, &leaf);
  }
  return gshgeom_hash_value(&h);
}

/* --- equality, build, free ---------------------------------------------------------------------- */

/** Whether the stored arrays @p a equal @p count doubles of @p b once `-0` is `+0`. */
static int samples_match(const double * a, const double * b, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (!bits_equal(a[i], pz(b[i]))) {
      return 0;
    }
  }
  return 1;
}

int gshscene_mask_matches(const GSHSCENE_Resource * resource, const void * source) {
  const GSHSCENE_Mask * want = (const GSHSCENE_Mask *)source;
  const GSHSCENE_Mask * have = &resource->u.mask.m;
  if (have->picture != want->picture || have->mode != want->mode || have->coefficients != want->coefficients ||
      have->acts_on != want->acts_on || !gshscene_space_equal(&have->space, &want->space) ||
      !gshscene_space_equal(&have->render_space, &want->render_space) ||
      !gshscene_space_equal(&have->backdrop_space, &want->backdrop_space)) {
    return 0;
  }
  for (int i = 0; i < 3; i++) {
    if (!bits_equal(have->backdrop[i], pz(want->backdrop[i]))) {
      return 0;
    }
  }
  const GSHSCENE_Transfer * a = &have->transfer;
  const GSHSCENE_Transfer * b = &want->transfer;
  if (a->kind != b->kind || !bits_equal(a->c0, pz(b->c0)) || !bits_equal(a->c1, pz(b->c1)) ||
      !bits_equal(a->n, pz(b->n)) || a->sample_count != b->sample_count || a->piece_count != b->piece_count ||
      !samples_match(a->samples, b->samples, a->sample_count)) {
    return 0;
  }
  for (size_t i = 0; i < a->piece_count; i++) {
    const GSHSCENE_Transfer_Piece * p = &a->pieces[i];
    const GSHSCENE_Transfer_Piece * q = &b->pieces[i];
    if (!bits_equal(p->lo, pz(q->lo)) || !bits_equal(p->hi, pz(q->hi)) || !bits_equal(p->e0, pz(q->e0)) ||
        !bits_equal(p->e1, pz(q->e1)) || p->leaf_kind != q->leaf_kind || !bits_equal(p->c0, pz(q->c0)) ||
        !bits_equal(p->c1, pz(q->c1)) || !bits_equal(p->n, pz(q->n)) || p->sample_count != q->sample_count ||
        !samples_match(p->samples, q->samples, p->sample_count)) {
      return 0;
    }
  }
  return 1;
}

static GSHSCENE_Result build(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  const GSHSCENE_Mask * s = (const GSHSCENE_Mask *)source;
  const GSHSCENE_Transfer * t = &s->transfer;
  size_t samples = t->sample_count;
  for (size_t i = 0; i < t->piece_count; i++) {
    samples += t->pieces[i].sample_count;
  }
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_MASK);
  if (!resource) {
    return GSHSCENE_ERR_OOM;
  }
  GSHSCENE_Transfer_Piece * pieces = NULL;
  double * data = NULL;
  if (t->piece_count && !(pieces = gshscene_calloc(&store->alloc, t->piece_count, sizeof *pieces))) {
    gshscene_store_discard_resource(resource);
    return GSHSCENE_ERR_OOM;
  }
  if (samples && !(data = gshscene_calloc(&store->alloc, samples, sizeof *data))) {
    gshscene_free(&store->alloc, pieces);
    gshscene_store_discard_resource(resource);
    return GSHSCENE_ERR_OOM;
  }
  GSHSCENE_Mask * m = &resource->u.mask.m;
  *m = *s;
  m->picture = gshscene_resource_retain(s->picture);
  for (int i = 0; i < 3; i++) {
    m->backdrop[i] = pz(s->backdrop[i]);
  }
  size_t at = 0;
  m->transfer.c0 = pz(t->c0);
  m->transfer.c1 = pz(t->c1);
  m->transfer.n = pz(t->n);
  m->transfer.samples = NULL;
  if (t->sample_count) {
    for (size_t i = 0; i < t->sample_count; i++) {
      data[at + i] = pz(t->samples[i]);
    }
    m->transfer.samples = data;
    at += t->sample_count;
  }
  m->transfer.pieces = pieces;
  for (size_t i = 0; i < t->piece_count; i++) {
    pieces[i] = t->pieces[i];
    pieces[i].lo = pz(pieces[i].lo);
    pieces[i].hi = pz(pieces[i].hi);
    pieces[i].e0 = pz(pieces[i].e0);
    pieces[i].e1 = pz(pieces[i].e1);
    pieces[i].c0 = pz(pieces[i].c0);
    pieces[i].c1 = pz(pieces[i].c1);
    pieces[i].n = pz(pieces[i].n);
    pieces[i].samples = NULL;
    if (t->pieces[i].sample_count) {
      for (size_t j = 0; j < t->pieces[i].sample_count; j++) {
        data[at + j] = pz(t->pieces[i].samples[j]);
      }
      pieces[i].samples = data + at;
      at += t->pieces[i].sample_count;
    }
  }
  resource->u.mask.pieces = pieces;
  resource->u.mask.samples = data;
  *out = resource;
  return GSHSCENE_OK;
}

void gshscene_mask_free(GSHSCENE_Resource * resource) {
  gshscene_resource_release(resource->u.mask.m.picture);
  gshscene_free(&resource->store->alloc, resource->u.mask.pieces);
  gshscene_free(&resource->store->alloc, resource->u.mask.samples);
}

GSHSCENE_Result gshscene_store_intern_mask(
    GSHSCENE_Store * store, const GSHSCENE_Mask * mask, const GSHSCENE_Resource ** out) {
  if (!store || !mask || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  Tally tally;
  GSHSCENE_Result r = check(store, mask, &tally);
  if (r != GSHSCENE_OK) {
    return r;
  }
  size_t bytes = gshscene_sat_add(GSHSCENE_BYTES_BASE,
      gshscene_sat_add(gshscene_sat_mul(tally.pieces, GSHSCENE_BYTES_PER_TRANSFER_PIECE),
          gshscene_sat_mul(tally.samples, GSHSCENE_BYTES_PER_TRANSFER_SAMPLE)));
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_MASK, content_hash(mask), bytes, mask, build};
  return gshscene_store_intern(store, &probe, out);
}

GSHSCENE_Result gshscene_resource_mask(const GSHSCENE_Resource * resource, GSHSCENE_Mask * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_MASK) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = resource->u.mask.m;
  return GSHSCENE_OK;
}

/* --- the dump (MK13) ---------------------------------------------------------------------------- */

static int dump_samples(FILE * out, const double * samples, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (fprintf(out, "s %016llx\n", HEX(gshgeom_f64_bits(samples[i]))) < 0) {
      return -1;
    }
  }
  return 0;
}

int gshscene_mask_dump(const GSHSCENE_Resource * resource, FILE * out) {
  const GSHSCENE_Mask * m = &resource->u.mask.m;
  const GSHSCENE_Transfer * t = &m->transfer;
  if (fprintf(out, "mask picture=%016llx mode=%u coeff=%u acts=%u rspace=", HEX(gshscene_resource_hash(m->picture)),
          m->mode, m->coefficients, m->acts_on) < 0 ||
      gshscene_space_dump(out, &m->render_space) < 0 || fputs(" lspace=", out) == EOF ||
      gshscene_space_dump(out, &m->space) < 0 ||
      fprintf(out, " backdrop=%016llx %016llx %016llx bspace=", HEX(gshgeom_f64_bits(m->backdrop[0])),
          HEX(gshgeom_f64_bits(m->backdrop[1])), HEX(gshgeom_f64_bits(m->backdrop[2]))) < 0 ||
      gshscene_space_dump(out, &m->backdrop_space) < 0 || fputc('\n', out) == EOF) {
    return -1;
  }
  if (fprintf(out, "tr kind=%u c0=%016llx c1=%016llx n=%016llx samples=%llu pieces=%llu\n", t->kind,
          HEX(gshgeom_f64_bits(t->c0)), HEX(gshgeom_f64_bits(t->c1)), HEX(gshgeom_f64_bits(t->n)),
          HEX(t->sample_count), HEX(t->piece_count)) < 0 ||
      dump_samples(out, t->samples, t->sample_count) < 0) {
    return -1;
  }
  for (size_t i = 0; i < t->piece_count; i++) {
    const GSHSCENE_Transfer_Piece * p = &t->pieces[i];
    if (fprintf(out, "p lo=%016llx hi=%016llx e0=%016llx e1=%016llx leaf=%u c0=%016llx c1=%016llx n=%016llx samples=%llu\n",
            HEX(gshgeom_f64_bits(p->lo)), HEX(gshgeom_f64_bits(p->hi)), HEX(gshgeom_f64_bits(p->e0)),
            HEX(gshgeom_f64_bits(p->e1)), p->leaf_kind, HEX(gshgeom_f64_bits(p->c0)), HEX(gshgeom_f64_bits(p->c1)),
            HEX(gshgeom_f64_bits(p->n)), HEX(p->sample_count)) < 0 ||
        dump_samples(out, p->samples, p->sample_count) < 0) {
      return -1;
    }
  }
  return 0;
}

/* --- MK3: the coefficient sets ---------------------------------------------------------------- */

GSHSCENE_Result gshscene_mask_resolve(const GSHSCENE_Resource * mask, const GSHSCENE_Color_Space * enclosing,
    GSHSCENE_Mask_Resolved * out) {
  if (!mask || !out || mask->kind != GSHSCENE_RESOURCE_MASK) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Mask * m = &mask->u.mask.m;
  GSHSCENE_Mask_Resolved r;
  memset(&r, 0, sizeof r);
  int have_enclosing = enclosing && !gshscene_color_space_is_target(enclosing);
  int luminance = m->mode == (uint32_t)GSHSCENE_MASK_LUMINANCE;
  if (!gshscene_color_space_is_target(&m->render_space)) {
    r.render_space = m->render_space;
  } else if (have_enclosing) {
    r.render_space = *enclosing;
  } else if (luminance) {
    return GSHSCENE_ERR_INVALID;
  }
  if (!luminance) {
    *out = r; /* alpha: no color, no coefficients */
    return GSHSCENE_OK;
  }
  r.luminance_space = gshscene_color_space_is_target(&m->space) ? r.render_space : m->space;
  r.backdrop_space = gshscene_color_space_is_target(&m->backdrop_space) ? r.luminance_space : m->backdrop_space;
  switch (m->coefficients) {
    case GSHSCENE_MASK_COEFF_W3C:
      r.k[0] = 2125.0 / 10000.0;
      r.k[1] = 7154.0 / 10000.0;
      r.k[2] = 721.0 / 10000.0;
      break;
    case GSHSCENE_MASK_COEFF_REC709:
      r.k[0] = 2126.0 / 10000.0;
      r.k[1] = 7152.0 / 10000.0;
      r.k[2] = 722.0 / 10000.0;
      break;
    case GSHSCENE_MASK_COEFF_LUM:
      r.k[0] = 30.0 / 100.0;
      r.k[1] = 59.0 / 100.0;
      r.k[2] = 11.0 / 100.0;
      break;
    default:
      gshscene_space_luminance_row(&r.luminance_space, r.k);
      break;
  }
  *out = r;
  return GSHSCENE_OK;
}

/* --- MK4: the transfer -------------------------------------------------------------------------- */

static double eval_leaf(const Leaf * leaf, double x) {
  switch (leaf->kind) {
    case GSHSCENE_TRANSFER_EXPONENTIAL: {
      double power = leaf->n == 0.0 ? 1.0 : (x == 0.0 ? 0.0 : gmath_pow(x, leaf->n));
      return leaf->c0 + power * (leaf->c1 - leaf->c0);
    }
    case GSHSCENE_TRANSFER_TABLE: {
      size_t m = leaf->count;
      double u = x * (double)(m - 1);
      size_t i = (size_t)u;
      if (i > m - 2) {
        i = m - 2;
      }
      double t = u - (double)i;
      return leaf->samples[i] + t * (leaf->samples[i + 1] - leaf->samples[i]);
    }
    default:
      return x;
  }
}

double gshscene_transfer_eval(const GSHSCENE_Transfer * t, double x) {
  if (!t) {
    return clamp01(x);
  }
  x = clamp01(x);
  double y;
  if (t->kind == GSHSCENE_TRANSFER_STITCHING && t->piece_count) {
    /* The piece with lo <= x < hi, the last also taking x = 1: the first piece whose hi is above x, else the last. */
    size_t lo = 0;
    size_t hi = t->piece_count - 1;
    while (lo < hi) {
      size_t mid = lo + (hi - lo) / 2;
      if (x < t->pieces[mid].hi) {
        hi = mid;
      } else {
        lo = mid + 1;
      }
    }
    const GSHSCENE_Transfer_Piece * p = &t->pieces[lo];
    double xp = clamp01(p->e0 + ((x - p->lo) * (p->e1 - p->e0)) / (p->hi - p->lo));
    Leaf leaf = {p->leaf_kind, p->c0, p->c1, p->n, p->sample_count, p->samples};
    y = eval_leaf(&leaf, xp);
  } else {
    Leaf leaf = {t->kind, t->c0, t->c1, t->n, t->sample_count, t->samples};
    y = eval_leaf(&leaf, x);
  }
  return clamp01(y);
}

double gshscene_mask_transfer(const GSHSCENE_Resource * mask, double x) {
  if (!mask || mask->kind != GSHSCENE_RESOURCE_MASK) {
    return 0.0;
  }
  return gshscene_transfer_eval(&mask->u.mask.m.transfer, x);
}

/* --- MK9: what a mask needs of a sink ------------------------------------------------------------ */

uint32_t gshscene_mask_required(const GSHSCENE_Resource * mask) {
  if (!mask || mask->kind != GSHSCENE_RESOURCE_MASK) {
    return 0;
  }
  const GSHSCENE_Mask * m = &mask->u.mask.m;
  uint32_t need = 0;
  if (m->mode == (uint32_t)GSHSCENE_MASK_ALPHA) {
    need |= GSHSCENE_MASK_NATIVE_ALPHA;
  } else {
    need |= GSHSCENE_MASK_NATIVE_LUMINANCE;
    need |= m->coefficients == (uint32_t)GSHSCENE_MASK_COEFF_W3C ? GSHSCENE_MASK_NATIVE_COEFF_W3C
        : m->coefficients == (uint32_t)GSHSCENE_MASK_COEFF_REC709 ? GSHSCENE_MASK_NATIVE_COEFF_REC709
        : m->coefficients == (uint32_t)GSHSCENE_MASK_COEFF_LUM ? GSHSCENE_MASK_NATIVE_COEFF_LUM
                                                                : GSHSCENE_MASK_NATIVE_COEFF_SPACE_Y;
    if (m->backdrop[0] != 0.0 || m->backdrop[1] != 0.0 || m->backdrop[2] != 0.0) {
      need |= GSHSCENE_MASK_NATIVE_BACKDROP;
    }
  }
  switch (m->transfer.kind) {
    case GSHSCENE_TRANSFER_EXPONENTIAL:
      need |= GSHSCENE_MASK_NATIVE_TRANSFER_EXPONENTIAL;
      break;
    case GSHSCENE_TRANSFER_TABLE:
      need |= GSHSCENE_MASK_NATIVE_TRANSFER_TABLE;
      break;
    case GSHSCENE_TRANSFER_STITCHING:
      need |= GSHSCENE_MASK_NATIVE_TRANSFER_STITCHING;
      break;
    default:
      break;
  }
  if (!gshscene_color_space_is_target(&m->space) || !gshscene_color_space_is_target(&m->render_space) ||
      !gshscene_color_space_is_target(&m->backdrop_space)) {
    need |= GSHSCENE_MASK_NATIVE_SPACES;
  }
  if (m->acts_on == (uint32_t)GSHSCENE_MASK_ACTS_ON_SHAPE) {
    need |= GSHSCENE_MASK_NATIVE_ACTS_ON_SHAPE;
  }
  return need;
}
