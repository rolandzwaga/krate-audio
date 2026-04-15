#pragma once

#include "bobyqa_refine.h"

namespace MembrumFit {

// Global-escape refinement for cases where BOBYQA gets stuck in a local
// minimum. Uses NLopt's NLOPT_GN_CRS2_LM (Controlled Random Search with
// Local Mutation, Kaelo & Ali 2006) -- BSD-licensed, derivative-free,
// global, already linked via the NLopt FetchContent.
//
// The plan reserved the name "cmaes_refine" for this hook because the
// spec was written when libcmaes (LGPL) was the candidate; switching to
// NLopt's CRS keeps the entire toolchain permissively licensed without
// any new dependencies. CMA-ES via libcmaes remains available via the
// MEMBRUM_FIT_ENABLE_CMAES CMake option for users who explicitly opt in.
RefineResult refineGlobalCRS(const RefineContext& ctx,
                             RenderableMembrumVoice& voice);

}  // namespace MembrumFit
