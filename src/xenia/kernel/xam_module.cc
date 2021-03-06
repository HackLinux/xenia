/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam_module.h"

#include "xenia/base/math.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam_private.h"

namespace xe {
namespace kernel {

XamModule::XamModule(Emulator* emulator, KernelState* kernel_state)
    : XKernelModule(kernel_state, "xe:\\xam.xex") {
  RegisterExportTable(export_resolver_);

  // Register all exported functions.
  xam::RegisterAvatarExports(export_resolver_, kernel_state_);
  xam::RegisterContentExports(export_resolver_, kernel_state_);
  xam::RegisterInfoExports(export_resolver_, kernel_state_);
  xam::RegisterInputExports(export_resolver_, kernel_state_);
  xam::RegisterMsgExports(export_resolver_, kernel_state_);
  xam::RegisterNetExports(export_resolver_, kernel_state_);
  xam::RegisterNotifyExports(export_resolver_, kernel_state_);
  xam::RegisterNuiExports(export_resolver_, kernel_state_);
  xam::RegisterUIExports(export_resolver_, kernel_state_);
  xam::RegisterUserExports(export_resolver_, kernel_state_);
  xam::RegisterVideoExports(export_resolver_, kernel_state_);
  xam::RegisterVoiceExports(export_resolver_, kernel_state_);
}

std::vector<xe::cpu::Export*> xam_exports(4096);

xe::cpu::Export* RegisterExport_xam(xe::cpu::Export* export) {
  assert_true(export->ordinal < xam_exports.size());
  xam_exports[export->ordinal] = export;
  return export;
}

void XamModule::RegisterExportTable(xe::cpu::ExportResolver* export_resolver) {
  assert_not_null(export_resolver);

// Build the export table used for resolution.
#include "xenia/kernel/util/export_table_pre.inc"
  static xe::cpu::Export xam_export_table[] = {
#include "xenia/kernel/xam_table.inc"
  };
#include "xenia/kernel/util/export_table_post.inc"
  for (size_t i = 0; i < xe::countof(xam_export_table); ++i) {
    auto& export = xam_export_table[i];
    assert_true(export.ordinal < xam_exports.size());
    if (!xam_exports[export.ordinal]) {
      xam_exports[export.ordinal] = &export;
    }
  }
  export_resolver->RegisterTable("xam.xex", &xam_exports);
}

XamModule::~XamModule() {}

}  // namespace kernel
}  // namespace xe
