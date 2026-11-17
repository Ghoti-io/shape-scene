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
 * The linetype resource (stroke.h, LT1 to LT3): a name, a description and elements, each a dash, a space, a dot, a text or a
 * shape, which a stroke style names for its dash pattern.  Its validity, bounds, canonical form (a `-0` is a `+0`, the strings
 * copied into one pool), equality, hash, dump and reading back.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <ghoti.io/shape-geometry/stroke.h>

#include <string.h>

/** The format of a linetype's hash and dump. */
#define LINETYPE_FORMAT_VERSION 1u

/** The longest string, in bytes. */
#define LINETYPE_STRING_MAX ((size_t)255)

#define HEX(x) ((unsigned long long)(x))

static double canonical_zero(double x) {
  return x + 0.0;
}

static int within(double x) {
  return x == 0.0 || (x >= GSHSCENE_STROKE_VALUE_MIN && x <= GSHSCENE_STROKE_VALUE_MAX) ||
      (x <= -GSHSCENE_STROKE_VALUE_MIN && x >= -GSHSCENE_STROKE_VALUE_MAX);
}

static int is_complex(uint32_t kind) {
  return kind == (uint32_t)GSHSCENE_LINETYPE_TEXT || kind == (uint32_t)GSHSCENE_LINETYPE_SHAPE;
}

/** A string is usable when it has no control character (a DXF value cannot span lines). */
static int clean(const char * s) {
  for (const unsigned char * p = (const unsigned char *)s; *p; p++) {
    if (*p < 0x20 || *p == 0x7f) {
      return 0;
    }
  }
  return 1;
}

static const char * str_or_empty(const char * s) {
  return s ? s : "";
}

static void hash_string(GSHGEOM_Hash * h, const char * s) {
  size_t n = s ? strlen(s) : 0;
  gshgeom_hash_u8(h, s ? 1 : 0);
  gshgeom_hash_u64(h, (uint64_t)n);
  for (size_t i = 0; i < n; i++) {
    gshgeom_hash_u8(h, (uint8_t)s[i]);
  }
}

static uint64_t content_hash(const GSHSCENE_Linetype * lt) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, LINETYPE_FORMAT_VERSION);
  gshgeom_hash_u32(&h, (uint32_t)GSHSCENE_RESOURCE_LINETYPE);
  hash_string(&h, lt->name);
  hash_string(&h, lt->description);
  gshgeom_hash_u64(&h, (uint64_t)lt->element_count);
  for (size_t i = 0; i < lt->element_count; i++) {
    const GSHSCENE_Linetype_Element * e = &lt->elements[i];
    gshgeom_hash_u8(&h, (uint8_t)e->kind);
    gshgeom_hash_f64(&h, canonical_zero(e->length));
    hash_string(&h, e->text);
    hash_string(&h, e->style);
    gshgeom_hash_f64(&h, canonical_zero(e->scale));
    gshgeom_hash_f64(&h, canonical_zero(e->rotation));
    gshgeom_hash_u8(&h, (uint8_t)e->rotation_mode);
    gshgeom_hash_f64(&h, canonical_zero(e->offset_x));
    gshgeom_hash_f64(&h, canonical_zero(e->offset_y));
  }
  return gshgeom_hash_value(&h);
}

static int same_string(const char * a, const char * b) {
  if (!a || !b) {
    return a == b;
  }
  return strcmp(a, b) == 0;
}

static int same_bits(double a, double b) {
  return gshgeom_f64_bits(a) == gshgeom_f64_bits(canonical_zero(b));
}

int gshscene_linetype_matches(const GSHSCENE_Resource * resource, const void * source) {
  const GSHSCENE_Linetype * want = (const GSHSCENE_Linetype *)source;
  const GSHSCENE_Resource * r = resource;
  if (r->u.linetype.element_count != want->element_count || !same_string(r->u.linetype.name, want->name) ||
      !same_string(r->u.linetype.description, want->description)) {
    return 0;
  }
  for (size_t i = 0; i < want->element_count; i++) {
    const GSHSCENE_Linetype_Element * a = &r->u.linetype.elements[i];
    const GSHSCENE_Linetype_Element * b = &want->elements[i];
    if (a->kind != b->kind || !same_bits(a->length, b->length) || !same_string(a->text, b->text) ||
        !same_string(a->style, b->style) || !same_bits(a->scale, b->scale) || !same_bits(a->rotation, b->rotation) ||
        a->rotation_mode != b->rotation_mode || !same_bits(a->offset_x, b->offset_x) || !same_bits(a->offset_y, b->offset_y)) {
      return 0;
    }
  }
  return 1;
}

static size_t string_bytes(const char * s) {
  return s ? strlen(s) + 1 : 0;
}

static char * pool_put(char ** cursor, const char * s) {
  if (!s) {
    return NULL;
  }
  size_t n = strlen(s) + 1;
  char * at = *cursor;
  memcpy(at, s, n);
  *cursor += n;
  return at;
}

static GSHSCENE_Result build(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  const GSHSCENE_Linetype * lt = (const GSHSCENE_Linetype *)source;
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_LINETYPE);
  if (!resource) {
    return GSHSCENE_ERR_OOM;
  }
  size_t pool = string_bytes(lt->name) + string_bytes(lt->description);
  for (size_t i = 0; i < lt->element_count; i++) {
    pool += string_bytes(lt->elements[i].text) + string_bytes(lt->elements[i].style);
  }
  resource->u.linetype.elements = gshscene_alloc(&store->alloc, lt->element_count * sizeof *resource->u.linetype.elements);
  resource->u.linetype.pool = gshscene_alloc(&store->alloc, pool ? pool : 1);
  if (!resource->u.linetype.elements || !resource->u.linetype.pool) {
    gshscene_free(&store->alloc, resource->u.linetype.elements);
    gshscene_free(&store->alloc, resource->u.linetype.pool);
    resource->u.linetype.elements = NULL;
    resource->u.linetype.pool = NULL;
    gshscene_store_discard_resource(resource);
    return GSHSCENE_ERR_OOM;
  }
  char * cursor = resource->u.linetype.pool;
  resource->u.linetype.name = pool_put(&cursor, lt->name);
  resource->u.linetype.description = pool_put(&cursor, lt->description);
  resource->u.linetype.element_count = lt->element_count;
  for (size_t i = 0; i < lt->element_count; i++) {
    GSHSCENE_Linetype_Element * e = &resource->u.linetype.elements[i];
    *e = lt->elements[i];
    e->length = canonical_zero(e->length);
    e->scale = canonical_zero(e->scale);
    e->rotation = canonical_zero(e->rotation);
    e->offset_x = canonical_zero(e->offset_x);
    e->offset_y = canonical_zero(e->offset_y);
    e->text = pool_put(&cursor, lt->elements[i].text);
    e->style = pool_put(&cursor, lt->elements[i].style);
    if (is_complex(e->kind)) {
      resource->u.linetype.complex = 1;
    }
  }
  *out = resource;
  return GSHSCENE_OK;
}

void gshscene_linetype_free(GSHSCENE_Resource * resource) {
  gshscene_free(&resource->store->alloc, resource->u.linetype.elements);
  gshscene_free(&resource->store->alloc, resource->u.linetype.pool);
}

/** LT2's validity of one element, in the order the header states. */
static int element_valid(const GSHSCENE_Linetype_Element * e) {
  if (e->kind >= (uint32_t)GSHSCENE_LINETYPE_KIND_COUNT || e->rotation_mode >= (uint32_t)GSHSCENE_LINETYPE_ROTATE_COUNT) {
    return 0;
  }
  if (!gshscene_finite(e->length) || !gshscene_finite(e->scale) || !gshscene_finite(e->rotation) ||
      !gshscene_finite(e->offset_x) || !gshscene_finite(e->offset_y)) {
    return 0;
  }
  int simple_length = e->kind == (uint32_t)GSHSCENE_LINETYPE_DASH || e->kind == (uint32_t)GSHSCENE_LINETYPE_SPACE;
  if (simple_length ? !(e->length > 0.0) : e->length != 0.0) {
    return 0;
  }
  if (is_complex(e->kind)) {
    if (!e->text || !e->text[0] || !e->style || !e->style[0] || !(e->scale > 0.0) || !clean(e->text) || !clean(e->style)) {
      return 0;
    }
  } else if (e->text || e->style || e->scale != 0.0 || e->rotation != 0.0 || e->rotation_mode != 0 || e->offset_x != 0.0 ||
      e->offset_y != 0.0) {
    return 0;
  }
  return 1;
}

GSHSCENE_Result gshscene_store_intern_linetype(
    GSHSCENE_Store * store, const GSHSCENE_Linetype * lt, const GSHSCENE_Resource ** out) {
  if (!store || !lt || !out || !lt->name || !lt->name[0] || !clean(lt->name) || (lt->description && !clean(lt->description)) ||
      lt->element_count == 0 || !lt->elements) {
    return GSHSCENE_ERR_INVALID;
  }
  int dash = 0;
  for (size_t i = 0; i < lt->element_count; i++) {
    if (!element_valid(&lt->elements[i])) {
      return GSHSCENE_ERR_INVALID;
    }
    dash = dash || lt->elements[i].kind == (uint32_t)GSHSCENE_LINETYPE_DASH;
  }
  if (!dash) {
    return GSHSCENE_ERR_INVALID;
  }
  /* LT3: the bounds, after every validity check. */
  const size_t most = store->limits.max_resource_elements < GSHGEOM_STROKE_DASHES_MAX ? store->limits.max_resource_elements
                                                                                      : GSHGEOM_STROKE_DASHES_MAX;
  if (lt->element_count > most || strlen(lt->name) > LINETYPE_STRING_MAX ||
      (lt->description && strlen(lt->description) > LINETYPE_STRING_MAX)) {
    return GSHSCENE_ERR_LIMIT;
  }
  size_t bytes = GSHSCENE_BYTES_BASE + string_bytes(lt->name) + string_bytes(lt->description);
  for (size_t i = 0; i < lt->element_count; i++) {
    const GSHSCENE_Linetype_Element * e = &lt->elements[i];
    if ((e->text && strlen(e->text) > LINETYPE_STRING_MAX) || (e->style && strlen(e->style) > LINETYPE_STRING_MAX) ||
        !within(e->length) || !within(e->scale) || !within(e->offset_x) || !within(e->offset_y) ||
        e->rotation > 1099511627776.0 || e->rotation < -1099511627776.0) {
      return GSHSCENE_ERR_LIMIT;
    }
    bytes = gshscene_sat_add(bytes, gshscene_sat_add(GSHSCENE_BYTES_PER_LINETYPE_ELEMENT,
        gshscene_sat_add(string_bytes(e->text), string_bytes(e->style))));
  }
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_LINETYPE, content_hash(lt), bytes, lt, build};
  return gshscene_store_intern(store, &probe, out);
}

GSHSCENE_Result gshscene_resource_linetype(const GSHSCENE_Resource * resource, GSHSCENE_Linetype * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_LINETYPE) {
    return GSHSCENE_ERR_INVALID;
  }
  out->name = resource->u.linetype.name;
  out->description = resource->u.linetype.description;
  out->element_count = resource->u.linetype.element_count;
  out->elements = resource->u.linetype.elements;
  return GSHSCENE_OK;
}

int gshscene_linetype_dump(const GSHSCENE_Resource * resource, FILE * out) {
  const GSHSCENE_Resource * r = resource;
  if (fprintf(out, "linetype name=%s description=%s elements=%llu\n", r->u.linetype.name,
          str_or_empty(r->u.linetype.description), HEX(r->u.linetype.element_count)) < 0) {
    return -1;
  }
  for (size_t i = 0; i < r->u.linetype.element_count; i++) {
    const GSHSCENE_Linetype_Element * e = &r->u.linetype.elements[i];
    if (fprintf(out, "e kind=%u length=%016llx text=%s style=%s scale=%016llx rotation=%016llx mode=%u x=%016llx y=%016llx\n",
            (unsigned)e->kind, HEX(gshgeom_f64_bits(e->length)), str_or_empty(e->text), str_or_empty(e->style),
            HEX(gshgeom_f64_bits(e->scale)), HEX(gshgeom_f64_bits(e->rotation)), (unsigned)e->rotation_mode,
            HEX(gshgeom_f64_bits(e->offset_x)), HEX(gshgeom_f64_bits(e->offset_y))) < 0) {
      return -1;
    }
  }
  return 0;
}
