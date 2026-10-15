// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Points mov::io at this build's proj.db before any test case runs, for test
// executables whose readers look EPSG codes up without each test case saying
// where the database is. Without it PROJ searches its own default, which on
// Linux is vcpkg's install prefix but on Windows is beside proj.dll, where the
// build tree has no database: an EPSG lookup there is projection_unavailable,
// not the error the test expects.

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "mov/io/projection.hpp"

namespace {

class ProjDataDir final : public Catch::EventListenerBase {
 public:
  using Catch::EventListenerBase::EventListenerBase;

  void testRunStarting(const Catch::TestRunInfo& /*info*/) override {
#if defined(MOV_TEST_PROJ_DATA_DIR)
    mov::io::set_projection_data_dir(MOV_TEST_PROJ_DATA_DIR);
#endif
  }
};

}  // namespace

CATCH_REGISTER_LISTENER(ProjDataDir)
