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
 * Named primitives (primitive.h, PR1 to PR13): a rounded rectangle, an ellipse and a rectangle drawn the way Canvas draws one of negative
 * width, interned as tagged paths; the tag read back; the same geometry interned as a plain path, which is another resource with no tag;
 * and one list given to two test sinks: a writer-like one, whose dump carries the tag so that `<rect rx ry>`, `<ellipse>` or `re` can be
 * emitted, and a toolpath-like one, whose dump says for each arc what a G2 or G3 needs, a circle only where the map keeps it one.
 * It checks what it shows and exits non-zero when that does not hold.
 */

#include <ghoti.io/shape-scene/shape-scene.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int ok, const char * what) {
  if (!ok) {
    fprintf(stderr, "primitive: %s\n", what);
    failures++;
  }
}

typedef struct Text {
  char bytes[8192];
  size_t used;
} Text;

static GSHSCENE_Result keep(void * ctx, const void * bytes, size_t count) {
  Text * t = ctx;
  size_t room = sizeof t->bytes - 1 - t->used;
  size_t n = count < room ? count : room;
  memcpy(t->bytes + t->used, bytes, n);
  t->used += n;
  t->bytes[t->used] = '\0';
  return GSHSCENE_OK;
}

static void render(uint32_t records, const GSHSCENE_List * list, const char * name, Text * text) {
  GSHSCENE_Dump_Sink_Config config;
  gshscene_dump_sink_config_default(&config);
  config.targets = 1u << GSHSCENE_TARGET_STREAM;
  config.records = records;
  GSHSCENE_Sink * sink = NULL;
  expect(gshscene_dump_sink_create(NULL, NULL, &config, &sink) == GSHSCENE_OK, "the sink");
  GSHSCENE_Writer writer = {keep, text};
  GSHSCENE_Target target;
  memset(&target, 0, sizeof target);
  target.kind = GSHSCENE_TARGET_STREAM;
  target.surface = &writer;
  target.unit = GSHSCENE_UNIT_POINT;
  GSHSCENE_Report * report = NULL;
  gshscene_report_create(NULL, NULL, &report);
  GSHSCENE_Render_Request request;
  memset(&request, 0, sizeof request);
  request.list = list;
  request.target = &target;
  request.report = report;
  expect(gshscene_sink_render(sink, &request) == GSHSCENE_OK, "the render");
  printf("--- %s\n%s", name, text->bytes);
  gshscene_report_destroy(report);
  gshscene_sink_destroy(sink);
}

int main(void) {
  GSHSCENE_Store * store = NULL;
  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK) {
    return 1;
  }

  /* CSS `border-radius`, resolved by the front end: the waist takes radii that fit and does not clamp them. */
  GSHSCENE_Primitive card;
  memset(&card, 0, sizeof card);
  card.kind = GSHSCENE_PRIMITIVE_ROUNDED_RECT;
  card.x = 10;
  card.y = 10;
  card.w = 100;
  card.h = 60;
  for (int i = 0; i < 4; i++) {
    card.r[i].rx = 8;
    card.r[i].ry = 8;
  }
  /* Canvas rect(150, 10, -40, 30): a negative width is `flip_x`, a rectangle that starts at its top-right corner and turns the other way. */
  GSHSCENE_Primitive negative;
  memset(&negative, 0, sizeof negative);
  negative.kind = GSHSCENE_PRIMITIVE_RECT;
  negative.flip_x = 1;
  negative.x = 150 - 40;
  negative.y = 10;
  negative.w = 40;
  negative.h = 30;
  /* SVG <circle cx=60 cy=100 r=20>: an ellipse with equal radii. */
  GSHSCENE_Primitive dot;
  memset(&dot, 0, sizeof dot);
  dot.kind = GSHSCENE_PRIMITIVE_ELLIPSE;
  dot.cx = 60;
  dot.cy = 100;
  dot.rx = 20;
  dot.ry = 20;

  const GSHSCENE_Resource *rounded = NULL, *flipped = NULL, *circle = NULL;
  expect(gshscene_store_intern_primitive(store, &card, &rounded) == GSHSCENE_OK, "intern the card");
  expect(gshscene_store_intern_primitive(store, &negative, &flipped) == GSHSCENE_OK, "intern the negative rect");
  expect(gshscene_store_intern_primitive(store, &dot, &circle) == GSHSCENE_OK, "intern the circle");

  GSHSCENE_Primitive back;
  expect(gshscene_resource_primitive(rounded, &back) == GSHSCENE_OK && back.kind == GSHSCENE_PRIMITIVE_ROUNDED_RECT &&
             back.r[2].rx == 8.0,
      "the card's tag reads back");
  expect(gshscene_resource_primitive(flipped, &back) == GSHSCENE_OK && back.flip_x == 1 && back.flip_y == 0,
      "the negative rect's flips read back");
  printf("the card is %s with %zu elements; the negative rect is %s with flip_x %d\n", gshscene_primitive_kind_string(GSHSCENE_PRIMITIVE_ROUNDED_RECT),
      gshgeom_path_element_count(gshscene_resource_path(rounded)), gshscene_primitive_kind_string(back.kind), back.flip_x);

  /* The tag is not inferred: the circle's own geometry, interned as a path, is a path and another resource. */
  GSHGEOM_Path * copy = NULL;
  gshgeom_path_copy(gshscene_resource_path(circle), NULL, NULL, &copy);
  const GSHSCENE_Resource * plain = NULL;
  expect(gshscene_store_intern_path(store, copy, &plain) == GSHSCENE_OK, "intern the circle as a path");
  gshgeom_path_destroy(copy);
  expect(plain != circle, "a tagged path and its untagged copy are two resources");
  expect(gshscene_resource_primitive(plain, &back) == GSHSCENE_OK && back.kind == GSHSCENE_PRIMITIVE_NONE, "and the copy has no tag");

  /* Canvas roundRect(x + w, y, -w, h, radii): the radii go to the mirrored corners and the path runs the other way round. */
  card.flip_x = 1;
  const GSHSCENE_Resource * mirrored = NULL;
  expect(gshscene_store_intern_primitive(store, &card, &mirrored) == GSHSCENE_OK && mirrored != NULL, "a flipped rounded rect interns");
  expect(gshscene_resource_primitive(mirrored, &back) == GSHSCENE_OK && back.kind == GSHSCENE_PRIMITIVE_ROUNDED_RECT && back.flip_x == 1,
      "and keeps its flip");
  expect(mirrored != rounded, "and is not the unflipped card");
  gshscene_resource_release(mirrored);

  GSHSCENE_List * list = NULL;
  expect(gshscene_list_create(NULL, NULL, store, &list) == GSHSCENE_OK, "the list");
  GSHSCENE_Paint paint;
  memset(&paint, 0, sizeof paint);
  paint.kind = GSHSCENE_PAINT_SOLID;
  paint.rgba[3] = 1.0;
  GSHGEOM_Map mirror;
  gshgeom_map_scale(2, -1.0, 1.0, 1.0, &mirror);
  gshscene_list_fill(list, rounded, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0);
  gshscene_list_fill(list, flipped, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0);
  gshscene_list_fill(list, circle, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0);
  gshscene_list_fill(list, circle, &mirror, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0);
  gshscene_list_close(list);

  Text writer_text = {{0}, 0};
  Text pen_text = {{0}, 0};
  render(GSHSCENE_DUMP_RECORD_PRIMITIVES, list, "a writer-like sink: the tag rides with the fill", &writer_text);
  render(GSHSCENE_DUMP_RECORD_ARCS, list, "a toolpath-like sink: each arc, a circle where the map keeps one", &pen_text);
  expect(strstr(writer_text.bytes, "T prim=ellipse") != NULL && strstr(writer_text.bytes, "T prim=rounded-rect") != NULL &&
             strstr(writer_text.bytes, "T prim=rect flip_x=1") != NULL,
      "the writer-like dump names each tag and its flips");
  expect(strstr(writer_text.bytes, "\na ") == NULL, "and no arc lines");
  expect(strstr(pen_text.bytes, "circular=1") != NULL && strstr(pen_text.bytes, "positive=0") != NULL,
      "the toolpath-like dump has circles, and the mirrored one turns the other way (G3)");
  expect(strstr(pen_text.bytes, "\nT ") == NULL, "and no tags");

  gshscene_list_destroy(list);
  gshscene_resource_release(plain);
  gshscene_resource_release(circle);
  gshscene_resource_release(flipped);
  gshscene_resource_release(rounded);
  gshscene_store_destroy(store);
  if (failures) {
    fprintf(stderr, "primitive: FAILED\n");
  }
  return failures != 0;
}
