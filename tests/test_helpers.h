/**
 * @file
 *
 * Shared helpers for the Ghoti.io Shape Scene unit tests.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GSHSCENE_TEST_HELPERS_H
#define GHOTI_IO_GSHSCENE_TEST_HELPERS_H

#include <gtest/gtest.h>

#include <ghoti.io/shape-scene/shape-scene.h>

#include <cstdint>

/** The number a dump prints for a resource: its place in its store, the low half of its id (the high half names the store, which a dump
 * leaves out so that its text, and what is hashed over it, does not depend on how many stores the process made before). */
inline uint64_t dump_id(const GSHSCENE_Resource * resource) {
  return gshscene_resource_id(resource) & 0xFFFFFFFFull;
}

#endif /* GHOTI_IO_GSHSCENE_TEST_HELPERS_H */
