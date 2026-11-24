// Compatibility include for the original safety API and its regression tests.
// Production tools own safety::SnapshotStore; old SnapshotManager callers use the same object.
#pragma once
#include "safety/snapshot_store.hpp"
namespace platypus::util {
using SnapshotManager = safety::SnapshotStore;
}
