/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "kernel/modules/xboxkrnl/kernel_state.h"

#include "kernel/modules/xboxkrnl/xobject.h"


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace {

}


KernelState::KernelState(Runtime* runtime) :
    runtime_(runtime),
    next_handle_(0) {
  pal_        = runtime->pal();
  memory_     = runtime->memory();
  processor_  = runtime->processor();

  objects_mutex_ = xe_mutex_alloc(0);
  XEASSERTNOTNULL(objects_mutex_);
}

KernelState::~KernelState() {
  // Delete all objects.
  // We first copy the list to another list so that the deletion of the objects
  // doesn't mess up iteration.
  std::vector<XObject*> all_objects;
  xe_mutex_lock(objects_mutex_);
  for (std::tr1::unordered_map<X_HANDLE, XObject*>::iterator it =
       objects_.begin(); it != objects_.end(); ++it) {
    all_objects.push_back(it->second);
  }
  objects_.clear();
  xe_mutex_unlock(objects_mutex_);
  for (std::vector<XObject*>::iterator it = all_objects.begin();
       it != all_objects.end(); ++it) {
    // Perhaps call a special ForceRelease method or something?
    XObject* obj = *it;
    delete obj;
  }

  xe_mutex_free(objects_mutex_);
  objects_mutex_ = NULL;

  processor_.reset();
  xe_memory_release(memory_);
  xe_pal_release(pal_);
}

Runtime* KernelState::runtime() {
  return runtime_;
}

xe_pal_ref KernelState::pal() {
  return pal_;
}

xe_memory_ref KernelState::memory() {
  return memory_;
}

cpu::Processor* KernelState::processor() {
  return processor_.get();
}

// TODO(benvanik): invesitgate better handle storage/structure.
// A much better way of doing handles, if performance becomes an issue, would
// be to try to make the pointers 32bit. Then we could round-trip them through
// PPC code without needing to keep a map.
// To achieve this we could try doing allocs in the 32-bit address space via
// the OS alloc calls, or maybe create a section with a reserved size at load
// time (65k handles * 4 is more than enough?).
// We could then use a free list of handle IDs and allocate/release lock-free.

XObject* KernelState::GetObject(X_HANDLE handle) {
  xe_mutex_lock(objects_mutex_);
  std::tr1::unordered_map<X_HANDLE, XObject*>::iterator it =
      objects_.find(handle);
  XObject* value = it != objects_.end() ? it->second : NULL;
  xe_mutex_unlock(objects_mutex_);
  return value;
}

X_HANDLE KernelState::InsertObject(XObject* obj) {
  xe_mutex_lock(objects_mutex_);
  X_HANDLE handle = 0x00001000 + (++next_handle_);
  objects_.insert(std::pair<X_HANDLE, XObject*>(handle, obj));
  xe_mutex_unlock(objects_mutex_);
  return handle;
}

void KernelState::RemoveObject(XObject* obj) {
  xe_mutex_lock(objects_mutex_);
  objects_.erase(obj->handle());
  xe_mutex_unlock(objects_mutex_);
}
