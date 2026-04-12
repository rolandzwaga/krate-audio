// ==============================================================================
// Coupling State Tests -- State v5 round-trip, v4 migration, preset exclusion
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/ibstream.h"

#include <cstdint>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

TEST_CASE("Coupling state stub compiles", "[coupling]") {
    REQUIRE(true);
}

// ==============================================================================
// T044b (FR-022): per-pad sound presets MUST NOT carry couplingAmount.
// Per-pad presets save individual pad sounds (e.g. "Kick 808"); coupling is a
// kit-level concern. Saving a pad preset then reloading it must leave the
// pad's couplingAmount at whatever it was before the load (i.e. NOT clobbered
// by the preset blob).
// ==============================================================================

TEST_CASE("Phase 6 (T044b): per-pad preset excludes couplingAmount (FR-022)",
          "[coupling][phase6][preset]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Select pad 2 and set a distinctive couplingAmount we want to preserve.
    const int pad = 2;
    const auto padCouplingId = static_cast<ParamID>(
        Membrum::padParamId(pad, Membrum::kPadCouplingAmount));
    controller.setParamNormalized(Membrum::kSelectedPadId,
                                  static_cast<double>(pad) / 31.0);
    controller.setParamNormalized(padCouplingId, 0.9);
    const double couplingBeforeSave = controller.getParamNormalized(padCouplingId);
    REQUIRE(couplingBeforeSave == Approx(0.9).margin(1e-6));

    // Save the per-pad preset stream.
    IBStream* stream = controller.padPresetStateProvider();
    REQUIRE(stream != nullptr);

    // Verify blob size is exactly 284 bytes (version 4 + 2 * int32 + 34 *
    // float64). Offset 36 (couplingAmount) is NOT part of this format.
    int64 end = 0;
    stream->seek(0, IBStream::kIBSeekEnd, &end);
    CHECK(end == 284);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    // Now change the pad's couplingAmount to a different value before reload.
    controller.setParamNormalized(padCouplingId, 0.2);
    const double couplingBeforeLoad = controller.getParamNormalized(padCouplingId);
    REQUIRE(couplingBeforeLoad == Approx(0.2).margin(1e-6));

    // Reload the preset -- couplingAmount must NOT be restored by this load.
    // It must stay at 0.2 (the pre-load value) rather than revert to 0.9.
    const bool ok = controller.padPresetLoadProvider(stream);
    CHECK(ok);

    const double couplingAfterLoad = controller.getParamNormalized(padCouplingId);
    CHECK(couplingAfterLoad == Approx(0.2).margin(1e-6));
    CHECK(couplingAfterLoad != Approx(0.9).margin(1e-6));

    stream->release();
    REQUIRE(controller.terminate() == kResultOk);
}
