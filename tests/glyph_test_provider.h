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
 * The test glyph provider (story 14a): a provider with no font file behind it,
 * which every path of the seam (glyph_provider.h) and of the routes (glyph.h)
 * can be driven through, in C and in C++.  It is installed with the conformance
 * suite, so a sink library's tests (shape-cpu's) and probes use the same one.
 *
 * Its faces state the upem and flags its configuration gives; it has one axis.
 * Its glyphs (26.6 design units, y up):
 *
 * | id | what |
 * | --- | --- |
 * | 0 | no outline (a space) |
 * | 1 | a rectangle with every coordinate an odd number of units: a tie at 16 ppem of a 2048-unit face |
 * | 2 | a ring of quadratics, odd and fractional coordinates |
 * | 3 | a cubic shape with fractional coordinates, partly left of the origin and below the baseline |
 * | 4 | corrupt: every op fails with `ERR_CORRUPT` |
 * | 5 | its program fails: `PROGRAM_FAILED` under `FONT`, `ERR_REFUSED` under `FONT_STRICT` |
 * | 6 | hinted with errors |
 * | 7 | no instructions: `NO_PROGRAM` even on a face with a program |
 * | 8 | many: a zigzag of 3000 lines, or of `zigzag` when that is set (3000 is past a small scratch, such as the test sink's) |
 * | 9 and up | unavailable: `ERR_INVALID` |
 *
 * The axis moves every x of glyphs 1 and 2 by `c / 4` 26.6 units (`c` the
 * F2.14 coordinate, divided with a floor), so two instances differ.
 *
 * **The fast tier** is glyph_provider.h's rule exactly for unhinted points.
 * **Hinting** (mode `FONT` or `FONT_STRICT`, a face with the hinting flag): each
 * unhinted point of a glyph that hints is moved to the nearest whole pixel of
 * the origin's grid (`floor((v + 32) / 64) * 64` of the absolute 26.6 value,
 * minus the fraction), a hinter that changes what reaches the waist.  A face
 * without the flag answers `NO_PROGRAM` with unhinted points.
 *
 * It counts every call, so a gate can see which tier a sink took.
 */

#ifndef GHOTI_IO_GSHSCENE_TEST_GLYPH_PROVIDER_H
#define GHOTI_IO_GSHSCENE_TEST_GLYPH_PROVIDER_H

#include <ghoti.io/cutil/atomic.h>
#include <ghoti.io/shape-geometry/device.h>
#include <ghoti.io/shape-geometry/path.h>
#include <ghoti.io/shape-scene/glyph_provider.h>
#include <ghoti.io/shape-scene/resource.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The glyphs the provider has (ids 0 to 8). */
#define GSHSCENE_TP_GLYPHS 9u

/** The lines of glyph 8. */
#define GSHSCENE_TP_ZIGZAG 3000u

/** A test provider: its configuration and its counters.  Zero-fill, then set the configuration. */
typedef struct GSHSCENE_Test_Provider {
  uint32_t upem;      /**< The upem its faces state (0 is 2048). */
  uint32_t flags;     /**< Face flags (`GSHSCENE_FACE_HINTING`). */
  uint32_t max_ppem;  /**< What `face_info` states (0: the default). */
  int fail_info;      /**< Nonzero: `face_info` fails with `ERR_CORRUPT`. */
  uint32_t zigzag;    /**< The lines of glyph 8 (0: ::GSHSCENE_TP_ZIGZAG). */
  GCU_Atomic_Size info_calls;
  GCU_Atomic_Size outline_calls;  /**< The general route. */
  GCU_Atomic_Size fast_calls;     /**< The fast tier. */
  GCU_Atomic_Size box_calls;
  GCU_Atomic_Size retains;
  GCU_Atomic_Size releases;
  GCU_Atomic_Size last_hint_version; /**< The `hint_version` of the last fast-tier request. */
} GSHSCENE_Test_Provider;

/** One element of a test glyph: a verb (`M`, `L`, `Q`, `C`, `Z`) and its points, 26.6 design units. */
typedef struct GSHSCENE_TP_Element {
  char verb;
  int32_t p[6];
} GSHSCENE_TP_Element;

#define GSHSCENE_TP_U(v) ((int32_t)(v) * 64)

static const GSHSCENE_TP_Element gshscene_tp_rect[] = {
    {'M', {GSHSCENE_TP_U(3), GSHSCENE_TP_U(3), 0, 0, 0, 0}},
    {'L', {GSHSCENE_TP_U(1021), GSHSCENE_TP_U(3), 0, 0, 0, 0}},
    {'L', {GSHSCENE_TP_U(1021), GSHSCENE_TP_U(1501), 0, 0, 0, 0}},
    {'L', {GSHSCENE_TP_U(3), GSHSCENE_TP_U(1501), 0, 0, 0, 0}},
    {'Z', {0, 0, 0, 0, 0, 0}},
    {'M', {GSHSCENE_TP_U(301), GSHSCENE_TP_U(301), 0, 0, 0, 0}},
    {'L', {GSHSCENE_TP_U(301), GSHSCENE_TP_U(1203), 0, 0, 0, 0}},
    {'L', {GSHSCENE_TP_U(723), GSHSCENE_TP_U(1203), 0, 0, 0, 0}},
    {'L', {GSHSCENE_TP_U(723), GSHSCENE_TP_U(301), 0, 0, 0, 0}},
    {'Z', {0, 0, 0, 0, 0, 0}},
};

static const GSHSCENE_TP_Element gshscene_tp_ring[] = {
    {'M', {GSHSCENE_TP_U(1023), GSHSCENE_TP_U(0), 0, 0, 0, 0}},
    {'Q', {GSHSCENE_TP_U(1771), GSHSCENE_TP_U(1), GSHSCENE_TP_U(1771), GSHSCENE_TP_U(751), 0, 0}},
    {'Q', {GSHSCENE_TP_U(1771), GSHSCENE_TP_U(1501), GSHSCENE_TP_U(1023), GSHSCENE_TP_U(1501), 0, 0}},
    {'Q', {GSHSCENE_TP_U(275) + 33, GSHSCENE_TP_U(1501), GSHSCENE_TP_U(275), GSHSCENE_TP_U(751) - 7, 0, 0}},
    {'Q', {GSHSCENE_TP_U(275), GSHSCENE_TP_U(1), GSHSCENE_TP_U(1023), GSHSCENE_TP_U(0), 0, 0}},
    {'Z', {0, 0, 0, 0, 0, 0}},
    {'M', {GSHSCENE_TP_U(1023), GSHSCENE_TP_U(301), 0, 0, 0, 0}},
    {'Q', {GSHSCENE_TP_U(577), GSHSCENE_TP_U(301), GSHSCENE_TP_U(577), GSHSCENE_TP_U(751), 0, 0}},
    {'Q', {GSHSCENE_TP_U(577), GSHSCENE_TP_U(1201), GSHSCENE_TP_U(1023), GSHSCENE_TP_U(1201), 0, 0}},
    {'Q', {GSHSCENE_TP_U(1469), GSHSCENE_TP_U(1201), GSHSCENE_TP_U(1469), GSHSCENE_TP_U(751), 0, 0}},
    {'Q', {GSHSCENE_TP_U(1469), GSHSCENE_TP_U(301), GSHSCENE_TP_U(1023), GSHSCENE_TP_U(301), 0, 0}},
    {'Z', {0, 0, 0, 0, 0, 0}},
};

static const GSHSCENE_TP_Element gshscene_tp_curl[] = {
    {'M', {GSHSCENE_TP_U(-101) + 5, GSHSCENE_TP_U(-203) + 17, 0, 0, 0, 0}},
    {'C', {GSHSCENE_TP_U(400) + 1, GSHSCENE_TP_U(-480), GSHSCENE_TP_U(1301), GSHSCENE_TP_U(97) + 63,
              GSHSCENE_TP_U(1001), GSHSCENE_TP_U(905)}},
    {'C', {GSHSCENE_TP_U(803), GSHSCENE_TP_U(1499) + 31, GSHSCENE_TP_U(-37), GSHSCENE_TP_U(1201),
              GSHSCENE_TP_U(13) - 9, GSHSCENE_TP_U(611) + 3}},
    {'L', {GSHSCENE_TP_U(511), GSHSCENE_TP_U(609), 0, 0, 0, 0}},
    {'Z', {0, 0, 0, 0, 0, 0}},
};

/** A test glyph's outline: its elements and count, or NULL for none (glyph 0, glyph 8 and the failing ids). */
static inline const GSHSCENE_TP_Element * gshscene_tp_elements(uint32_t glyph, size_t * count) {
  switch (glyph) {
    case 1:
    case 5:
    case 6:
    case 7:
      *count = sizeof gshscene_tp_rect / sizeof gshscene_tp_rect[0];
      return gshscene_tp_rect;
    case 2:
      *count = sizeof gshscene_tp_ring / sizeof gshscene_tp_ring[0];
      return gshscene_tp_ring;
    case 3:
      *count = sizeof gshscene_tp_curl / sizeof gshscene_tp_curl[0];
      return gshscene_tp_curl;
    default:
      *count = 0;
      return NULL;
  }
}

/** Element @p i of glyph @p glyph (of @p count), from the tables or, for glyph 8, made: a move, the zigzag's lines, a close. */
static inline GSHSCENE_TP_Element gshscene_tp_element(const GSHSCENE_Test_Provider * tp, uint32_t glyph, size_t i,
    size_t * count) {
  GSHSCENE_TP_Element e;
  memset(&e, 0, sizeof e);
  if (glyph == 8) {
    size_t lines = tp->zigzag ? tp->zigzag : GSHSCENE_TP_ZIGZAG;
    *count = lines + 2;
    if (i == 0) {
      e.verb = 'M';
    } else if (i <= lines) {
      e.verb = 'L';
      e.p[0] = GSHSCENE_TP_U((int32_t)i);
      e.p[1] = GSHSCENE_TP_U((int32_t)(i % 2u) * 700);
    } else {
      e.verb = 'Z';
    }
    return e;
  }
  const GSHSCENE_TP_Element * table = gshscene_tp_elements(glyph, count);
  return table && i < *count ? table[i] : e;
}

/** The x shift of the face's instance for @p glyph: `floor(c / 4)` for glyphs 1 and 2, else 0. */
static inline int32_t gshscene_tp_shift(const GSHSCENE_Face * face, uint32_t glyph) {
  if ((glyph != 1 && glyph != 2) || face->coord_count == 0) {
    return 0;
  }
  int32_t c = face->coords[0];
  return c >= 0 ? c / 4 : -((-c + 3) / 4);
}

/** Map a shape-geometry result to the seam's. */
static inline GSHSCENE_Result gshscene_tp_result(GSHGEOM_Result r) {
  return r == GSHGEOM_OK ? GSHSCENE_OK : r == GSHGEOM_ERR_OOM ? GSHSCENE_ERR_OOM
      : r == GSHGEOM_ERR_LIMIT ? GSHSCENE_ERR_LIMIT : GSHSCENE_ERR_INVALID;
}

/** Glyphs 4 and 8 up fail before anything else; @return nonzero and the failure. */
static inline int gshscene_tp_fails(uint32_t glyph, GSHSCENE_Result * out) {
  if (glyph == 4) {
    *out = GSHSCENE_ERR_CORRUPT;
    return 1;
  }
  if (glyph >= GSHSCENE_TP_GLYPHS) {
    *out = GSHSCENE_ERR_INVALID;
    return 1;
  }
  return 0;
}

static inline GSHSCENE_Result gshscene_tp_face_info(void * ctx, const GSHSCENE_Face * face, GSHSCENE_Face_Info * out) {
  GSHSCENE_Test_Provider * tp = (GSHSCENE_Test_Provider *)ctx;
  (void)face;
  gcu_atomic_size_fetch_add(&tp->info_calls, 1);
  if (tp->fail_info) {
    return GSHSCENE_ERR_CORRUPT;
  }
  memset(out, 0, sizeof *out);
  out->upem = tp->upem ? tp->upem : 2048u;
  out->axis_count = 1;
  out->flags = tp->flags;
  out->max_ppem = tp->max_ppem;
  out->glyph_count = GSHSCENE_TP_GLYPHS;
  out->family_name = "Ghoti Test";
  out->postscript_name = "GhotiTest-Regular";
  return GSHSCENE_OK;
}

static inline GSHSCENE_Result gshscene_tp_outline(void * ctx, const GSHSCENE_Face * face, uint32_t glyph, GSHGEOM_Path * out) {
  GSHSCENE_Test_Provider * tp = (GSHSCENE_Test_Provider *)ctx;
  gcu_atomic_size_fetch_add(&tp->outline_calls, 1);
  GSHSCENE_Result failed;
  if (gshscene_tp_fails(glyph, &failed)) {
    return failed;
  }
  size_t n = 0;
  gshscene_tp_element(tp, glyph, 0, &n);
  int32_t dx = gshscene_tp_shift(face, glyph);
  const unsigned b = GSHSCENE_GLYPH_OUTLINE_BITS;
  for (size_t i = 0; i < n; i++) {
    GSHSCENE_TP_Element e = gshscene_tp_element(tp, glyph, i, &n);
    const int32_t * p = e.p;
    GSHGEOM_Result r = GSHGEOM_OK;
    switch (e.verb) {
      case 'M':
        r = gshgeom_path_move_to_fixed(out, p[0] + dx, p[1], b);
        break;
      case 'L':
        r = gshgeom_path_line_to_fixed(out, p[0] + dx, p[1], b);
        break;
      case 'Q':
        r = gshgeom_path_quadratic_to_fixed(out, p[0] + dx, p[1], p[2] + dx, p[3], b);
        break;
      case 'C':
        r = gshgeom_path_cubic_to_fixed(out, p[0] + dx, p[1], p[2] + dx, p[3], p[4] + dx, p[5], b);
        break;
      default:
        r = gshgeom_path_close(out);
        break;
    }
    if (r != GSHGEOM_OK) {
      return gshscene_tp_result(r);
    }
  }
  return GSHSCENE_OK;
}

/** One point of the fast tier: unhinted by the seam's rule, then, when hinting, moved to the nearest whole pixel. */
static inline GSHGEOM_Result gshscene_tp_point(const GSHSCENE_Glyph_Request * rq, int hint, int32_t x, int32_t y,
    GSHGEOM_Device_Point * out) {
  int32_t sx = 0;
  int32_t sy = 0;
  GSHGEOM_Result r = gshgeom_scale_26_6(x, rq->scale, 0, &sx);
  if (r == GSHGEOM_OK) {
    r = gshgeom_scale_26_6(y, rq->scale, 1, &sy);
  }
  if (r != GSHGEOM_OK) {
    return r;
  }
  int64_t ax = (int64_t)rq->frac_x + sx;
  int64_t ay = (int64_t)rq->frac_y + sy;
  if (hint) {
    /* floor((v + 32) / 64) * 64 for either sign, without a shift of a negative number. */
    int64_t vx = ax + 32;
    int64_t vy = ay + 32;
    ax = vx - ((vx % 64 + 64) % 64);
    ay = vy - ((vy % 64 + 64) % 64);
  }
  if (ax < INT32_MIN || ax > INT32_MAX || ay < INT32_MIN || ay > INT32_MAX) {
    return GSHGEOM_ERR_LIMIT;
  }
  out->x = (int32_t)ax;
  out->y = (int32_t)ay;
  return GSHGEOM_OK;
}

static inline GSHSCENE_Result gshscene_tp_device_path(void * ctx, const GSHSCENE_Face * face,
    const GSHSCENE_Glyph_Request * rq, GSHGEOM_Device_Path * out, GSHSCENE_Glyph_Answer * answer) {
  GSHSCENE_Test_Provider * tp = (GSHSCENE_Test_Provider *)ctx;
  gcu_atomic_size_fetch_add(&tp->fast_calls, 1);
  gcu_atomic_size_store(&tp->last_hint_version, rq->hint_version);
  GSHSCENE_Result failed;
  if (gshscene_tp_fails(rq->glyph, &failed)) {
    return failed;
  }
  /* The request's scale is the seam's snapped scale of its size, or the request is not the seam's. */
  int32_t scale = 0;
  if (gshscene_glyph_scale(rq->ppem, tp->upem ? tp->upem : 2048u, &scale) != GSHSCENE_OK || rq->scale != scale) {
    return GSHSCENE_ERR_INVALID;
  }
  int asks = rq->hint_mode != GSHSCENE_HINT_NONE;
  int strict = rq->hint_mode == GSHSCENE_HINT_FONT_STRICT;
  int has_program = (tp->flags & GSHSCENE_FACE_HINTING) != 0;
  uint32_t status = GSHSCENE_HINT_STATUS_HINTED;
  int hint = 0;
  if (!asks) {
    status = GSHSCENE_HINT_STATUS_NO_PROGRAM;
  } else if (!has_program || rq->glyph == 7) {
    status = GSHSCENE_HINT_STATUS_NO_PROGRAM;
  } else if (rq->glyph == 5) {
    if (strict) {
      return GSHSCENE_ERR_REFUSED;
    }
    status = GSHSCENE_HINT_STATUS_PROGRAM_FAILED;
  } else {
    hint = 1;
    status = rq->glyph == 6 ? GSHSCENE_HINT_STATUS_WITH_ERRORS : GSHSCENE_HINT_STATUS_HINTED;
  }
  size_t n = 0;
  gshscene_tp_element(tp, rq->glyph, 0, &n);
  int32_t dx = gshscene_tp_shift(face, rq->glyph);
  for (size_t i = 0; i < n; i++) {
    GSHSCENE_TP_Element e = gshscene_tp_element(tp, rq->glyph, i, &n);
    const int32_t * p = e.p;
    GSHGEOM_Device_Point d[3];
    GSHGEOM_Result r = GSHGEOM_OK;
    int k = e.verb == 'M' || e.verb == 'L' ? 1 : e.verb == 'Q' ? 2 : e.verb == 'C' ? 3 : 0;
    for (int j = 0; j < k && r == GSHGEOM_OK; j++) {
      r = gshscene_tp_point(rq, hint, p[2 * j] + dx, p[2 * j + 1], &d[j]);
    }
    if (r == GSHGEOM_OK) {
      switch (e.verb) {
        case 'M':
          r = gshgeom_device_path_move_to(out, d[0]);
          break;
        case 'L':
          r = gshgeom_device_path_line_to(out, d[0]);
          break;
        case 'Q':
          r = gshgeom_device_path_quadratic_to(out, d[0], d[1]);
          break;
        case 'C':
          r = gshgeom_device_path_cubic_to(out, d[0], d[1], d[2]);
          break;
        default:
          r = gshgeom_device_path_close(out);
          break;
      }
    }
    if (r != GSHGEOM_OK) {
      return gshscene_tp_result(r);
    }
  }
  answer->status = status;
  /* The advance: the rectangle's width, scaled (hinted to whole pixels when hinted). */
  int32_t adv = 0;
  gshgeom_scale_26_6(GSHSCENE_TP_U(1024), rq->scale, 0, &adv);
  answer->advance = hint ? (adv + 32) / 64 * 64 : adv;
  return GSHSCENE_OK;
}

static inline GSHSCENE_Result gshscene_tp_box(void * ctx, const GSHSCENE_Face * face, uint32_t glyph, GSHSCENE_Glyph_Box * out) {
  GSHSCENE_Test_Provider * tp = (GSHSCENE_Test_Provider *)ctx;
  gcu_atomic_size_fetch_add(&tp->box_calls, 1);
  GSHSCENE_Result failed;
  if (gshscene_tp_fails(glyph, &failed)) {
    return failed;
  }
  size_t n = 0;
  gshscene_tp_element(tp, glyph, 0, &n);
  int32_t dx = gshscene_tp_shift(face, glyph);
  memset(out, 0, sizeof *out);
  out->empty = 1;
  for (size_t i = 0; i < n; i++) {
    GSHSCENE_TP_Element e = gshscene_tp_element(tp, glyph, i, &n);
    int k = e.verb == 'M' || e.verb == 'L' ? 1 : e.verb == 'Q' ? 2 : e.verb == 'C' ? 3 : 0;
    for (int j = 0; j < k; j++) {
      int32_t x = e.p[2 * j] + dx;
      int32_t y = e.p[2 * j + 1];
      if (out->empty) {
        out->x_min = out->x_max = x;
        out->y_min = out->y_max = y;
        out->empty = 0;
      } else {
        out->x_min = x < out->x_min ? x : out->x_min;
        out->x_max = x > out->x_max ? x : out->x_max;
        out->y_min = y < out->y_min ? y : out->y_min;
        out->y_max = y > out->y_max ? y : out->y_max;
      }
    }
  }
  return GSHSCENE_OK;
}

static inline GSHSCENE_Result gshscene_tp_face_data(void * ctx, const GSHSCENE_Face * face, const uint8_t ** data, size_t * size) {
  static const uint8_t bytes[4] = {0x00, 0x01, 0x00, 0x00}; /* an sfnt version and nothing else: a test */
  (void)ctx;
  (void)face;
  *data = bytes;
  *size = sizeof bytes;
  return GSHSCENE_OK;
}

static inline void gshscene_tp_retain(void * ctx) {
  gcu_atomic_size_fetch_add(&((GSHSCENE_Test_Provider *)ctx)->retains, 1);
}

static inline void gshscene_tp_release(void * ctx) {
  gcu_atomic_size_fetch_add(&((GSHSCENE_Test_Provider *)ctx)->releases, 1);
}

/** The provider's ops: with the fast tier (@p fast nonzero) or without it. */
static inline const GSHSCENE_Glyph_Provider * gshscene_tp_ops(int fast) {
  static const GSHSCENE_Glyph_Provider with = {GSHSCENE_GLYPH_PROVIDER_VERSION, gshscene_tp_face_info,
      gshscene_tp_outline, gshscene_tp_device_path, gshscene_tp_box, gshscene_tp_face_data, gshscene_tp_retain,
      gshscene_tp_release};
  static const GSHSCENE_Glyph_Provider without = {GSHSCENE_GLYPH_PROVIDER_VERSION, gshscene_tp_face_info,
      gshscene_tp_outline, NULL, gshscene_tp_box, NULL, gshscene_tp_retain, gshscene_tp_release};
  return fast ? &with : &without;
}

/** Zero the counters (the configuration is kept). */
static inline void gshscene_tp_reset(GSHSCENE_Test_Provider * tp) {
  gcu_atomic_size_init(&tp->info_calls, 0);
  gcu_atomic_size_init(&tp->outline_calls, 0);
  gcu_atomic_size_init(&tp->fast_calls, 0);
  gcu_atomic_size_init(&tp->box_calls, 0);
  gcu_atomic_size_init(&tp->retains, 0);
  gcu_atomic_size_init(&tp->releases, 0);
  gcu_atomic_size_init(&tp->last_hint_version, 0);
}

/** Intern a face of @p tp (key, index and one coordinate), bound with the fast tier or not; NULL on failure. */
static inline const GSHSCENE_Resource * gshscene_tp_face(
    GSHSCENE_Store * store, GSHSCENE_Test_Provider * tp, int fast, uint64_t key, int16_t coord) {
  GSHSCENE_Face_Desc desc;
  memset(&desc, 0, sizeof desc);
  desc.font_key = key;
  desc.coord_count = coord ? 1u : 0u;
  desc.coords = coord ? &coord : NULL;
  desc.provider = gshscene_tp_ops(fast);
  desc.ctx = tp;
  const GSHSCENE_Resource * face = NULL;
  return gshscene_store_intern_face(store, &desc, &face) == GSHSCENE_OK ? face : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_TEST_GLYPH_PROVIDER_H */
