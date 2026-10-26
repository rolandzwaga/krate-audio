// ==============================================================================
// dsp_ieee_tests: the one owner of the global operator new/delete replacements
// ==============================================================================
// <allocation_detector.h> counts allocations through these replacements, and a test image may define them in exactly
// ONE TU (a second include is a duplicate-symbol link error). The IEEE TUs came from three executables whose owners
// stayed behind (dsp_processors_tests: brownian_drift_test.cpp), so without this TU every NoAllocation positive
// control here read 0 (DropPhaseLock_NoAllocation, MonoHandler_GlideModeNoAllocation). The moved TUs include
// <allocation_detector.h> only.
// ==============================================================================

#include <allocation_detector.h>
#include <allocation_operator_overrides.h>
