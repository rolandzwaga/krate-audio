export const meta = {
  name: 'membrum-signal-path',
  description: 'Full signal-path audit of the Membrum plugin: stage-by-stage bug hunt + dynamics/sameness diagnostic + scientific-correctness research against the literature, adversarially verified, synthesized into a prioritized report',
  whenToUse: 'After the DSP-correctness pass, to find anything wrong across the COMPLETE chain (param->exciter->body->layers->shaper->voice->pool->master), verify the physical model against published acoustics/DSP literature, and diagnose why presets sound the same/boring.',
  phases: [
    { title: 'Find', detail: 'one agent per signal-path stage + cross-cutting lenses' },
    { title: 'Research', detail: 'verify each physical claim against scientific literature' },
    { title: 'Verify', detail: 'independent skeptics try to refute each code finding' },
    { title: 'Synthesize', detail: 'dedup, prioritize, propose concrete fixes' },
  ],
}

// ---------------------------------------------------------------------------
// Shared context handed to every agent so they judge against the right rules.
// ---------------------------------------------------------------------------
const CONTEXT = `
You are auditing the Membrum VST3 drum-synth plugin (physical modelling, modal
synthesis) in the repo at f:/projects/iterum. This is a READ-ONLY analysis task:
DO NOT edit any files. Read source and tests, reason about correctness.

Real-time audio rules (violations are bugs): no allocation/lock/IO on the audio
thread; denormals must be flushed; NaN/Inf must not propagate; parameters at the
VST boundary are normalized [0,1] and must be denormalized before use.

THE FULL PER-VOICE SIGNAL PATH (plugins/membrum/src/dsp/drum_voice.h):
  param denorm -> pitch-env/tension (control) -> material-morph
  -> ExciterBank (+ ClickLayer) -> BodyBank (modal resonator bank)
  -> 1/sqrt(N) output normalize + gain compensation
  -> secondary shell bank (Phase 8D head<->shell coupling)
  -> NoiseLayer (parallel) -> UnnaturalZone (modeInject -> nonlinearCoupling)
  -> ToneShaper (drive -> wavefolder -> DCblocker -> SVF)
  -> amp envelope x level -> softClip([-1,1])
There are TWO block paths: processBlockFast (block-rate modulation refresh, SIMD
body) and processBlockSlow (per-sample, used only for FeedbackExciter). They are
supposed to be perceptually equivalent.

POOL + MASTER (voice_pool.cpp, processor.cpp ~line 698 process()):
  VoicePool: allocation, stealing, choke groups, per-pad dispatch, voice
  summation, multi-bus output routing, pan.
  Master: coupling engine (mono sum -> delay -> engine -> energy limiter ->
  add to L/R), master gain, peak meter, silence flags.

USER-REPORTED SYMPTOM (important): the factory presets "mostly sound the same
and boring" despite different parameter values. Treat that as a diagnostic clue
pointing at the path, not just the presets: look for stages that flatten dynamics
or homogenize timbre (e.g. the per-voice softClip on shaped*env*level, the master
energy limiter, the 1/sqrt(N) normalize + gain compensation interacting, any
clamp/normalize that collapses parameter range), and for parameters that fail to
produce an audible change because their effect is cancelled, clamped to a narrow
range, or never reaches the DSP.
`

const FINDING_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['findings'],
  properties: {
    findings: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['title', 'severity', 'file', 'line', 'problem', 'why_wrong', 'sound_impact'],
        properties: {
          title: { type: 'string', description: 'one-line summary' },
          severity: { type: 'string', enum: ['critical', 'high', 'medium', 'low'] },
          file: { type: 'string' },
          line: { type: 'string', description: 'line number or range, e.g. "446" or "446-449"' },
          problem: { type: 'string', description: 'what the code does that is wrong' },
          why_wrong: { type: 'string', description: 'the correct behaviour and why this deviates (cite DSP/physical reasoning)' },
          sound_impact: { type: 'string', description: 'concrete audible consequence; does it contribute to presets sounding the same/boring?' },
        },
      },
    },
  },
}

const VERDICT_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['is_real', 'confidence', 'corrected_severity', 'reasoning'],
  properties: {
    is_real: { type: 'boolean', description: 'true only if you confirmed the bug by reading the actual code' },
    confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
    corrected_severity: { type: 'string', enum: ['critical', 'high', 'medium', 'low', 'not-a-bug'] },
    reasoning: { type: 'string', description: 'what you read and why the finding holds or fails' },
  },
}

// Scientific-correctness verdict for a physical claim made by the code.
const RESEARCH_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['claim_as_coded', 'code_ref', 'verdict', 'evidence_quality', 'correct_form', 'sources', 'implication'],
  properties: {
    claim_as_coded: { type: 'string', description: 'the actual formula / constants / topology found IN THE CODE (read it, do not guess)' },
    code_ref: { type: 'string', description: 'file:line where the claim lives' },
    verdict: { type: 'string', enum: ['confirmed', 'contradicted', 'oversimplified', 'unsupported', 'nuanced'] },
    evidence_quality: { type: 'string', enum: ['strong', 'moderate', 'weak'], description: 'strong only when backed by a textbook/peer-reviewed paper, not just blogs/forums' },
    correct_form: { type: 'string', description: 'what the literature says it SHOULD be (values/formula), if different' },
    sources: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['title', 'type', 'says'],
        properties: {
          title: { type: 'string' },
          type: { type: 'string', enum: ['textbook', 'paper', 'thesis', 'docs', 'blog', 'forum'] },
          url: { type: 'string' },
          says: { type: 'string', description: 'the specific thing this source establishes' },
        },
      },
    },
    implication: { type: 'string', description: 'if wrong/oversimplified, the audible consequence and whether it could cause the boring/same-sounding symptom' },
  },
}

const CLAIM_LIST_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: ['claims'],
  properties: {
    claims: {
      type: 'array',
      items: {
        type: 'object',
        additionalProperties: false,
        required: ['title', 'files', 'question'],
        properties: {
          title: { type: 'string' },
          files: { type: 'string', description: 'where the claim lives' },
          question: { type: 'string', description: 'the physical question to verify against literature' },
        },
      },
    },
  },
}

// ---------------------------------------------------------------------------
// CODE-BUG stages. Each is one finder agent. Cross-cutting lenses are stages
// too so they fan out in the same phase.
// ---------------------------------------------------------------------------
const STAGES = [
  {
    key: 'params',
    title: 'Parameter plumbing & denorm',
    files: 'plugins/membrum/src/processor/processor.cpp (processParameterChanges + the denormalize helper near line 51), plugins/membrum/src/dsp/pad_config.h, plugins/membrum/src/dsp/voice_common_params.h, plugins/membrum/src/processor/macro_mapper.cpp/.h',
    focus: 'Every host parameter is denormalized with the correct range and reaches the matching DrumVoice setter. Hunt for: params dropped, overwritten, mapped to a too-narrow effective range, wrong default, or a macro that collapses several params into near-identical values across presets.',
  },
  {
    key: 'exciters',
    title: 'Exciters + ClickLayer',
    files: 'plugins/membrum/src/dsp/exciter_bank.h, plugins/membrum/src/dsp/exciters/*.h (impulse, mallet, fm_impulse, friction, noise_burst, feedback), plugins/membrum/src/dsp/click_layer.h, plugins/membrum/src/dsp/exciter_type.h',
    focus: 'Velocity mapping, excitation energy/spectrum per exciter type, deferred type-swap correctness, the click routing (half into excitation, full into output), and whether exciter-selection actually changes the excitation. Does each exciter inject a distinct enough signal, or do they converge?',
  },
  {
    key: 'bodies',
    title: 'Modal bodies & mode mapping',
    files: 'plugins/membrum/src/dsp/body_bank.h, plugins/membrum/src/dsp/bodies/*.h (membrane, plate, shell, bell, noise, string + their mappers), plugins/membrum/src/dsp/*_modes.h, plugins/membrum/src/dsp/bodies/natural_fundamental.h, and dsp/include/krate/dsp/processors/modal_resonator_bank.h',
    focus: 'Mode frequency/amplitude/damping mapping from Material/Size/Decay/StrikePos. The 1/sqrt(N) output normalize + bodyGainCompensation interaction. CRITICAL for the sameness symptom: do Material/Size/Decay/StrikePos actually move the mode set audibly, or are their effects small/clamped/normalized away?',
  },
  {
    key: 'secondary',
    title: 'Secondary shell bank & coupling (8D)',
    files: 'plugins/membrum/src/dsp/drum_voice.h (configureSecondaryBank, stabilityClampedCoupling, the secondary mix in processBlockFast), plugins/membrum/src/dsp/coupling_matrix.h',
    focus: 'Passivity/stability of head<->shell coupling, the 0.25 coupling clamp, shell damping range, gain of the mixed shell output. Is the secondary bank ever audible, or always swamped/decayed before it matters?',
  },
  {
    key: 'layers',
    title: 'NoiseLayer + UnnaturalZone',
    files: 'plugins/membrum/src/dsp/noise_layer.h, plugins/membrum/src/dsp/unnatural/*.h (mode_inject, nonlinear_coupling, material_morph, mode_stretch, decay_skew, unnatural_zone)',
    focus: 'NoiseLayer filter mode/standalone gain/mix scaling. UnnaturalZone amount==0 early-out (must be bit-identical to default). When enabled, does modeInject / nonlinearCoupling produce an audible, parameter-proportional change, or is it inaudible/over-clamped?',
  },
  {
    key: 'toneshaper',
    title: 'ToneShaper + pitch envelope',
    files: 'plugins/membrum/src/dsp/tone_shaper.h, plugins/membrum/src/dsp/pitch_segment_envelope.h',
    focus: 'Order drive->wavefolder->DCblocker->SVF, bypass equivalence to the default path, SVF coefficient updates, pitch-envelope Hz feeding the body. Does the drive/fold stage crush dynamics or push every preset toward the same saturated timbre?',
  },
  {
    key: 'voice-output',
    title: 'Voice mix, amp env & final softClip',
    files: 'plugins/membrum/src/dsp/drum_voice.h (process(), processBlockFast, processBlockSlow, the softClip(shaped*env*level) line, ampEnvelope setup in prepare())',
    focus: 'PRIME SAMENESS SUSPECT. Does softClip on every voice flatten loud hits so dynamics/timbre converge? Is the amp envelope (attack 0.5ms, sustain 1.0, release 300ms) shape fixed regardless of preset, making every hit have the same contour? Verify processBlockFast vs processBlockSlow produce equivalent output.',
  },
  {
    key: 'voicepool',
    title: 'VoicePool: alloc, steal, choke, mix, routing, pan',
    files: 'plugins/membrum/src/voice_pool/voice_pool.cpp/.h, plugins/membrum/src/voice_pool/*.h (voice_meta, voice_stealing_policy, choke_group_table)',
    focus: 'Voice summation gain (do N stacked voices clip or stay flat?), per-pad dispatch correctness, choke/steal click-freeness, multi-bus output routing and pan law. Any place voices are summed then hard-limited in a way that homogenizes the mix.',
  },
  {
    key: 'master',
    title: 'Master bus: coupling engine, energy limiter, master gain, meter',
    files: 'plugins/membrum/src/processor/processor.cpp process() ~lines 698-820 (coupling chain, applyEnergyLimiter, master gain, silence flags), couplingEngine + couplingDelay members',
    focus: 'PRIME SAMENESS SUSPECT. applyEnergyLimiter: does it flatten transients/dynamics across the whole mix? Coupling-engine stability and gain. Master-gain range (-24..+12 dB). Meter observing post-master. Does the limiter make every preset settle to the same loudness/spectrum?',
  },
  {
    key: 'gain-staging',
    title: 'CROSS-CUTTING: end-to-end gain staging',
    files: 'Trace amplitude from exciter output through body, normalize, layers, shaper, env, voice softClip, voice sum, master limiter, master gain. Read drum_voice.h, voice_pool.cpp, processor.cpp.',
    focus: 'Follow a unit-velocity hit numerically through every multiply/clamp. Find double-scaling, a stage that loses most of the level, a clip reached too early, or compensations that cancel a parameter. Where does dynamic range get crushed?',
  },
  {
    key: 'sameness',
    title: 'CROSS-CUTTING: why do presets sound the same?',
    files: 'plugins/membrum/src/preset/membrum_preset_config.h, default_kit.h, and any preset data; cross-reference with the per-voice param->DSP mapping in drum_voice.h and the bodies/exciters.',
    focus: 'Enumerate the user-facing parameters and rate each: STRONG / WEAK / NONE audible effect, with the code reason. Identify the smallest set of path bugs that would explain "different presets, similar boring sound". This is a diagnosis, not just a bug list -- still emit findings for each weak/dead parameter.',
  },
]

// ---------------------------------------------------------------------------
// SCIENTIFIC-CORRECTNESS topics. Each agent reads the code to get the ACTUAL
// numbers/formula, then verifies against published acoustics / DSP literature.
// ---------------------------------------------------------------------------
const RESEARCH_TOPICS = [
  {
    key: 'membrane-modes',
    title: 'Circular membrane mode ratios',
    files: 'plugins/membrum/src/dsp/membrane_modes.h, plugins/membrum/src/dsp/bodies/membrane_mapper.h',
    question: 'Do the coded mode-frequency ratios match the Bessel-zero ratios of an ideal circular membrane (1.000, 1.594, 2.136, 2.296, 2.653, 2.918, ...) and, for a real drumhead, the air-loaded/tensioned ratios reported in Fletcher & Rossing and Rossing\'s timpani/membrane work? Are the modal amplitudes and strike-position weighting (Bessel-mode shapes) physically sensible?',
  },
  {
    key: 'plate-modes',
    title: 'Plate / cymbal mode ratios',
    files: 'plugins/membrum/src/dsp/bodies/plate_modes.h, plugins/membrum/src/dsp/bodies/plate_mapper.h',
    question: 'Do the plate mode ratios match thin-plate (Kirchhoff/Chladni) theory and measured cymbal/gong spectra (Fletcher & Rossing; Bilbao plate models)? Is the inharmonic spacing realistic or too regular?',
  },
  {
    key: 'shell-bell-modes',
    title: 'Shell (free-free beam) & bell mode ratios',
    files: 'plugins/membrum/src/dsp/bodies/shell_modes.h, plugins/membrum/src/dsp/bodies/bell_modes.h, the configureSecondaryBank shell ratios in drum_voice.h',
    question: 'Are the shell ratios actually the free-free Euler-Bernoulli beam eigenvalue ratios (the code claims this)? Do the bell ratios match published bell/idiophone partial series (hum, prime, tierce, quint, nominal)? Is "shell f0 ~= 0.6 * f_head" a real rule of thumb?',
  },
  {
    key: 'damping-law',
    title: 'Modal damping law R = b1 + b3*f^2',
    files: 'dsp/include/krate/dsp/processors/modal_resonator_bank.h (DampingLaw), drum_voice.h damping comments',
    question: 'Is the frequency-dependent damping R_k = b1 + b3*f^2 the form given by Chaigne (1993) / Chaigne & Kergomard / Bilbao for plates and membranes? Are the b1/b3 ranges used (and the t60 they imply) physically plausible for drums? Is per-mode damping (higher modes decaying faster) correctly modelled?',
  },
  {
    key: 'normalization',
    title: '1/sqrt(N) energy-equalization output norm',
    files: 'plugins/membrum/src/dsp/drum_voice.h noteOn (setOutputGain 1/sqrt(N)) and the bodyGainCompensation logic',
    question: 'Is 1/sqrt(N) the correct energy-equalization normalization for summing N uncorrelated modal contributions, and is it appropriate here given the modes start IN PHASE at a strike (so the transient peak ~ sum, not sqrt-sum)? Could this norm be flattening per-preset loudness/timbre differences? Reference Faust physmodels.lib (:>/(nModes)), STK Modal, Smith/CCRMA.',
  },
  {
    key: 'coupling-stability',
    title: 'Head<->shell coupling stability criterion',
    files: 'plugins/membrum/src/dsp/drum_voice.h (coupling^2 * 0.5 * Q_body * Q_shell < 1 reasoning, 0.25 clamp), coupling_matrix.h',
    question: 'Is the passivity/stability argument for the body<->shell coupling sound (Karjalainen et al.; Bilbao on coupled modal systems)? Is dropping the feedback path (feedforward-only body->shell) the right call, or does real drum head/shell coupling require a bidirectional model? Does feedforward-only lose audible character?',
  },
  {
    key: 'tension-modulation',
    title: 'Nonlinear tension modulation / pitch glide',
    files: 'plugins/membrum/src/dsp/drum_voice.h (tensionPitchMod = 1 + amt*energyEnv, energy ~ velocity^2, 20ms follower)',
    question: 'Does the energy-driven pitch-up-then-relax model match Avanzini et al. "Efficient synthesis of tension modulation... based on energy estimation" (JASA 2012) and observed tom/timpani pitch glide? Is "energy proportional to velocity^2" and the resulting <=2 semitone shift physically calibrated?',
  },
  {
    key: 'strike-position',
    title: 'Strike-position spectral weighting',
    files: 'plugins/membrum/src/dsp/bodies/membrane_mapper.h and other mappers (strikePos -> mode amplitudes)',
    question: 'Does strike position weight the mode amplitudes by the mode shape at the strike point (comb-like suppression of modes with a node at the strike, e.g. sin(n*pi*x) for 1D, Bessel mode shape for the membrane)? Is the physical "hit the center vs the edge" timbre change correctly reproduced?',
  },
  {
    key: 'exciter-physics',
    title: 'Exciter models (mallet/impulse/friction/feedback)',
    files: 'plugins/membrum/src/dsp/exciters/*.h',
    question: 'Are the contact/impulse models physically grounded (Hertzian contact / nonlinear mallet-membrane contact a la Avanzini & Rocchesso; bowed-friction stick-slip a la McIntyre-Schumacher-Woodhouse for the friction exciter)? Is the velocity->force->spectrum mapping realistic?',
  },
  {
    key: 'softclip-tanh',
    title: 'Output soft-clip as gain stage',
    files: 'dsp/include/krate/dsp/core/ (softClip) and its use in drum_voice.h output',
    question: 'Is putting a per-voice tanh-style soft-clip on the final modal output defensible, or is it (as the code comments suspect) gain-staging masquerading as safety that crushes dynamics? What do STK/Faust/Bilbao do instead at the output of a modal bank?',
  },
]

phase('Find')

// ---- Code-bug pipeline: find -> verify (per finding, 2 lenses). -----------
const codePromise = pipeline(
  STAGES,
  (s) => agent(
    `${CONTEXT}\n\nWhat counts as a finding: signal lost/zeroed; double-scaling or gain collapse; sign/order-of-operations error; a parameter that does not reach or audibly affect the DSP; fast-vs-slow path divergence; denormal/NaN risk; broken bypass/default equivalence; stability/passivity violation; a clamp/normalize that crushes dynamic range or timbral variation.\n\n=== YOUR STAGE: ${s.title} ===\nFiles to read: ${s.files}\n\nFocus: ${s.focus}\n\nRead the listed source AND the matching tests under plugins/membrum/tests/. Report every concrete problem you can substantiate with file:line. Prefer fewer, well-evidenced findings over speculation. If the stage is clean, return an empty findings array.`,
    { label: `find:${s.key}`, phase: 'Find', schema: FINDING_SCHEMA },
  ),
  (review, s) => {
    const findings = (review && review.findings) || []
    if (findings.length === 0) return []
    return parallel(findings.flatMap((f) => {
      const base = `${CONTEXT}\n\nA prior agent reported this finding in the "${s.title}" stage. Independently verify it by READING THE ACTUAL CODE at ${f.file}:${f.line}. Default to is_real=false unless the code clearly confirms it.\n\nFINDING: ${f.title}\nProblem: ${f.problem}\nWhy claimed wrong: ${f.why_wrong}\nClaimed sound impact: ${f.sound_impact}`
      return [
        () => agent(`${base}\n\nLENS: CORRECTNESS. Is the described code behaviour actually present and actually wrong?`,
          { label: `verify:${s.key}:correct`, phase: 'Verify', schema: VERDICT_SCHEMA })
          .then((v) => ({ finding: f, stage: s.key, lens: 'correctness', verdict: v })),
        () => agent(`${base}\n\nLENS: AUDIBILITY. Even if the code is as described, would it produce an audible effect / contribute to the sameness symptom? A "bug" with zero audible impact is corrected_severity=low or not-a-bug.`,
          { label: `verify:${s.key}:audible`, phase: 'Verify', schema: VERDICT_SCHEMA })
          .then((v) => ({ finding: f, stage: s.key, lens: 'audibility', verdict: v })),
      ]
    }))
  },
)

// ---- Research pipeline (runs concurrently with the code pipeline). --------
// Each curated topic is researched; a completeness agent then surfaces any
// physical claims the curated list missed, and those are researched too.
const researchPromise = (async () => {
  const curated = await parallel(RESEARCH_TOPICS.map((t) => () =>
    agent(
      `${CONTEXT}\n\n=== SCIENTIFIC-CORRECTNESS REVIEW ===\nTopic: ${t.title}\nFiles to read FIRST (extract the actual coded values/formula -- do NOT guess them): ${t.files}\n\nQuestion to answer against the literature: ${t.question}\n\nMethod: (1) read the code and state precisely what it does. (2) Use web search + fetch to find authoritative sources. WEIGHT SOURCE QUALITY: textbooks (Fletcher & Rossing "The Physics of Musical Instruments", Bilbao "Numerical Sound Synthesis", Chaigne & Kergomard "Acoustics of Musical Instruments") and peer-reviewed papers are ground truth; CCRMA/Smith and Faust/STK docs are strong; blogs and forums are corroboration only and can NEVER be the sole basis for a "contradicted" verdict. (3) Decide if the code matches, contradicts, oversimplifies, or is nuanced vs reality, and give the correct form with citations. (4) State the audible implication and whether it could contribute to presets sounding generic/same.`,
      { label: `research:${t.key}`, phase: 'Research', schema: RESEARCH_SCHEMA },
    )
  ))

  const gaps = await agent(
    `${CONTEXT}\n\nWe have already research-verified these physical topics: ${RESEARCH_TOPICS.map((t) => t.title).join('; ')}.\n\nScan the modelling code (plugins/membrum/src/dsp/bodies/*, exciters/*, unnatural/*, tone_shaper.h, and dsp/include/krate/dsp/processors/modal_resonator_bank.h) for ANY OTHER physical/acoustic claim, constant, formula, or topology -- cited or uncited -- that is NOT covered by the list above and is worth verifying against literature. Return up to 8 of the highest-value ones.`,
    { label: 'research:gaps', phase: 'Research', schema: CLAIM_LIST_SCHEMA },
  )

  const extra = await parallel(((gaps && gaps.claims) || []).slice(0, 8).map((c, i) => () =>
    agent(
      `${CONTEXT}\n\n=== SCIENTIFIC-CORRECTNESS REVIEW (gap-fill) ===\nClaim: ${c.title}\nWhere: ${c.files}\nVerify: ${c.question}\n\nSame method and source-quality weighting as the curated topics: read the code for the actual values, verify against textbooks/papers (strong) with blogs/forums as corroboration only.`,
      { label: `research:extra:${i}`, phase: 'Research', schema: RESEARCH_SCHEMA },
    )
  ))

  return [...curated, ...extra].filter(Boolean)
})()

const [perStage, researchResults] = await Promise.all([codePromise, researchPromise])

// ---- Collapse code findings: keep where the correctness lens confirms. -----
const verified = perStage
  .flat()
  .filter(Boolean)
  .reduce((acc, r) => {
    const k = `${r.finding.file}:${r.finding.line}:${r.finding.title}`
    if (!acc[k]) acc[k] = { finding: r.finding, stage: r.stage, verdicts: [] }
    acc[k].verdicts.push({ lens: r.lens, ...r.verdict })
    return acc
  }, {})

const confirmed = Object.values(verified).filter((e) => {
  const correctness = e.verdicts.find((v) => v.lens === 'correctness')
  return correctness && correctness.is_real && correctness.corrected_severity !== 'not-a-bug'
})

// Physics problems = anything not cleanly confirmed.
const physicsProblems = researchResults.filter((r) => r && r.verdict !== 'confirmed')

log(`Confirmed ${confirmed.length} code findings; ${physicsProblems.length}/${researchResults.length} physical claims flagged`)

phase('Synthesize')

const report = await agent(
  `${CONTEXT}\n\nYou are the synthesizer. You receive (A) confirmed, independently-verified CODE findings and (B) SCIENTIFIC-CORRECTNESS verdicts on the physical model. Produce a prioritized engineering report:\n\n1. EXECUTIVE SUMMARY: the 3-5 root causes most likely behind "presets sound the same and boring", drawing on BOTH code bugs and physics mismatches.\n2. CODE FINDINGS BY SEVERITY (critical -> low): file:line, the bug, the concrete fix, expected audible improvement.\n3. SCIENTIFIC-CORRECTNESS FINDINGS: for each contradicted/oversimplified/nuanced physical claim -- what the code does, what the literature says (cite sources, note evidence quality), the correct form, and the audible consequence. Separate "the math is wrong" from "the math is right but wired up wrong".\n4. SAMENESS DIAGNOSIS: the chain of stages that flatten dynamics / homogenize timbre + parameters with weak/dead audible effect, ordered by how much fixing them restores variety.\n5. SUGGESTED FIX ORDER: smallest-change-highest-impact first; flag which fixes must land BEFORE presets are re-tuned (since re-tuning against a wrong model wastes the effort).\n\nBe concrete and cite file:line and sources throughout. Do not invent items not present in the data.\n\n(A) CONFIRMED CODE FINDINGS:\n${JSON.stringify(confirmed.map((c) => ({ stage: c.stage, ...c.finding, verdicts: c.verdicts })), null, 2)}\n\n(B) SCIENTIFIC-CORRECTNESS VERDICTS:\n${JSON.stringify(researchResults, null, 2)}`,
  { label: 'synthesize', phase: 'Synthesize' },
)

return {
  confirmedCodeFindings: confirmed.length,
  physicalClaimsChecked: researchResults.length,
  physicalClaimsFlagged: physicsProblems.length,
  report,
  confirmed,
  research: researchResults,
}
