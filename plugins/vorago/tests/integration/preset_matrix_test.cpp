// ==============================================================================
// Vorago Phase 14 - coverage matrix, non-subset and sound-space distinctness; FR-011a, FR-013, FR-015, FR-036; SC-008, SC-010, SC-018, SC-028, SC-029; filled by T042
// ==============================================================================
// Registered by T003 (specs/vorago-phase14-presets-release/tasks.md); filled by
// T042. The three aggregate cases, all [long][vorago-sweep][vorago-aggregate]:
// never per push. The aggregate job runs them with VORAGO_SWEEP_IN pointing at
// the shard jobs' record files, so sweepRecordFor loads every record and only
// the plan 6.13 control twins render here (FR-036, ruling R-4 / P-6).
//
//  - Vorago_PresetMatrix_CoverageComplete (plan 6.12 "Coverage"; FR-013, SC-008,
//    SC-018, SC-029). Per the G2 ruling (2026-09-29; spec SC-008 / SC-029,
//    tools/vorago_preset_defs.h requiredPrimaryCells) E6.hi / E7.hi are verified
//    SECONDARIES, so the Group E primary check covers E1-E5 (five distinct
//    presets) and E6.hi / E7.hi each need >= 1 verified secondary claim.
//  - Vorago_PresetMatrix_NoShowcaseSubset (plan 6.12 witness search; FR-011a).
//  - Vorago_PresetSweep_SoundSpaceDistinct (plan 6.6, 6.13; FR-015, FR-036,
//    SC-010): controls (a), (a'), (b), (c) on the control set, then every pair.
//    A control failure is an FR-017 stop, never a widening.
//
// Every assertion runs on the test thread; control renders run through runJobs
// inside the support helpers.
// ==============================================================================

#include "preset_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

namespace PD = ::Vorago::PresetDefs;
using VoragoTest::ClaimRole;

/// Every record 0..N (N = the default-surface pseudo-preset), each REQUIREd
/// complete (K takes), so a missing shard record fails loudly.
std::vector<const VoragoTest::SweepRecord*> t042AllRecords() {
    const std::size_t n = PD::allPresets().size();
    std::vector<const VoragoTest::SweepRecord*> out;
    out.reserve(n + 1u);
    for (std::size_t i = 0; i <= n; ++i) {
        std::printf("[aggregate] index %zu: computing / loading record...\n", i);
        std::fflush(stdout);
        const VoragoTest::SweepRecord& rec = VoragoTest::sweepRecordFor(i);
        INFO("index " << i << ": " << rec.name);
        REQUIRE(rec.takes.size() == static_cast<std::size_t>(VoragoTest::kRuledTakes));
        out.push_back(&rec);
    }
    return out;
}

std::string t042Label(PD::Capability c) {
    const auto i = static_cast<std::size_t>(c);
    if (i >= PD::kNumCapabilities) {
        return "<none>";
    }
    return std::string(PD::cellSpecs()[i].label);
}

bool t042IsSecondary(const PD::VoragoPresetDef& def, PD::Capability c) {
    return std::find(def.secondaries.begin(), def.secondaries.end(), c) != def.secondaries.end();
}

/// The plan 6.12 one rule: Q verifies c iff verifiedAt(Secondary), and, when c is
/// Q's own primary, also verifiedAt(Primary). `qPrimary` is Count for the
/// default-surface pseudo-preset.
bool t042Verifies(const VoragoTest::VerificationVector& v, PD::Capability c,
                  PD::Capability qPrimary) {
    const auto i = static_cast<std::size_t>(c);
    if (i >= v.cells.size()) {
        return false;
    }
    const VoragoTest::CellOutcome& o = v.cells[i];
    if (!VoragoTest::verifiedAt(o, c, ClaimRole::Secondary)) {
        return false;
    }
    return c != qPrimary || VoragoTest::verifiedAt(o, c, ClaimRole::Primary);
}

/// The measured default-state set (plan 6.12): the D cells the default-surface
/// pseudo-preset verifies at the secondary bar, in Capability order.
std::vector<PD::Capability> t042MeasuredDefaultState(const VoragoTest::SweepRecord& pseudo) {
    std::vector<PD::Capability> out;
    for (std::size_t i = 0; i < PD::kNumCapabilities; ++i) {
        if (PD::cellSpecs()[i].group != PD::CapabilityGroup::D) {
            continue;
        }
        const auto c = static_cast<PD::Capability>(i);
        if (t042Verifies(pseudo.vec, c, PD::Capability::Count)) {
            out.push_back(c);
        }
    }
    return out;
}

/// requiredPrimaryCells() recomputed from a GIVEN default-state set (plan 6.12:
/// "from the measured default-state set, not only from the recorded constant").
/// Same derivation as tools/vorago_preset_defs.h requiredPrimaryCells(): S1-S10,
/// M1-M12, E1-E5, plus (D1.* u D3.* u D8.* u D9.*) minus the set; E6.hi / E7.hi
/// are secondaries (G2 ruling 2026-09-29). In Capability order.
std::vector<PD::Capability> t042RequiredPrimariesFrom(const std::vector<PD::Capability>& dflt) {
    using C = PD::Capability;
    const auto inRange = [](C c, C first, C last) {
        const auto v = static_cast<std::uint8_t>(c);
        return v >= static_cast<std::uint8_t>(first) && v <= static_cast<std::uint8_t>(last);
    };
    std::vector<C> out;
    for (std::size_t i = 0; i < PD::kNumCapabilities; ++i) {
        const auto c = static_cast<C>(i);
        if (inRange(c, C::S1Noise, C::E5GhostBursts)) {
            out.push_back(c);
            continue;
        }
        const bool eligibleD = inRange(c, C::D1Glass, C::D1GlassSphere) ||
                               inRange(c, C::D3Direct, C::D3MetallicHiss) ||
                               inRange(c, C::D8Standard, C::D9SlowAttack);
        if (eligibleD && std::find(dflt.begin(), dflt.end(), c) == dflt.end()) {
            out.push_back(c);
        }
    }
    return out;
}

std::string t042Names(const std::vector<PD::Capability>& cells) {
    std::string s;
    for (const PD::Capability c : cells) {
        if (!s.empty()) {
            s += ", ";
        }
        s += t042Label(c);
    }
    return s.empty() ? std::string("<none>") : s;
}

}  // namespace

// ==============================================================================
// Coverage (plan 6.12; FR-013, SC-008, SC-018, SC-029)
// ==============================================================================
TEST_CASE("Vorago_PresetMatrix_CoverageComplete",
          "[vorago][preset][long][vorago-sweep][vorago-aggregate]") {
    using C = PD::Capability;
    const std::vector<PD::VoragoPresetDef>& defs = PD::allPresets();
    const std::size_t n = defs.size();
    REQUIRE(n > 0u);
    const std::vector<const VoragoTest::SweepRecord*> recs = t042AllRecords();
    REQUIRE(recs.size() == n + 1u);
    const VoragoTest::SweepRecord& pseudo = *recs[n];

    // ---- The printed preset x cell matrix -----------------------------------------
    std::printf("\n=== Vorago_PresetMatrix_CoverageComplete (FR-013, SC-008, SC-018, SC-029; "
                "N = %zu, K = %d) ===\n",
                n, VoragoTest::kRuledTakes);
    std::printf("Presets (matrix column = index mod 10; last column = default surface):\n");
    for (std::size_t p = 0; p < n; ++p) {
        std::printf("  %2zu  %-28s  primary %s\n", p, recs[p]->name.c_str(),
                    t042Label(defs[p].primary).c_str());
    }
    std::printf("Legend: P verified primary | S verified secondary | X claimed-failed | "
                "v unclaimed, verified | . unclaimed, not verified\n");
    std::string header(32u, ' ');
    for (std::size_t p = 0; p < n; ++p) {
        header += static_cast<char>('0' + static_cast<int>(p % 10u));
    }
    header += " d";
    std::printf("%s\n", header.c_str());

    std::size_t claimedFailed = 0;
    std::vector<std::size_t> verifierCount(PD::kNumCapabilities, 0u);
    for (std::size_t i = 0; i < PD::kNumCapabilities; ++i) {
        const auto c = static_cast<C>(i);
        std::string row;
        for (std::size_t p = 0; p < n; ++p) {
            const bool verified = t042Verifies(recs[p]->vec, c, defs[p].primary);
            if (verified) {
                ++verifierCount[i];
            }
            const bool isPrimary = defs[p].primary == c;
            const bool claimed = isPrimary || t042IsSecondary(defs[p], c);
            if (claimed && !verified) {
                ++claimedFailed;
                row += 'X';
            } else if (claimed) {
                row += isPrimary ? 'P' : 'S';
            } else {
                row += verified ? 'v' : '.';
            }
        }
        const bool dflt = t042Verifies(pseudo.vec, c, C::Count);
        std::printf("  %-28.28s  %s %c  (%zu factory verifier%s)\n", t042Label(c).c_str(),
                    row.c_str(), dflt ? 'v' : '.', verifierCount[i],
                    verifierCount[i] == 1u ? "" : "s");
    }

    const std::vector<C> measured = t042MeasuredDefaultState(pseudo);
    const std::span<const C> recordedSpan = PD::kRecordedDefaultStateCells();
    std::vector<C> recorded(recordedSpan.begin(), recordedSpan.end());
    std::printf("\nDefault-state cells (measured on the default surface, %zu): %s\n",
                measured.size(), t042Names(measured).c_str());
    std::printf("kRecordedDefaultStateCells (%zu): %s\n", recorded.size(),
                t042Names(recorded).c_str());
    std::fflush(stdout);

    // ---- Every cell has >= 1 factory verifier; 0 claimed-but-failed ------------------
    for (std::size_t i = 0; i < PD::kNumCapabilities; ++i) {
        INFO("cell " << t042Label(static_cast<C>(i)) << " has no factory verifier");
        CHECK(verifierCount[i] >= 1u);
    }
    {
        INFO("claimed-but-failed entries in the matrix (X): " << claimedFailed);
        CHECK(claimedFailed == 0u);
    }

    // ---- Primaries: verified at F, unique, never default-state, never D10.1 ----------
    std::vector<C> verifiedPrimaries;
    for (std::size_t p = 0; p < n; ++p) {
        const C prim = defs[p].primary;
        const VoragoTest::CellOutcome& o = recs[p]->vec.cells[static_cast<std::size_t>(prim)];
        INFO(recs[p]->name << " primary " << t042Label(prim) << ": d " << o.d << ", skip ["
                           << o.skip << "]");
        const bool atF = t042Verifies(recs[p]->vec, prim, prim);
        CHECK(atF);
        const bool notD10 = prim != C::D10FreezeHolds;
        CHECK(notD10);  // FR-011b
        CHECK(std::find(measured.begin(), measured.end(), prim) == measured.end());
        if (atF) {
            verifiedPrimaries.push_back(prim);
        }
    }
    std::vector<C> sortedPrimaries = verifiedPrimaries;
    std::sort(sortedPrimaries.begin(), sortedPrimaries.end());
    {
        INFO("two presets share a primary");
        CHECK(std::adjacent_find(sortedPrimaries.begin(), sortedPrimaries.end()) ==
              sortedPrimaries.end());
    }

    // ---- E1-E5 each the verified primary of a distinct preset -------------------------
    {
        std::vector<std::size_t> eHosts;
        for (auto e = static_cast<std::size_t>(C::E1PartialBloom);
             e <= static_cast<std::size_t>(C::E5GhostBursts); ++e) {
            const auto c = static_cast<C>(e);
            std::optional<std::size_t> host;
            for (std::size_t p = 0; p < n && !host.has_value(); ++p) {
                if (defs[p].primary == c && t042Verifies(recs[p]->vec, c, c)) {
                    host = p;
                }
            }
            INFO(t042Label(c) << " is no preset's verified primary");
            CHECK(host.has_value());
            if (host.has_value()) {
                std::printf("%s: verified primary of %s\n", t042Label(c).c_str(),
                            recs[*host]->name.c_str());
                eHosts.push_back(*host);
            }
        }
        std::sort(eHosts.begin(), eHosts.end());
        INFO("E1-E5 primaries must sit on five distinct presets");
        CHECK(eHosts.size() == 5u);
        CHECK(std::adjacent_find(eHosts.begin(), eHosts.end()) == eHosts.end());
    }

    // ---- E6.hi / E7.hi: >= 1 verified secondary claim each (G2 ruling, SC-008) --------
    for (const C c : {C::E6SyncRateHi, C::E7SelfAffinityHi}) {
        std::size_t hosts = 0;
        for (std::size_t p = 0; p < n; ++p) {
            if (t042IsSecondary(defs[p], c) && t042Verifies(recs[p]->vec, c, defs[p].primary)) {
                std::printf("%s: verified secondary of %s\n", t042Label(c).c_str(),
                            recs[p]->name.c_str());
                ++hosts;
            }
        }
        INFO(t042Label(c) << " is no preset's verified secondary claim");
        CHECK(hosts >= 1u);
    }

    // ---- SC-018: >= 1 verified D11 and D12.1 ------------------------------------------
    for (const C c : {C::D11GhostReverse, C::D12TriggersOn}) {
        INFO("SC-018: " << t042Label(c) << " has no factory verifier");
        CHECK(verifierCount[static_cast<std::size_t>(c)] >= 1u);
    }

    // ---- Measured default-state set == the recorded constant --------------------------
    {
        std::sort(recorded.begin(), recorded.end());
        INFO("measured default-state set: " << t042Names(measured)
                                            << "; recorded: " << t042Names(recorded));
        const bool sameSet = measured == recorded;  // both in Capability order
        CHECK(sameSet);
    }

    // ---- Primary set == requiredPrimaryCells() from the MEASURED set (SC-029) ---------
    {
        const std::vector<C> required = t042RequiredPrimariesFrom(measured);
        const std::vector<C> requiredRecorded = PD::requiredPrimaryCells();
        std::printf("Required primaries from the measured set: %zu; from the recorded "
                    "constant: %zu; verified primaries: %zu\n",
                    required.size(), requiredRecorded.size(), sortedPrimaries.size());
        std::fflush(stdout);
        INFO("required (measured): " << t042Names(required));
        INFO("verified primaries: " << t042Names(sortedPrimaries));
        const bool primariesMatch = sortedPrimaries == required;  // both in Capability order
        CHECK(primariesMatch);
        const bool requiredMatch = required == requiredRecorded;
        CHECK(requiredMatch);
    }
}

// ==============================================================================
// Non-subset (plan 6.12 witness search; FR-011a, SC-008)
// ==============================================================================
TEST_CASE("Vorago_PresetMatrix_NoShowcaseSubset",
          "[vorago][preset][long][vorago-sweep][vorago-aggregate]") {
    const std::vector<PD::VoragoPresetDef>& defs = PD::allPresets();
    const std::size_t n = defs.size();
    REQUIRE(n > 0u);
    const std::vector<const VoragoTest::SweepRecord*> recs = t042AllRecords();
    REQUIRE(recs.size() == n + 1u);

    std::printf("\n=== Vorago_PresetMatrix_NoShowcaseSubset (FR-011a; %zu ordered pairs) ===\n",
                n * (n - 1u));
    std::size_t subsets = 0;
    for (std::size_t p = 0; p < n; ++p) {
        std::printf("\n%s (primary %s):\n", recs[p]->name.c_str(),
                    t042Label(defs[p].primary).c_str());
        for (std::size_t q = 0; q < n; ++q) {
            if (q == p) {
                continue;
            }
            const std::optional<PD::Capability> w =
                VoragoTest::findWitness(defs[p], recs[q]->vec, defs[q].primary);
            if (w.has_value()) {
                std::printf("  %s vs %s: witness %s\n", recs[p]->name.c_str(),
                            recs[q]->name.c_str(), t042Label(*w).c_str());
            } else {
                std::printf("  %s vs %s: SUBSET\n", recs[p]->name.c_str(), recs[q]->name.c_str());
                ++subsets;
            }
            INFO(recs[p]->name << " vs " << recs[q]->name
                               << ": every claim of P is verified by Q (SUBSET)");
            CHECK(w.has_value());
        }
    }
    std::printf("\nSUBSET pairs: %zu\n", subsets);
    std::fflush(stdout);
}

// ==============================================================================
// Sound-space distinctness + controls (plan 6.6, 6.13; FR-015, FR-036, SC-010)
// ==============================================================================
TEST_CASE("Vorago_PresetSweep_SoundSpaceDistinct",
          "[vorago][preset][long][vorago-sweep][vorago-aggregate]") {
    const std::vector<PD::VoragoPresetDef>& defs = PD::allPresets();
    const std::size_t n = defs.size();
    REQUIRE(n >= 2u);
    const std::vector<const VoragoTest::SweepRecord*> recs = t042AllRecords();
    REQUIRE(recs.size() == n + 1u);
    const unsigned threads = VoragoTest::sweepPoolWidth();
    const int K = VoragoTest::kRuledTakes;

    std::printf("\n=== Vorago_PresetSweep_SoundSpaceDistinct (FR-015, FR-036, SC-010; N = %zu, "
                "K = %d) ===\n",
                n, K);

    // Factory records only (the pseudo-preset is not a preset).
    std::vector<VoragoTest::SweepRecord> factory;
    factory.reserve(n);
    for (std::size_t p = 0; p < n; ++p) {
        factory.push_back(*recs[p]);
    }

    // ---- Control set C (>= 3 distinct presets) -----------------------------------------
    const VoragoTest::ControlSet cs = VoragoTest::controlSet(factory);
    std::printf("Control set C (%zu):", cs.members.size());
    for (const std::size_t m : cs.members) {
        std::printf(" [%s]", factory[m].name.c_str());
    }
    std::printf("\n");
    std::fflush(stdout);
    {
        INFO("C-7.3: the control set needs >= " << VoragoTest::kMinControlSet
                                                << " distinct presets (FR-017 stop)");
        REQUIRE(cs.ok);
    }

    // Take 0 is the stored-seed take (computeTakes); its M1..M3 mean; the Comp.
    const auto storedSeedOf = [](const VoragoTest::SweepRecord& r) {
        return r.takes.front().seedIndex;
    };
    const auto storedMinutesMeanOf = [](const VoragoTest::SweepRecord& r) {
        return VoragoTest::meanOf(r.takes.front().minutes);
    };
    const auto compOf = [&defs](std::size_t p) {
        std::vector<std::uint8_t> comp;
        std::string why;
        const bool built = VoragoTest::buildPresetComponentState(defs[p], comp, why);
        INFO(defs[p].name << ": " << why);
        REQUIRE(built);
        return comp;
    };

    // ---- (a) level twin, every preset: d < 0.05 ----------------------------------------
    std::printf("\n(a) level twin (bound < %.2f):\n", VoragoTest::kLevelTwinBound);
    for (const VoragoTest::SweepRecord& r : factory) {
        std::printf("  %-28s d %.6f\n", r.name.c_str(), r.levelTwinD);
        INFO(r.name << ": level twin d " << r.levelTwinD);
        CHECK(r.levelTwinD < VoragoTest::kLevelTwinBound);
    }

    // ---- (a') gain twin, highest-Pressure preset: d <= 2.0 ----------------------------
    {
        std::size_t hp = 0;  // ties -> the first record (as controlSet)
        for (std::size_t p = 1; p < n; ++p) {
            if (factory[p].storedPressure > factory[hp].storedPressure) {
                hp = p;
            }
        }
        const VoragoTest::SweepRecord& r = factory[hp];
        const std::vector<std::uint8_t> comp = compOf(hp);
        const std::optional<double> d =
            VoragoTest::gainTwinD(comp, r.tl, storedSeedOf(r), storedMinutesMeanOf(r));
        std::printf("\n(a') gain twin on %s (Pressure %.4f): d %s%.4f (bound <= %.2f)\n",
                    r.name.c_str(), r.storedPressure, d.has_value() ? "" : "<render failed> ",
                    d.value_or(0.0), VoragoTest::kSeedTwinMargin);
        std::fflush(stdout);
        INFO(r.name << ": gain twin (FR-017 stop on failure)");
        REQUIRE(d.has_value());
        CHECK(*d <= VoragoTest::kSeedTwinMargin);
    }

    // ---- (b) seed twins t_K <= 2.0 and (c) sub twins d < 4.0, per C preset ------------
    double tMax = 0.0;
    std::printf("\n(b) seed twins (t_K <= %.2f) and (c) sub twins (d < %.2f):\n",
                VoragoTest::kSeedTwinMargin, VoragoTest::kFloorF);
    for (const std::size_t m : cs.members) {
        const VoragoTest::SweepRecord& r = factory[m];
        const std::vector<std::uint8_t> comp = compOf(m);
        const std::optional<double> tK =
            VoragoTest::seedTwinTK(comp, r.tl, storedSeedOf(r), K, r.mean, threads);
        const VoragoTest::SubTwinResult sub =
            VoragoTest::subTwinD(comp, r.tl, storedSeedOf(r), storedMinutesMeanOf(r), threads);
        std::printf("  %-28s t_K %s%.4f  |  sub -0.125: ", r.name.c_str(),
                    tK.has_value() ? "" : "<render failed> ", tK.value_or(0.0));
        if (sub.minusRun) {
            std::printf("d %.4f", sub.dMinus);
        } else {
            std::printf("not run (leaves [0, 1])");
        }
        std::printf("  sub +0.125: ");
        if (sub.plusRun) {
            std::printf("d %.4f", sub.dPlus);
        } else {
            std::printf("not run (leaves [0, 1])");
        }
        std::printf("  [%s]\n", sub.ok ? "ok" : "FAIL");
        std::fflush(stdout);
        {
            INFO(r.name << ": seed twin t_K (FR-017 stop on failure)");
            CHECK(tK.has_value());
            if (tK.has_value()) {
                CHECK(*tK <= VoragoTest::kSeedTwinMargin);
                tMax = std::max(tMax, *tK);
            }
        }
        {
            INFO(r.name << ": sub twin minus run " << sub.minusRun << " d " << sub.dMinus
                        << ", plus run " << sub.plusRun << " d " << sub.dPlus
                        << " (FR-017 stop on failure)");
            CHECK(sub.ok);
        }
    }

    // ---- Every pair: d(P, Q) >= max(4.0, 2 t_max) ------------------------------------
    // Sweep ruling 2026-09-30: the 2 max(s(P), s(Q)) term is RECORDED (printed
    // below), not gated - D(P) and D(Q) are K-take, three-minute means whose noise
    // is the take term, not either preset's own minute-to-minute evolution (the
    // same reading as gate G2's twin bars). The first sweep read floors up to
    // 13.7 from that term on evolving presets, 216 of its 313 failing pairs above 4.0.
    std::printf("\ns(P) per preset:\n");
    for (const VoragoTest::SweepRecord& r : factory) {
        std::printf("  %-28s s(P) %.4f\n", r.name.c_str(), r.selfDistance);
    }
    const double baseFloor = std::max(VoragoTest::kFloorF, 2.0 * tMax);
    const auto pairFloor = [baseFloor](std::size_t, std::size_t) { return baseFloor; };
    std::vector<double> ds;
    ds.reserve(n * (n - 1u) / 2u);
    double minD = 0.0;
    std::size_t minP = 0;
    std::size_t minQ = 1;
    std::size_t failures = 0;
    for (std::size_t p = 0; p < n; ++p) {
        for (std::size_t q = p + 1u; q < n; ++q) {
            const double d = VoragoTest::descriptorDistance(factory[p].mean, factory[q].mean);
            const double pf = pairFloor(p, q);
            if (ds.empty() || d < minD) {
                minD = d;
                minP = p;
                minQ = q;
            }
            ds.push_back(d);
            if (!(d >= pf)) {
                ++failures;
                std::printf("  BELOW FLOOR: %s vs %s: d %.4f < floor %.4f\n",
                            factory[p].name.c_str(), factory[q].name.c_str(), d, pf);
            }
            INFO(factory[p].name << " vs " << factory[q].name << ": d " << d << ", floor "
                                 << pf);
            CHECK(d >= pf);
        }
    }
    std::vector<double> sorted = ds;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t half = sorted.size() / 2u;
    const double median = (sorted.size() % 2u == 1u) ? sorted[half]
                                                     : 0.5 * (sorted[half - 1u] + sorted[half]);
    std::printf("\nPairs %zu: min d %.4f (%s vs %s, floor %.4f)  median d %.4f  max d %.4f\n",
                sorted.size(), sorted.front(), factory[minP].name.c_str(),
                factory[minQ].name.c_str(), pairFloor(minP, minQ), median, sorted.back());
    std::printf("t_max %.4f  K %d  effective floor max(F, 2 t_max) = %.4f (s(P) recorded, "
                "not gated - ruling 2026-09-30)  pairs below floor: %zu\n",
                tMax, K, baseFloor, failures);
    std::fflush(stdout);
}
