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
#define GSHSCENE_Color GHOTIIO_SHAPE_SCENE(GSHSCENE_Color)
#define GSHSCENE_Color_Space GHOTIIO_SHAPE_SCENE(GSHSCENE_Color_Space)
#define GSHSCENE_Composite GHOTIIO_SHAPE_SCENE(GSHSCENE_Composite)
#define GSHSCENE_Damage GHOTIIO_SHAPE_SCENE(GSHSCENE_Damage)
#define GSHSCENE_Damage_Options GHOTIIO_SHAPE_SCENE(GSHSCENE_Damage_Options)
#define GSHSCENE_Diff GHOTIIO_SHAPE_SCENE(GSHSCENE_Diff)
#define GSHSCENE_Diff_Kind GHOTIIO_SHAPE_SCENE(GSHSCENE_Diff_Kind)
#define GSHSCENE_Edit GHOTIIO_SHAPE_SCENE(GSHSCENE_Edit)
#define GSHSCENE_Glyph GHOTIIO_SHAPE_SCENE(GSHSCENE_Glyph)
#define GSHSCENE_Glyph_Run GHOTIIO_SHAPE_SCENE(GSHSCENE_Glyph_Run)
#define GSHSCENE_Glyph_Source GHOTIIO_SHAPE_SCENE(GSHSCENE_Glyph_Source)
#define GSHSCENE_Group GHOTIIO_SHAPE_SCENE(GSHSCENE_Group)
#define GSHSCENE_Index_Hash_Fn GHOTIIO_SHAPE_SCENE(GSHSCENE_Index_Hash_Fn)
#define GSHSCENE_Item GHOTIIO_SHAPE_SCENE(GSHSCENE_Item)
#define GSHSCENE_Item_Kind GHOTIIO_SHAPE_SCENE(GSHSCENE_Item_Kind)
#define GSHSCENE_Key GHOTIIO_SHAPE_SCENE(GSHSCENE_Key)
#define GSHSCENE_Limits GHOTIIO_SHAPE_SCENE(GSHSCENE_Limits)
#define GSHSCENE_List GHOTIIO_SHAPE_SCENE(GSHSCENE_List)
#define GSHSCENE_Node GHOTIIO_SHAPE_SCENE(GSHSCENE_Node)
#define GSHSCENE_Node_Info GHOTIIO_SHAPE_SCENE(GSHSCENE_Node_Info)
#define GSHSCENE_Node_Kind GHOTIIO_SHAPE_SCENE(GSHSCENE_Node_Kind)
#define GSHSCENE_Ordering GHOTIIO_SHAPE_SCENE(GSHSCENE_Ordering)
#define GSHSCENE_Paint GHOTIIO_SHAPE_SCENE(GSHSCENE_Paint)
#define GSHSCENE_Paint_Kind GHOTIIO_SHAPE_SCENE(GSHSCENE_Paint_Kind)
#define GSHSCENE_Problem_Reason GHOTIIO_SHAPE_SCENE(GSHSCENE_Problem_Reason)
#define GSHSCENE_Problems GHOTIIO_SHAPE_SCENE(GSHSCENE_Problems)
#define GSHSCENE_Rect GHOTIIO_SHAPE_SCENE(GSHSCENE_Rect)
#define GSHSCENE_Release_Fn GHOTIIO_SHAPE_SCENE(GSHSCENE_Release_Fn)
#define GSHSCENE_Resource GHOTIIO_SHAPE_SCENE(GSHSCENE_Resource)
#define GSHSCENE_Resource_Kind GHOTIIO_SHAPE_SCENE(GSHSCENE_Resource_Kind)
#define GSHSCENE_Result GHOTIIO_SHAPE_SCENE(GSHSCENE_Result)
#define GSHSCENE_Scene GHOTIIO_SHAPE_SCENE(GSHSCENE_Scene)
#define GSHSCENE_Store GHOTIIO_SHAPE_SCENE(GSHSCENE_Store)
#define GSHSCENE_Store_Stats GHOTIIO_SHAPE_SCENE(GSHSCENE_Store_Stats)
#define GSHSCENE_Tie GHOTIIO_SHAPE_SCENE(GSHSCENE_Tie)
#define GSHSCENE_Tie_Reason GHOTIIO_SHAPE_SCENE(GSHSCENE_Tie_Reason)
#define GSHSCENE_Version GHOTIIO_SHAPE_SCENE(GSHSCENE_Version)
#define GSHSCENE_Visit GHOTIIO_SHAPE_SCENE(GSHSCENE_Visit)
#define GSHSCENE_Visit_Fn GHOTIIO_SHAPE_SCENE(GSHSCENE_Visit_Fn)

/* Public functions. */
#define gshscene_allocator_default GHOTIIO_SHAPE_SCENE(gshscene_allocator_default)
#define gshscene_color_check GHOTIIO_SHAPE_SCENE(gshscene_color_check)
#define gshscene_color_init GHOTIIO_SHAPE_SCENE(gshscene_color_init)
#define gshscene_color_space_check GHOTIIO_SHAPE_SCENE(gshscene_color_space_check)
#define gshscene_color_space_from_info GHOTIIO_SHAPE_SCENE(gshscene_color_space_from_info)
#define gshscene_color_space_is_target GHOTIIO_SHAPE_SCENE(gshscene_color_space_is_target)
#define gshscene_composite_is_unbounded GHOTIIO_SHAPE_SCENE(gshscene_composite_is_unbounded)
#define gshscene_composite_string GHOTIIO_SHAPE_SCENE(gshscene_composite_string)
#define gshscene_diff GHOTIIO_SHAPE_SCENE(gshscene_diff)
#define gshscene_diff_count GHOTIIO_SHAPE_SCENE(gshscene_diff_count)
#define gshscene_diff_damage GHOTIIO_SHAPE_SCENE(gshscene_diff_damage)
#define gshscene_diff_destroy GHOTIIO_SHAPE_SCENE(gshscene_diff_destroy)
#define gshscene_diff_foreground_changed GHOTIIO_SHAPE_SCENE(gshscene_diff_foreground_changed)
#define gshscene_diff_get GHOTIIO_SHAPE_SCENE(gshscene_diff_get)
#define gshscene_edit_add GHOTIIO_SHAPE_SCENE(gshscene_edit_add)
#define gshscene_edit_base GHOTIIO_SHAPE_SCENE(gshscene_edit_base)
#define gshscene_edit_child GHOTIIO_SHAPE_SCENE(gshscene_edit_child)
#define gshscene_edit_destroy GHOTIIO_SHAPE_SCENE(gshscene_edit_destroy)
#define gshscene_edit_foreground GHOTIIO_SHAPE_SCENE(gshscene_edit_foreground)
#define gshscene_edit_lookup_key GHOTIIO_SHAPE_SCENE(gshscene_edit_lookup_key)
#define gshscene_edit_move GHOTIIO_SHAPE_SCENE(gshscene_edit_move)
#define gshscene_edit_node GHOTIIO_SHAPE_SCENE(gshscene_edit_node)
#define gshscene_edit_node_count GHOTIIO_SHAPE_SCENE(gshscene_edit_node_count)
#define gshscene_edit_remove GHOTIIO_SHAPE_SCENE(gshscene_edit_remove)
#define gshscene_edit_remove_key GHOTIIO_SHAPE_SCENE(gshscene_edit_remove_key)
#define gshscene_edit_set GHOTIIO_SHAPE_SCENE(gshscene_edit_set)
#define gshscene_edit_set_foreground GHOTIIO_SHAPE_SCENE(gshscene_edit_set_foreground)
#define gshscene_edit_store GHOTIIO_SHAPE_SCENE(gshscene_edit_store)
#define gshscene_edit_upsert GHOTIIO_SHAPE_SCENE(gshscene_edit_upsert)
#define gshscene_instance_path_compare GHOTIIO_SHAPE_SCENE(gshscene_instance_path_compare)
#define gshscene_instance_path_key GHOTIIO_SHAPE_SCENE(gshscene_instance_path_key)
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
#define gshscene_list_foreground GHOTIIO_SHAPE_SCENE(gshscene_list_foreground)
#define gshscene_list_group_pop GHOTIIO_SHAPE_SCENE(gshscene_list_group_pop)
#define gshscene_list_group_push GHOTIIO_SHAPE_SCENE(gshscene_list_group_push)
#define gshscene_list_hash GHOTIIO_SHAPE_SCENE(gshscene_list_hash)
#define gshscene_list_instance GHOTIIO_SHAPE_SCENE(gshscene_list_instance)
#define gshscene_list_is_closed GHOTIIO_SHAPE_SCENE(gshscene_list_is_closed)
#define gshscene_list_item GHOTIIO_SHAPE_SCENE(gshscene_list_item)
#define gshscene_list_reset GHOTIIO_SHAPE_SCENE(gshscene_list_reset)
#define gshscene_list_set_foreground GHOTIIO_SHAPE_SCENE(gshscene_list_set_foreground)
#define gshscene_list_tie GHOTIIO_SHAPE_SCENE(gshscene_list_tie)
#define gshscene_list_tie_count GHOTIIO_SHAPE_SCENE(gshscene_list_tie_count)
#define gshscene_node_init GHOTIIO_SHAPE_SCENE(gshscene_node_init)
#define gshscene_node_kind_string GHOTIIO_SHAPE_SCENE(gshscene_node_kind_string)
#define gshscene_problem_reason_string GHOTIIO_SHAPE_SCENE(gshscene_problem_reason_string)
#define gshscene_problems_count GHOTIIO_SHAPE_SCENE(gshscene_problems_count)
#define gshscene_problems_destroy GHOTIIO_SHAPE_SCENE(gshscene_problems_destroy)
#define gshscene_problems_get GHOTIIO_SHAPE_SCENE(gshscene_problems_get)
#define gshscene_resource_bytes GHOTIIO_SHAPE_SCENE(gshscene_resource_bytes)
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
#define gshscene_scene_create GHOTIIO_SHAPE_SCENE(gshscene_scene_create)
#define gshscene_scene_current GHOTIIO_SHAPE_SCENE(gshscene_scene_current)
#define gshscene_scene_destroy GHOTIIO_SHAPE_SCENE(gshscene_scene_destroy)
#define gshscene_scene_merge GHOTIIO_SHAPE_SCENE(gshscene_scene_merge)
#define gshscene_scene_open_edit GHOTIIO_SHAPE_SCENE(gshscene_scene_open_edit)
#define gshscene_scene_publish GHOTIIO_SHAPE_SCENE(gshscene_scene_publish)
#define gshscene_scene_store GHOTIIO_SHAPE_SCENE(gshscene_scene_store)
#define gshscene_store_add_release_listener GHOTIIO_SHAPE_SCENE(gshscene_store_add_release_listener)
#define gshscene_store_create GHOTIIO_SHAPE_SCENE(gshscene_store_create)
#define gshscene_store_create_with_index_hash GHOTIIO_SHAPE_SCENE(gshscene_store_create_with_index_hash)
#define gshscene_store_destroy GHOTIIO_SHAPE_SCENE(gshscene_store_destroy)
#define gshscene_store_intern_glyph_run GHOTIIO_SHAPE_SCENE(gshscene_store_intern_glyph_run)
#define gshscene_store_intern_path GHOTIIO_SHAPE_SCENE(gshscene_store_intern_path)
#define gshscene_store_intern_picture GHOTIIO_SHAPE_SCENE(gshscene_store_intern_picture)
#define gshscene_store_remove_release_listener GHOTIIO_SHAPE_SCENE(gshscene_store_remove_release_listener)
#define gshscene_store_stats GHOTIIO_SHAPE_SCENE(gshscene_store_stats)
#define gshscene_version_child GHOTIIO_SHAPE_SCENE(gshscene_version_child)
#define gshscene_version_dump GHOTIIO_SHAPE_SCENE(gshscene_version_dump)
#define gshscene_version_foreground GHOTIIO_SHAPE_SCENE(gshscene_version_foreground)
#define gshscene_version_hash GHOTIIO_SHAPE_SCENE(gshscene_version_hash)
#define gshscene_version_item_node GHOTIIO_SHAPE_SCENE(gshscene_version_item_node)
#define gshscene_version_key_count GHOTIIO_SHAPE_SCENE(gshscene_version_key_count)
#define gshscene_version_list GHOTIIO_SHAPE_SCENE(gshscene_version_list)
#define gshscene_version_lookup_key GHOTIIO_SHAPE_SCENE(gshscene_version_lookup_key)
#define gshscene_version_node GHOTIIO_SHAPE_SCENE(gshscene_version_node)
#define gshscene_version_node_count GHOTIIO_SHAPE_SCENE(gshscene_version_node_count)
#define gshscene_version_number GHOTIIO_SHAPE_SCENE(gshscene_version_number)
#define gshscene_version_picture GHOTIIO_SHAPE_SCENE(gshscene_version_picture)
#define gshscene_version_release GHOTIIO_SHAPE_SCENE(gshscene_version_release)
#define gshscene_version_retain GHOTIIO_SHAPE_SCENE(gshscene_version_retain)
#define gshscene_version_serial GHOTIIO_SHAPE_SCENE(gshscene_version_serial)
#define gshscene_version_string GHOTIIO_SHAPE_SCENE(gshscene_version_string)
#define gshscene_version_visit GHOTIIO_SHAPE_SCENE(gshscene_version_visit)

/// @endcond

#endif // GHOTI_IO_GSHSCENE_NAMESPACE_H
