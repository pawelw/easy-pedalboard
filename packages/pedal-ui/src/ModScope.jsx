import "./ModScope.css";

/**
 * The Chorus and Phaser engines' displays: what each engine's LFO is doing to
 * the signal, drawn over time across the well, from the same constants the
 * audio path reads. Neither is a live meter - like BarDisplay, both are drawn
 * from the knob positions, so nothing here animates on its own.
 *
 * The constants below MUST match their headers by hand:
 *   CHORUS_PHASE_SPAN      ee::dsp::config::kPhaseSpanCycles   (ChorusConfig.h)
 *   PHASER_SWEEP_MIN/MAX   ee::dsp::phaser::kSweepMinHz/MaxHz  (PhaserConfig.h)
 *   PHASER_STEREO_OFFSET   ee::dsp::phaser::kStereoOffsetCycles
 *   PHASER_STAGES          ee::dsp::phaser::kStages
 * A picture drawn from a different number than the engine is a picture of the
 * wrong effect.
 *
 * Rate is time, and a 140px well cannot show 0.05 Hz and 8 Hz on one honest
 * time axis, so it sets how many LFO cycles the well spans - from half a cycle
 * at the bottom of the knob to five at the top, following the knob's own
 * (skewed) travel. Faster reads as busier, which is the thing a player hears.
 */

const CHORUS_PHASE_SPAN = 0.33;

const PHASER_SWEEP_MIN = 250;
const PHASER_SWEEP_MAX = 1800;
const PHASER_STEREO_OFFSET = 0.25;
const PHASER_STAGES = 6;

// Where the cascade's summed response nulls, relative to the all-pass corner:
// the dry + all-pass sum cancels where the cascade's phase is an odd multiple
// of pi, i.e. each first-order stage at pi/STAGES, 3pi/STAGES... -
// atan(f/fc) = k*pi/(2*STAGES) for odd k. Six stages is three notches, at
// fc*tan(15deg), fc and fc*tan(75deg). Feedback deepens them without moving
// them, so it is not in the picture.
const PHASER_NOTCH_RATIOS = Array.from({ length: PHASER_STAGES / 2 }, (_, i) =>
  Math.tan(((2 * i + 1) * Math.PI) / (2 * PHASER_STAGES)),
);

// The well's frequency axis for the phaser, bottom to top. Covers the lowest
// notch at the bottom of a full sweep (~67 Hz) to the highest at the top
// (~6.7 kHz) with a little air either side.
const PHASER_F_LO = 45;
const PHASER_F_HI = 10000;

const VIEW_W = 140;
const VIEW_H = 63;
const PAD_X = 10;
const PAD_Y = 7;
const STEPS = 120;

const MIN_CYCLES = 0.5;
const MAX_CYCLES = 5;

function clamp01(v) {
  return Math.max(0, Math.min(1, Number.isFinite(v) ? v : 0));
}

function cyclesForRate(rate01) {
  return MIN_CYCLES + (MAX_CYCLES - MIN_CYCLES) * clamp01(rate01);
}

/** An SVG path through `STEPS` samples of `yAt(t)`, t in [0, 1] across the
    plot. */
function tracePath(yAt) {
  let d = "";
  for (let i = 0; i <= STEPS; i++) {
    const t = i / STEPS;
    const x = PAD_X + t * (VIEW_W - 2 * PAD_X);
    d += (i === 0 ? "M" : "L") + x.toFixed(2) + " " + yAt(t).toFixed(2) + " ";
  }
  return d;
}

function Well({ ariaLabel, children }) {
  return (
    <div className="pui-reset pui-modscope" role="img" aria-label={ariaLabel}>
      <svg className="pui-modscope__svg" viewBox={`0 0 ${VIEW_W} ${VIEW_H}`} preserveAspectRatio="none">
        {children}
      </svg>
    </div>
  );
}

/**
 * The chorus's delay-tap modulation, left and right. Each trace is the
 * channel's LFO (ee::dsp::Chorus: a sine, peak excursion Depth x kDepthMaxMs):
 * Depth is how far it swings - at 0 both lie flat on the centre line, which is
 * a fixed delay and no chorus at all - Rate is how many cycles the well spans,
 * and Phase is how far the right trace runs behind the left. The shaded band
 * between them is the stereo width: at Phase 0 the traces coincide and it
 * vanishes, the same collapse the engine's image makes.
 */
export function ChorusScope({ rate01 = 0.5, depth01 = 0.5, phase01 = 0.5 }) {
  const cycles = cyclesForRate(rate01);
  const amp = clamp01(depth01) * (VIEW_H / 2 - PAD_Y);
  const offset = clamp01(phase01) * CHORUS_PHASE_SPAN;
  const mid = VIEW_H / 2;

  const yL = (t) => mid - amp * Math.sin(2 * Math.PI * t * cycles);
  const yR = (t) => mid - amp * Math.sin(2 * Math.PI * (t * cycles - offset));

  const left = tracePath(yL);
  const right = tracePath(yR);

  // The band between the two traces: along L, then back along R.
  let band = left;
  for (let i = STEPS; i >= 0; i--) {
    const t = i / STEPS;
    band += "L" + (PAD_X + t * (VIEW_W - 2 * PAD_X)).toFixed(2) + " " + yR(t).toFixed(2) + " ";
  }
  band += "Z";

  return (
    <Well ariaLabel="Chorus modulation">
      <line className="pui-modscope__grid" x1="0" x2={VIEW_W} y1={mid} y2={mid} />
      <path className="pui-modscope__band" d={band} />
      <path className="pui-modscope__trace pui-modscope__trace--r" d={right} />
      <path className="pui-modscope__trace" d={left} />
    </Well>
  );
}

/**
 * The phaser's notches, over time. Up the well is frequency (log), across it
 * is time; each line is one of the cascade's three notches riding the LFO.
 * They move together, a fixed ratio apart, because they are all set by the
 * one all-pass corner (ee::dsp::Phaser::runChannel): Depth is how far that
 * corner swings about the geometric centre of kSweepMinHz..kSweepMaxHz - at 0
 * the notches still sit mid-spectrum and colour the tone, exactly as the engine
 * does, so they lie flat rather than vanish - and Rate is how many cycles the
 * well spans. The faint lines are the right channel, a fixed quarter cycle
 * behind.
 */
export function PhaserScope({ rate01 = 0.5, depth01 = 0.5 }) {
  const cycles = cyclesForRate(rate01);
  const depth = clamp01(depth01);

  const logMin = Math.log(PHASER_SWEEP_MIN);
  const logMax = Math.log(PHASER_SWEEP_MAX);
  const logMid = 0.5 * (logMin + logMax);
  const halfSpan = 0.5 * (logMax - logMin);

  const logLo = Math.log(PHASER_F_LO);
  const logHi = Math.log(PHASER_F_HI);
  const yForLogHz = (lf) => VIEW_H - PAD_Y - ((lf - logLo) / (logHi - logLo)) * (VIEW_H - 2 * PAD_Y);

  const notchPaths = (offsetCycles) =>
    PHASER_NOTCH_RATIOS.map((ratio) =>
      tracePath((t) => {
        const mod = Math.sin(2 * Math.PI * (t * cycles + offsetCycles));
        return yForLogHz(logMid + depth * halfSpan * mod + Math.log(ratio));
      }),
    );

  const left = notchPaths(0);
  const right = notchPaths(PHASER_STEREO_OFFSET);

  // Gridlines at 100 Hz, 1 kHz and 5 kHz - enough to read the notches as
  // low / mid / high without printing a scale in a well this small.
  const grid = [100, 1000, 5000].map((hz) => yForLogHz(Math.log(hz)));

  return (
    <Well ariaLabel="Phaser notch sweep">
      {grid.map((y) => (
        <line key={y} className="pui-modscope__grid" x1="0" x2={VIEW_W} y1={y} y2={y} />
      ))}
      {right.map((d, i) => (
        <path key={`r${i}`} className="pui-modscope__trace pui-modscope__trace--r" d={d} />
      ))}
      {left.map((d, i) => (
        <path key={`l${i}`} className="pui-modscope__trace" d={d} />
      ))}
    </Well>
  );
}
