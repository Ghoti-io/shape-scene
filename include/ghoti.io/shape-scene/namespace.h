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
 * @file namespace.h
 *
 * Maps every public name of this library into its version namespace.
 *
 * Kept in one file rather than beside each declaration: a type rename has to
 * be in effect before any struct tag that uses the name, and an internal
 * header may define such a tag without including the public header that
 * declares the typedef.
 *
 * `make check-symbols` fails if an exported symbol is missing from this list.
 *
 * See CONVENTIONS.md section 4.
 */

#ifndef GHOTI_IO_GSHSCENE_NAMESPACE_H
#define GHOTI_IO_GSHSCENE_NAMESPACE_H

#include <ghoti.io/shape-scene/libver.h>

/// @cond HIDDEN_SYMBOLS

/* Public types. GSHGEOM_* names are shape-geometry's; it has already renamed them. */
#define GSHSCENE_Allocator GHOTIIO_SHAPE_SCENE(GSHSCENE_Allocator)
#define GSHSCENE_Glyph GHOTIIO_SHAPE_SCENE(GSHSCENE_Glyph)
#define GSHSCENE_Glyph_Run GHOTIIO_SHAPE_SCENE(GSHSCENE_Glyph_Run)
#define GSHSCENE_Glyph_Source GHOTIIO_SHAPE_SCENE(GSHSCENE_Glyph_Source)
#define GSHSCENE_Group GHOTIIO_SHAPE_SCENE(GSHSCENE_Group)
#define GSHSCENE_Index_Hash_Fn GHOTIIO_SHAPE_SCENE(GSHSCENE_Index_Hash_Fn)
#define GSHSCENE_Item GHOTIIO_SHAPE_SCENE(GSHSCENE_Item)
#define GSHSCENE_Item_Kind GHOTIIO_SHAPE_SCENE(GSHSCENE_Item_Kind)
#define GSHSCENE_Limits GHOTIIO_SHAPE_SCENE(GSHSCENE_Limits)
#define GSHSCENE_List GHOTIIO_SHAPE_SCENE(GSHSCENE_List)
#define GSHSCENE_Ordering GHOTIIO_SHAPE_SCENE(GSHSCENE_Ordering)
#define GSHSCENE_Paint GHOTIIO_SHAPE_SCENE(GSHSCENE_Paint)
#define GSHSCENE_Paint_Kind GHOTIIO_SHAPE_SCENE(GSHSCENE_Paint_Kind)
#define GSHSCENE_Release_Fn GHOTIIO_SHAPE_SCENE(GSHSCENE_Release_Fn)
#define GSHSCENE_Resource GHOTIIO_SHAPE_SCENE(GSHSCENE_Resource)
#define GSHSCENE_Resource_Kind GHOTIIO_SHAPE_SCENE(GSHSCENE_Resource_Kind)
#define GSHSCENE_Result GHOTIIO_SHAPE_SCENE(GSHSCENE_Result)
#define GSHSCENE_Store GHOTIIO_SHAPE_SCENE(GSHSCENE_Store)
#define GSHSCENE_Store_Stats GHOTIIO_SHAPE_SCENE(GSHSCENE_Store_Stats)

/* Public functions. */
#define gshscene_allocator_default GHOTIIO_SHAPE_SCENE(gshscene_allocator_default)
#define gshscene_limits_default GHOTIIO_SHAPE_SCENE(gshscene_limits_default)
#define gshscene_list_bytes GHOTIIO_SHAPE_SCENE(gshscene_list_bytes)
#define gshscene_list_clip_pop GHOTIIO_SHAPE_SCENE(gshscene_list_clip_pop)
#define gshscene_list_clip_push GHOTIIO_SHAPE_SCENE(gshscene_list_clip_push)
#define gshscene_list_close GHOTIIO_SHAPE_SCENE(gshscene_list_close)
#define gshscene_list_count GHOTIIO_SHAPE_SCENE(gshscene_list_count)
#define gshscene_list_create GHOTIIO_SHAPE_SCENE(gshscene_list_create)
#define gshscene_list_destroy GHOTIIO_SHAPE_SCENE(gshscene_list_destroy)
#define gshscene_list_dump GHOTIIO_SHAPE_SCENE(gshscene_list_dump)
#define gshscene_list_equal GHOTIIO_SHAPE_SCENE(gshscene_list_equal)
#define gshscene_list_expansion GHOTIIO_SHAPE_SCENE(gshscene_list_expansion)
#define gshscene_list_fill GHOTIIO_SHAPE_SCENE(gshscene_list_fill)
#define gshscene_list_group_pop GHOTIIO_SHAPE_SCENE(gshscene_list_group_pop)
#define gshscene_list_group_push GHOTIIO_SHAPE_SCENE(gshscene_list_group_push)
#define gshscene_list_hash GHOTIIO_SHAPE_SCENE(gshscene_list_hash)
#define gshscene_list_instance GHOTIIO_SHAPE_SCENE(gshscene_list_instance)
#define gshscene_list_is_closed GHOTIIO_SHAPE_SCENE(gshscene_list_is_closed)
#define gshscene_list_item GHOTIIO_SHAPE_SCENE(gshscene_list_item)
#define gshscene_list_reset GHOTIIO_SHAPE_SCENE(gshscene_list_reset)
#define gshscene_resource_dump GHOTIIO_SHAPE_SCENE(gshscene_resource_dump)
#define gshscene_resource_glyph_run GHOTIIO_SHAPE_SCENE(gshscene_resource_glyph_run)
#define gshscene_resource_hash GHOTIIO_SHAPE_SCENE(gshscene_resource_hash)
#define gshscene_resource_id GHOTIIO_SHAPE_SCENE(gshscene_resource_id)
#define gshscene_resource_kind GHOTIIO_SHAPE_SCENE(gshscene_resource_kind)
#define gshscene_resource_kind_string GHOTIIO_SHAPE_SCENE(gshscene_resource_kind_string)
#define gshscene_resource_path GHOTIIO_SHAPE_SCENE(gshscene_resource_path)
#define gshscene_resource_picture GHOTIIO_SHAPE_SCENE(gshscene_resource_picture)
#define gshscene_resource_picture_depth GHOTIIO_SHAPE_SCENE(gshscene_resource_picture_depth)
#define gshscene_resource_refcount GHOTIIO_SHAPE_SCENE(gshscene_resource_refcount)
#define gshscene_resource_release GHOTIIO_SHAPE_SCENE(gshscene_resource_release)
#define gshscene_resource_retain GHOTIIO_SHAPE_SCENE(gshscene_resource_retain)
#define gshscene_result_string GHOTIIO_SHAPE_SCENE(gshscene_result_string)
#define gshscene_store_add_release_listener GHOTIIO_SHAPE_SCENE(gshscene_store_add_release_listener)
#define gshscene_store_create GHOTIIO_SHAPE_SCENE(gshscene_store_create)
#define gshscene_store_create_with_index_hash GHOTIIO_SHAPE_SCENE(gshscene_store_create_with_index_hash)
#define gshscene_store_destroy GHOTIIO_SHAPE_SCENE(gshscene_store_destroy)
#define gshscene_store_intern_glyph_run GHOTIIO_SHAPE_SCENE(gshscene_store_intern_glyph_run)
#define gshscene_store_intern_path GHOTIIO_SHAPE_SCENE(gshscene_store_intern_path)
#define gshscene_store_intern_picture GHOTIIO_SHAPE_SCENE(gshscene_store_intern_picture)
#define gshscene_store_remove_release_listener GHOTIIO_SHAPE_SCENE(gshscene_store_remove_release_listener)
#define gshscene_store_stats GHOTIIO_SHAPE_SCENE(gshscene_store_stats)
#define gshscene_version_number GHOTIIO_SHAPE_SCENE(gshscene_version_number)
#define gshscene_version_string GHOTIIO_SHAPE_SCENE(gshscene_version_string)

/// @endcond

#endif // GHOTI_IO_GSHSCENE_NAMESPACE_H
