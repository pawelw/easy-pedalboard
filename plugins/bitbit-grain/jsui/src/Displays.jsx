import { useEffect, useRef } from "react";
import { useJuceSliderValue, useParamId, useFormattedText } from "@synthpeak/pedal-ui/juce";
import { useModAssignment } from "./ModRouting.jsx";
import { useLfoValue } from "./LfoPlayback.jsx";
import { grainEnvelopePath, familyEnvelopePath, smoothForShape, FAMILY_TRIANGLE } from "./envelopeShape.js";

/**
 * The five recessed displays across the plate (COMPONENTS.md's "Displays"
 * table) plus the full-width scope strip. Random/Reverb are decorative and
 * static, matching the design_handoff mock exactly; Grain and Pitch Weights
 * both read live parameter values.
 */

// The Size range, mirroring GrainerConfig.h's kMinGrainMs/kMaxGrainMs. Only
// GrainEnvelope uses these, to turn the printed duration back into a width.
const SIZE_MIN_MS = 30;
const SIZE_MAX_MS = 1000;

function clampMs(ms) {
  return Math.min(SIZE_MAX_MS, Math.max(SIZE_MIN_MS, ms));
}

/** A parameter's live modulated 0..1, mirroring PluginProcessor.cpp's own
    modulatedValue() exactly - base01 + depth * lfoValue, clamped. `assignment`
    is `useModAssignment(paramId)`'s result, `undefined` when the parameter
    carries no assignment, in which case this is the identity (the base value,
    untouched). Shared by every display below that reads a modulation target,
    so the DSP-matching formula lives in exactly one place. */
function resolveModulated(base01, assignment, lfoValue) {
  return assignment ? Math.min(1, Math.max(0, base01 + assignment.depth * lfoValue)) : base01;
}

/** Milliseconds out of a readout this codebase printed - "240 ms" or "1.20 s",
    the two shapes BitBitGrainProcessor::sizeReadout() emits. Returns null for
    anything else, including the empty string a page with no JUCE backend
    behind it gets back, so the caller can fall back rather than draw nonsense. */
function parseDurationMs(text) {
  const match = /^\s*([\d.]+)\s*(ms|s)\s*$/.exec(text || "");
  if (match == null) return null;

  const value = Number(match[1]);
  if (! Number.isFinite(value)) return null;

  return match[2] === "s" ? value * 1000 : value;
}

/** Mirrors ee::dsp::kGrainDensityDivisions (shared/include/ee/dsp/
    GrainSyncMap.h) - only the `beats` column, since only how many of a
    division fit in a bar is drawn here, never its label. */
const GRAIN_SYNC_BEATS = [1 / 32, 1 / 24, 1 / 16, 1 / 12, 0.125, 1 / 6, 0.25, 0.375, 1 / 3, 0.5, 0.75, 2 / 3, 1, 1.5, 4 / 3, 2, 3, 4, 6];

/** Which of GrainSyncMap's tempo divisions Destiny would land on if Sync were
    on. Density is a *rate* map there (knob up = faster), so the fastest,
    shortest division sits at the top of the knob's travel - the reverse of a
    duration knob - see GrainSyncMap::divisionIndex's own note. */
function grainDivisionBeats(density01) {
  const last = GRAIN_SYNC_BEATS.length - 1;
  const pick = 1 - density01;
  const index = Math.min(last, Math.max(0, Math.round(pick * last)));
  return GRAIN_SYNC_BEATS[index];
}

/** Grain's envelope curve: attack/decay lean driven by Size, Shape and
    Destiny (density) - the same three fields Grainer::updateDerived morphs
    off Shape (attack width, decay steepness), stretched by Size and overlaid
    with extra staggered copies for however many of Destiny's tempo division
    fit in a bar (a quarter note is "1/4", four of which fit in a bar of
    four beats, so it draws four - not a linear guess at Destiny's value).

    Feedback fades those trailing copies rather than adding to their count: it
    is how much of each pass comes back round (see GrainerConfig.h's FEEDBACK
    section), so at 0 the picture is one clean pass (the trailing copies scale
    to nothing) and turned up it is the same grain re-sung several times, each
    fainter than the last - one Feedback power per step from the lead grain.
    (The knob's position, not its dB gain: a true -22 dB repeat would not show
    at all.) The lead is drawn first (left), the way a delay's own repeat
    diagram reads: the struck note first, its repeats decaying away to the
    right of it. */
export function GrainEnvelope({ accent, family = FAMILY_TRIANGLE }) {
  const [shape] = useJuceSliderValue("shape");
  const [size] = useJuceSliderValue("size");
  const [density] = useJuceSliderValue("density");
  const [feedback01] = useJuceSliderValue("feedback");

  const shapeA = useModAssignment("shape");
  const sizeA = useModAssignment("size");
  const densityA = useModAssignment("density");
  const feedbackA = useModAssignment("feedback");

  // Width comes from the readout, not from the knob position. The readout is
  // clamped to the grain length the engine will actually apply (see
  // BitBitGrainProcessor::sizeReadout) - synced, the knob can select a division
  // far longer than kMaxGrainMs, and every position past that cap produces the
  // same grain. Drawing off the raw knob made the envelope go on widening
  // while the value sat still, which is a picture of a sound nothing is
  // making. Reading it back off the printed text is what guarantees the two
  // can never disagree - always off the BASE size, since the readout is the
  // unmodulated value: there is no live text equivalent for a modulated one.
  // A size assignment instead falls back to the raw 0..1 (GrainEnvelopeLive
  // below), same as this display already does with no backend to ask at all.
  // Logarithmic, because grain length reads to the ear as a ratio, not a
  // difference - 20 to 40 ms is the same step as 1 to 2 s.
  const sizeId = useParamId("size");
  const sizeMs = parseDurationMs(useFormattedText(sizeId, size));
  const sizeSpanBase =
    sizeMs == null ? size : Math.log(clampMs(sizeMs) / SIZE_MIN_MS) / Math.log(SIZE_MAX_MS / SIZE_MIN_MS);

  if (shapeA || sizeA || densityA || feedbackA)
    return (
      <GrainEnvelopeLive
        accent={accent}
        family={family}
        shape={shape}
        shapeA={shapeA}
        density={density}
        densityA={densityA}
        feedback01={feedback01}
        feedbackA={feedbackA}
        sizeSpanBase={sizeSpanBase}
        size={size}
        sizeA={sizeA}
      />
    );

  return (
    <GrainEnvelopeBody
      accent={accent}
      family={family}
      shape={shape}
      density={density}
      feedback01={feedback01}
      sizeSpan={sizeSpanBase}
    />
  );
}

/** GrainEnvelope's own live subscription to the LFO's per-frame output - see
    ModdableKnob.jsx's ModulatedKnob and FilterCurve's ModulatedFilterCurve for
    the same split, and why it is a separate component (useLfoValue()
    re-renders its subscriber every frame; an unmodulated display, the common
    case, should not pay for that). Family is not itself modulatable (a
    discrete window choice, not a knob a Mod row can target), so it only ever
    passes through here unchanged. */
function GrainEnvelopeLive({
  accent,
  family,
  shape,
  shapeA,
  density,
  densityA,
  feedback01,
  feedbackA,
  sizeSpanBase,
  size,
  sizeA,
}) {
  const lfoValue = useLfoValue();

  return (
    <GrainEnvelopeBody
      accent={accent}
      family={family}
      shape={resolveModulated(shape, shapeA, lfoValue)}
      density={resolveModulated(density, densityA, lfoValue)}
      feedback01={resolveModulated(feedback01, feedbackA, lfoValue)}
      // No live duration text to read a modulated size off (see GrainEnvelope
      // above) - substitutes the raw modulated 0..1 straight in, the same
      // fallback sizeSpanBase itself uses when there is no text to ask.
      sizeSpan={sizeA ? resolveModulated(size, sizeA, lfoValue) : sizeSpanBase}
    />
  );
}

/** The envelope's own drawing, given already-resolved 0..1 inputs (the base
    parameters, or their live modulated values - GrainEnvelope/GrainEnvelopeLive's
    own concern, not this component's). */
function GrainEnvelopeBody({ accent, family = FAMILY_TRIANGLE, shape, density, feedback01, sizeSpan }) {
  // Shape is also the swell, over the bottom of its travel only (see
  // GrainerConfig.h's SMOOTH section) - and only for Triangle. The other three
  // families have no swell blend of their own (see Grainer::envelopeOf's own
  // note), so Smooth plays no part in their path.
  const smooth = smoothForShape(shape);
  const grainCount = Math.min(8, Math.max(1, Math.round(4 / grainDivisionBeats(density))));
  const width = 70 + sizeSpan * 110; // longer Size = wider grain window
  // The grain's real length, for the swell's fixed-millisecond fade-in.
  const lengthMs = SIZE_MIN_MS * Math.pow(SIZE_MAX_MS / SIZE_MIN_MS, Math.min(1, Math.max(0, sizeSpan)));
  const spacing = grainCount > 1 ? (234 - width) / (grainCount - 1) : 0;
  const pathFor =
    family === FAMILY_TRIANGLE
      ? (x0) => grainEnvelopePath(x0, width, shape, smooth, lengthMs)
      : (x0) => familyEnvelopePath(x0, width, family, shape);

  return (
    <svg width="100%" height="48" viewBox="0 0 240 48" preserveAspectRatio="none" fill="none">
      {Array.from({ length: grainCount }, (_, i) => {
        const x0 = 6 + i * Math.max(spacing, 0);
        const stepsOut = i; // 0 at the lead (left), rising going right
        const isLead = stepsOut === 0;
        const decay = Math.pow(feedback01, stepsOut);
        return (
          <path
            key={i}
            d={pathFor(x0)}
            stroke={accent}
            strokeWidth="1.6"
            strokeLinecap="round"
            vectorEffect="non-scaling-stroke"
            fill={isLead ? accent : "none"}
            fillOpacity={isLead ? 0.1 : 0}
            opacity={isLead ? 1 : 0.35 * decay}
          />
        );
      })}
    </svg>
  );
}

// grainEnvelopePath itself moved to envelopeShape.js, shared with
// GrainShapeIcon.jsx (the Shape knob's own glyph) - see that file's note.

/** Three bars, heights from the Low/Unison/High pitch-weight parameters -
    the one display in this row that COMPONENTS.md documents as reactive. */
export function PitchWeights({ accent }) {
  const [low] = useJuceSliderValue("plow");
  const [unison] = useJuceSliderValue("puni");
  const [high] = useJuceSliderValue("phigh");

  const lowA = useModAssignment("plow");
  const uniA = useModAssignment("puni");
  const highA = useModAssignment("phigh");

  if (lowA || uniA || highA)
    return (
      <PitchWeightsLive accent={accent} low={low} lowA={lowA} unison={unison} uniA={uniA} high={high} highA={highA} />
    );

  return <PitchWeightsBody accent={accent} low={low} unison={unison} high={high} />;
}

/** PitchWeights's own live subscription to the LFO's per-frame output - see
    GrainEnvelopeLive's own note on why this split exists. */
function PitchWeightsLive({ accent, low, lowA, unison, uniA, high, highA }) {
  const lfoValue = useLfoValue();

  return (
    <PitchWeightsBody
      accent={accent}
      low={resolveModulated(low, lowA, lfoValue)}
      unison={resolveModulated(unison, uniA, lfoValue)}
      high={resolveModulated(high, highA, lfoValue)}
    />
  );
}

function PitchWeightsBody({ accent, low, unison, high }) {
  return (
    <div className="pg-weights">
      <div className="pg-weights__baseline" />
      {[low, unison, high].map((v, i) => (
        <div key={i} className="pg-weights__col">
          <div
            className="pg-weights__bar"
            // .pg-weights only has 31px of vertical room above its own
            // bottom padding (48px tall minus 8px top/9px bottom padding) -
            // 40 overshot that and poked a fully-dialled bar past the
            // panel's own top edge.
            style={{ height: `${Math.max(2, v * 30)}px`, background: accent }}
          />
        </div>
      ))}
    </div>
  );
}

// Fixed scatter - 11 grains, left percent/size/opacity straight off the mock
// (BitBit Grain - 1c.dc.html). `top` is numeric (percent, 50 = the centre
// line) rather than a string here, since RandomField scales each grain's
// distance from centre by the Stereo knob at render time.
const RANDOM_DOTS = [
  { left: "8%", top: 62, size: 5, opacity: 0.35 },
  { left: "16%", top: 34, size: 3, opacity: 0.5 },
  { left: "24%", top: 70, size: 3, opacity: 0.65 },
  { left: "33%", top: 46, size: 5, opacity: 0.8 },
  { left: "41%", top: 26, size: 3, opacity: 0.35 },
  { left: "49%", top: 58, size: 3, opacity: 0.5 },
  { left: "57%", top: 38, size: 5, opacity: 0.65 },
  { left: "64%", top: 74, size: 3, opacity: 0.8 },
  { left: "72%", top: 50, size: 3, opacity: 0.35 },
  { left: "80%", top: 30, size: 5, opacity: 0.5 },
  { left: "88%", top: 64, size: 3, opacity: 0.65 },
];

/** L/R stereo field: a centre line and a scatter of grains either side, each
    now driven by this section's own three knobs plus the Grain section's
    Wide. Stereo scales every grain's distance from the centre line (0
    collapses the whole field onto it, 1 is the full spread above) and, above
    its own 0, is the only thing that does - it takes priority over Wide
    outright, the same as Grainer::nextPan() does for the real audio. Only
    with Stereo fully closed does Wide take over that scaling instead, so the
    field collapses to the centre line only when both are at 0. Scatter sets
    how far and how fast the grains jitter - it is exactly the engine's own
    per-grain timing/position jitter, so 0 holds them still. Reverse is not
    drawn: a reversed grain looks and drifts like any other. Each grain is
    drawn with the same steep-attack/long-decay lean as GrainEnvelope above -
    a fat rounded head tapering to a thin tail - rather than a plain dot, so
    the two displays read as the same shape.

    The jitter itself is a `requestAnimationFrame` loop rather than a CSS
    keyframe animation driven by custom properties: a CSS animation re-reads
    its `to` value (and duration) live, but re-evaluates it at the *same*
    elapsed-time fraction, so every tick of the Scatter knob snapped the
    drift to a new position - visible as the grains jumping rather than
    drifting. Accumulating each grain's own phase every frame and reading the
    latest knob values off a ref sidesteps that: a knob change can only ever
    bend the curve going forward, never relocate a point already drawn. */
export function RandomField({ accent }) {
  const [stereo] = useJuceSliderValue("stereo");
  const [scatter] = useJuceSliderValue("scatter");
  // Wide is not a modulation target (see GrainFace's own note on the "width"
  // knob), so this is the one plain, non-modulated read in the section.
  const [width] = useJuceSliderValue("width");

  const stereoA = useModAssignment("stereo");
  const scatterA = useModAssignment("scatter");

  if (stereoA || scatterA)
    return (
      <RandomFieldLive
        accent={accent}
        stereo={stereo}
        stereoA={stereoA}
        scatter={scatter}
        scatterA={scatterA}
        width={width}
      />
    );

  return <RandomFieldBody accent={accent} stereo={stereo} scatter={scatter} width={width} />;
}

/** RandomField's own live subscription to the LFO's per-frame output - see
    GrainEnvelopeLive's own note on why this split exists. Stacks on top of
    RandomFieldBody's own animation frame loop (the grains' jitter, a
    different and unrelated motion) rather than replacing it - this one only
    re-resolves the two knob-derived numbers the jitter loop and the layout
    below already read every frame; it does not touch how the jitter itself
    moves. */
function RandomFieldLive({ accent, stereo, stereoA, scatter, scatterA, width }) {
  const lfoValue = useLfoValue();

  return (
    <RandomFieldBody
      accent={accent}
      stereo={resolveModulated(stereo, stereoA, lfoValue)}
      scatter={resolveModulated(scatter, scatterA, lfoValue)}
      width={width}
    />
  );
}

function RandomFieldBody({ accent, stereo, scatter, width }) {
  const n = RANDOM_DOTS.length;
  const liveRef = useRef({ scatter });
  liveRef.current = { scatter };
  const dotRefs = useRef([]);

  useEffect(() => {
    const phase = RANDOM_DOTS.map((_, i) => (i / n) * Math.PI * 2);
    let last = performance.now();
    let raf = requestAnimationFrame(tick);

    function tick(now) {
      const dt = (now - last) / 1000;
      last = now;
      const { scatter } = liveRef.current;
      // Grainer::setScatter docs 0 as "metronomic, identical grains" - no
      // jitter at all, not just a small one, so this has no floor.
      const amplitude = scatter * 22; // px of drift travel
      const speed = 1.4 + scatter * 4; // radians/sec, faster jitter as Scatter rises

      for (let i = 0; i < n; i++) {
        phase[i] += speed * dt;
        const dx = Math.sin(phase[i]) * amplitude;
        const el = dotRefs.current[i];
        if (el) el.style.transform = `translate(-50%, -50%) translateX(${dx.toFixed(2)}px)`;
      }

      raf = requestAnimationFrame(tick);
    }

    return () => cancelAnimationFrame(raf);
  }, [n]);

  return (
    <div className="pg-field">
      <div className="pg-field__hline" />
      <span className="pg-field__side pg-field__side--l">L</span>
      <span className="pg-field__side pg-field__side--r">R</span>
      {RANDOM_DOTS.map((d, i) => {
        // Stereo takes priority the moment it is off 0, same as nextPan():
        // Wide only gets to scale the spread while Stereo is fully closed.
        const spread = stereo > 0 ? stereo : width;
        const top = 50 + (d.top - 50) * spread;
        const w = d.size * 2.6;
        const h = d.size * 1.5;
        return (
          <span
            key={i}
            ref={(el) => (dotRefs.current[i] = el)}
            className="pg-field__dot"
            style={{ left: d.left, top: `${top}%`, width: w, height: h, opacity: d.opacity }}
          >
            <svg viewBox="0 0 14 8" width="100%" height="100%">
              <path
                d="M1 4 C1 1.9 3.3 1 5.6 1 C9.6 1 13 2.5 13 4 C13 5.5 9.6 7 5.6 7 C3.3 7 1 6.1 1 4 Z"
                fill={accent}
              />
            </svg>
          </span>
        );
      })}
    </div>
  );
}

// The cloud lowpass's travel, mirroring GrainerConfig.h's CLOUD FILTER block.
// Only the lowpass is drawn: the highpass underneath is fixed and hidden by
// design, so putting it on the scope would advertise a control that is not
// there.
const CLOUD_LP_HZ = 13000;
// Mirrors GrainerConfig.h's kCloudLowpassMinHz exactly - was 320, then 150,
// now 20 so the shut end of the knob closes the cloud away rather than only
// darkening it (see that constant's own note).
const CLOUD_LP_MIN_HZ = 20;

const CURVE_F_MIN = 20;
const CURVE_F_MAX = 20000;
// A wide range on purpose. The ladder Reso crossfades in falls at 24 dB/oct,
// which over a 30 dB plot is off the bottom inside a single octave and reads
// as a vertical cliff rather than a filter skirt; 60 dB across the same
// height is the same slope drawn at a legible angle.
const CURVE_DB_FLOOR = -60;
// Headroom over unity, so Reso's peak has somewhere to go: without it the
// resonant bump is simply clipped flat against the top of the plot and the
// knob looks like it does nothing.
const CURVE_DB_CEIL = 15;
const CURVE_GRID_HZ = [100, 1000, 10000];
const CURVE_W = 120;
const CURVE_H = 44;
const CURVE_PAD = 4;
const CURVE_LOG_MIN = Math.log(CURVE_F_MIN);
const CURVE_LOG_SPAN = Math.log(CURVE_F_MAX) - CURVE_LOG_MIN;

// LadderFilter::kMaxFeedback, mirrored: Reso 1 is a hair past the classic
// 4-pole unity-loop-gain point, which is what makes the top of the knob
// self-oscillate.
const LADDER_MAX_FEEDBACK = 4.2;

/** Where the cutoff sits for a given knob position - the same geometric sweep
    Grainer::updateCloudFilter() does with its own std::pow, so the drawn
    corner tracks the audible one across the whole travel rather than only at
    the ends. `openness` is the knob in 0..1, 1 being wide open. */
function cloudCutoff(openness) {
  return CLOUD_LP_HZ * Math.pow(CLOUD_LP_MIN_HZ / CLOUD_LP_HZ, 1 - openness);
}

/** The cloud filter's magnitude in dB: the 4-pole ladder Grainer::cloudFiltered
    runs, with Reso as its feedback - G^4 / (1 + k G^4) over a one-pole G,
    scaled by LadderFilter's own passband compensation (1 + k) so the flat part
    stays on the 0 dB line and only the peak moves. */
function cloudDb(fHz, lpHz, reso = 0) {
  const w = fHz / lpHz;
  const d = 1 + w * w;
  // One stage: 1 / (1 + jw).
  const pRe = 1 / d;
  const pIm = -w / d;

  // Four of them in cascade...
  let gRe = 1;
  let gIm = 0;
  for (let i = 0; i < 4; i++) {
    const re = gRe * pRe - gIm * pIm;
    gIm = gRe * pIm + gIm * pRe;
    gRe = re;
  }

  // ...inside k of negative feedback.
  const k = reso * LADDER_MAX_FEEDBACK;
  const denRe = 1 + k * gRe;
  const denIm = k * gIm;
  const denMag = denRe * denRe + denIm * denIm || 1e-12;
  const re = ((gRe * denRe + gIm * denIm) / denMag) * (1 + k);
  const im = ((gIm * denRe - gRe * denIm) / denMag) * (1 + k);

  return 20 * Math.log10(Math.max(1e-6, Math.hypot(re, im)));
}

function curveX(hz) {
  const clamped = Math.min(CURVE_F_MAX, Math.max(CURVE_F_MIN, hz));
  return CURVE_PAD + ((Math.log(clamped) - CURVE_LOG_MIN) / CURVE_LOG_SPAN) * (CURVE_W - CURVE_PAD * 2);
}

function curveY(db) {
  const clamped = Math.min(CURVE_DB_CEIL, Math.max(CURVE_DB_FLOOR, db));
  return CURVE_PAD + ((CURVE_DB_CEIL - clamped) / (CURVE_DB_CEIL - CURVE_DB_FLOOR)) * (CURVE_H - CURVE_PAD * 2);
}

/** FilterCurve's own live subscription to the LFO's per-frame output - split
    out so it only ever mounts while `filter` actually has an assignment (see
    FilterCurve below), the same reason ModdableKnob.jsx's ModulatedKnob is a
    separate component from ModdableKnob itself: useLfoValue() re-renders its
    subscriber every animation frame, and an unmodulated Filter knob (the
    common case) should not pay for that. Mirrors PluginProcessor.cpp's own
    modulatedValue() exactly - base01 + depth * lfoValue, clamped - so the
    curve drawn here is the filter the DSP is actually running, not a
    separate UI-only approximation of it. */
function ModulatedFilterCurve({ accent, filter, reso, assignment }) {
  const lfoValue = useLfoValue();
  return <FilterCurveBody accent={accent} openness={resolveModulated(filter, assignment, lfoValue)} reso={reso} />;
}

/** The Mixer's Filter response, in place of a printed value. Reads `filter`
    and `reso` live and redraws, so the line moves with both knobs: wide open
    it is flat to 13 kHz, winding Filter down walks the corner to 20 Hz, and
    Reso raises the peak sitting on that corner. While the Mod tab has an LFO
    assigned to Filter, it instead redraws every frame from the live modulated
    value (ModulatedFilterCurve above) - the curve *is* this knob's value
    readout, so it is the one place a moving LFO assignment has to show up for
    the display to keep meaning what it says. Reso takes no LFO (see
    PluginProcessor.h's note on resoParam), so it needs no such path. */
export function FilterCurve({ accent }) {
  const [filter] = useJuceSliderValue("filter");
  const [reso] = useJuceSliderValue("reso");
  const assignment = useModAssignment("filter");

  if (assignment) return <ModulatedFilterCurve accent={accent} filter={filter} reso={reso} assignment={assignment} />;
  return <FilterCurveBody accent={accent} openness={filter} reso={reso} />;
}

/** The curve's own drawing, given the filter's openness (0..1, already
    resolved - the base parameter or the live modulated value, FilterCurve's
    own concern, not this component's). */
function FilterCurveBody({ accent, openness, reso = 0 }) {
  // The parameter is 0..100 % and the hook hands back 0..1, so it is already
  // the openness the sweep wants.
  const lpHz = cloudCutoff(openness);

  // Enough to resolve the peak: at high Reso the ladder's bump is only a
  // fraction of an octave wide, and at 72 steps across ten octaves it fell
  // between samples and read as a kink rather than a spike.
  const steps = 180;
  let line = "";
  for (let i = 0; i <= steps; i++) {
    const t = i / steps;
    const hz = Math.exp(CURVE_LOG_MIN + t * CURVE_LOG_SPAN);
    const x = CURVE_PAD + t * (CURVE_W - CURVE_PAD * 2);
    line += `${i === 0 ? "M" : "L"}${x.toFixed(1)} ${curveY(cloudDb(hz, lpHz, reso)).toFixed(1)} `;
  }

  const floorY = CURVE_H - CURVE_PAD;
  const fill = `${line}L ${CURVE_W - CURVE_PAD} ${floorY} L ${CURVE_PAD} ${floorY} Z`;

  return (
    <div className="pg-filter-curve">
      <svg width="100%" height="100%" viewBox={`0 0 ${CURVE_W} ${CURVE_H}`} preserveAspectRatio="none" fill="none">
        <g stroke="#233034" strokeWidth="1" vectorEffect="non-scaling-stroke">
          {CURVE_GRID_HZ.map((hz) => (
            <line key={hz} x1={curveX(hz)} y1={CURVE_PAD} x2={curveX(hz)} y2={floorY} />
          ))}
          {/* Unity, not a level partway down: it is what the flat passband
              sits on and so what Reso's peak is read against. */}
          <line x1={CURVE_PAD} y1={curveY(0)} x2={CURVE_W - CURVE_PAD} y2={curveY(0)} />
        </g>
        <path d={fill} fill={accent} fillOpacity="0.12" stroke="none" />
        <path
          d={line}
          fill="none"
          stroke={accent}
          strokeWidth="1.6"
          strokeLinecap="round"
          strokeLinejoin="round"
          vectorEffect="non-scaling-stroke"
        />
      </svg>
    </div>
  );
}

// Eight bars falling 28 -> 3px (COMPONENTS.md), bending with Decay the way
// BitBit Alpine's own reverb-tail bars do (packages/module-face/src/
// SideModule.jsx's decayBars) - the exponent runs the opposite way to Decay,
// so a short decay is already on the floor a couple of bars in and a long
// one holds up across the row. Was a fixed array (COMPONENTS.md's own numbers
// at rest, decay01 = 0), so the display never moved no matter what Decay was
// dialled to; this is that same rest shape, now a function of it.
const TAIL_BARS = 8;
const TAIL_MIN = 3;
const TAIL_MAX = 28;

function reverbTailHeights(decay01) {
  const curve = 0.45 + (1 - decay01) * 1.6;
  return Array.from({ length: TAIL_BARS }, (_, i) => TAIL_MIN + (TAIL_MAX - TAIL_MIN) * Math.pow(1 - i / (TAIL_BARS - 1), curve));
}

export function ReverbTail({ accent }) {
  // Not a ModdableKnob target (PluginProcessor.cpp's own kModChunk note: Delay
  // and Reverb are out of the LFO's reach), so there is no live-modulated
  // reading to resolve here the way GrainEnvelope/PitchWeights do - the base
  // value is the whole of it.
  const [decay] = useJuceSliderValue("decay");
  const heights = reverbTailHeights(decay);

  return (
    <div className="pg-tail">
      {heights.map((h, i) => (
        <div key={i} className="pg-tail__bar" style={{ height: `${h}px`, background: accent }} />
      ))}
    </div>
  );
}
