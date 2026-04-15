// Placeholder smoke test. Validates that the membrum_fit_core library links and
// the basic types compile.
#include "src/types.h"
#include "src/exciter_classifier.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

int main(int argc, char* argv[]) {
    return Catch::Session().run(argc, argv);
}


TEST_CASE("MembrumFit smoke: types compile and classifier returns a value") {
    MembrumFit::AttackFeatures f{};
    f.logAttackTime = -3.5f;
    f.spectralFlatness = 0.05f;
    const auto e = MembrumFit::classifyExciter(f, MembrumFit::ExciterDecisionSet::Phase1Subset);
    REQUIRE(static_cast<int>(e) >= 0);
}
