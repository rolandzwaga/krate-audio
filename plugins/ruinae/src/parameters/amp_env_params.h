#pragma once
// Amp envelope parameters. The implementation now lives in the shared,
// config-driven env_params.h (AmpEnvParams + handle/register/format/save/load
// wrappers). This header is kept as a thin re-export so existing include sites
// (`parameters/amp_env_params.h`) and the envTime/envCurve helpers continue to
// resolve unchanged.
#include "parameters/env_params.h"
