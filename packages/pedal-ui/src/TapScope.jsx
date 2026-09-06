import "./TapScope.css";

const MAX_TAP_HEIGHT = 30;
const MIN_TAP_HEIGHT = 2;

// The lane's time base. At the Time knob's 3s maximum the window is 24s -
// eight repeats across the lane, the way this has always looked - and it
// shrinks as the delay does, so a shorter time visibly tightens the comb.
//
// It shrinks more slowly than the time does, though: as the square root, not
// in step. Two reasons it is not simply proportional (a fixed 24s window, so
// that a given gap always meant the same number of milliseconds):
//
//   - The knob spans ~50:1, 62ms to 3s. On a linear 24s axis the default
//     patch - 250ms, 35% feedback - is two marks jammed against the left
//     edge. Truthful, and useless.
//   - The exact figure is already on the face, in the Readout beside each
//     knob. This is a feel indicator, not a measurement.
//
// The compression costs only the *absolute* reading. Both lanes always share
// one window, so the left/right comparison stays exactly proportional - an
// unlinked pair still reads as two combs at honestly different rates.
// WINDOW_COMPRESSION = 1 makes the axis strictly linear again.
const WINDOW_AT_MAX_MS = 24000;
const MAX_TIME_MS = 3000;
const WINDOW_COMPRESSION = 0.5;

function windowFor(longestMs) {
  return WINDOW_AT_MAX_MS * Math.pow(longestMs / MAX_TIME_MS, WINDOW_COMPRESSION);
}

// A tap thins out as its neighbours close in, so a fast delay reads as a fine
// comb instead of one solid bar. Clamped in CSS against a percentage of the
// lane, which avoids having to measure the lane's pixel width.
const TAP_WIDTH_MAX = 4;
const TAP_WIDTH_MIN = 1.5;
const TAP_WIDTH_OF_SPACING = 0.45;

// Mirrors TapeDelay::kMaxFeedback (shared/src/dsp/TapeDelay.cpp): the knob's
// 100% is 0.86 of unity, not 1.0, so repeats always die away. Keeping the same
// number here is what makes the drawn decay match what you hear.
const MAX_FEEDBACK = 0.86;

// Where a repeat stops being audible, as a fraction of the first one: -30dB.
// This is what sets the tap *count*, so the picture should agree with what you
// hear - 50% feedback gives four or five repeats on screen and four or five in
// the room.
//
// TapeDelay::getTailSeconds computes its repeat count the same way, but with a
// 0.001 (-60dB) floor. That is deliberately not reused: it answers "when has
// this decayed into the noise floor" (how long a host must keep processing
// after the input stops), which is far below the point where a repeat stops
// being something you can pick out.
//
// Deliberately a level rather than a pixel height: the count is then a function
// of feedback alone, so pulling Mix down dims the repeats without pretending
// there are fewer of them.
const AUDIBLE_FLOOR = 0.032;

// A hard stop on the drawing, not on the physics: at 100% feedback the floor
// above is reached around 23 repeats.
const MAX_TAPS = 26;

/** How many repeats are worth hearing at this feedback - the same
    log(floor)/log(gain) the delay engine uses for its own tail estimate. */
function audibleRepeats(gain) {
  if (gain <= 0.001) return 1;
  return Math.min(MAX_TAPS, Math.max(1, Math.floor(Math.log(AUDIBLE_FLOOR) / Math.log(gain)) + 1));
}

/**
 * One channel's repeats. Tap `n` sits at `n x timeMs` along the shared window
 * and stands `feedback^(n-1)` as tall as the first, scaled by the wet level -
 * so time sets the spacing, feedback sets how far the run trails off, and mix
 * sets the overall height.
 */
function taps(timeMs, windowMs, gain, wet) {
  const out = [];
  const count = audibleRepeats(gain);

  for (let n = 1; n <= count; n++) {
    const at = (n * timeMs) / windowMs;
    if (at > 1) break;

    out.push({
      at,
      atMs: n * timeMs,
      height: Math.max(MIN_TAP_HEIGHT, MAX_TAP_HEIGHT * wet * Math.pow(gain, n - 1)),
    });
  }

  return out;
}

function Lane({ items, spacing, cycleMs, bottom }) {
  // Never wider than the default, never thinner than a hairline, and in
  // between it tracks the gap between taps.
  const width = `clamp(${TAP_WIDTH_MIN}px, ${spacing * TAP_WIDTH_OF_SPACING * 100}%, ${TAP_WIDTH_MAX}px)`;

  return (
    <div className={`pui-tapscope__lane${bottom ? " pui-tapscope__lane--bottom" : ""}`}>
      {items.map((t, i) => (
        <span
          key={i}
          className="pui-tapscope__tap"
          style={{
            left: `${t.at * 100}%`,
            height: t.height,
            width,
            // Every tap runs the same one-cycle-long animation; the negative
            // delay starts each one part-way through, so its bright frame
            // lands exactly `atMs` into the cycle. Both lanes share the cycle,
            // so an unlinked pair drifts against each other on one clock, the
            // way the two delay lines actually do.
            animationDuration: `${cycleMs}ms`,
            animationDelay: `${-(cycleMs - t.atMs)}ms`,
          }}
        />
      ))}
    </div>
  );
}

/**
 * The stereo repeat visualiser above Delay's control row: two lanes hanging
 * off a shared centre line, left channel above it, right below, each tap
 * fading on a loop the way a real repeat decays into the noise floor.
 *
 * The three controls it draws are meant to be separable at a glance:
 *
 *   - `leftMs`/`rightMs` set tap *spacing* against a fixed time base, and do
 *     it independently per lane - so a short time visibly tightens the comb,
 *     and an unlinked pair reads as two combs that don't line up.
 *   - `feedback01` sets how far the run trails off - how many taps there are
 *     and how gently they shrink. It must not touch spacing.
 *   - `mix01` scales every tap's height together, leaving the count alone.
 *
 * That separation is the whole point, and it is what the first version got
 * wrong: it laid taps out with `justify-content: space-between`, so adding a
 * tap re-spaced all of them and feedback read as a *time* change.
 */
export default function TapScope({
  height = 78,
  leftMs = 500,
  rightMs = 500,
  feedback01 = 0.6,
  mix01 = 1,
  windowMs,
}) {
  const left = Math.max(1, leftMs);
  const right = Math.max(1, rightMs);

  const gain = Math.min(0.999, Math.max(0, feedback01) * MAX_FEEDBACK);
  // The equal-power wet leg of the same crossfade the processor mixes with,
  // so the drawn height tracks what the repeats actually do as Mix moves.
  const wet = Math.sin(Math.min(1, Math.max(0, mix01)) * Math.PI * 0.5);

  // One window for both lanes, keyed to the slower of the two, so their
  // spacings stay in the same ratio their times are.
  //
  // Two things set it, and the wider one wins. windowFor() is the time-reading
  // axis; the second term is however long the audible run actually lasts, so
  // that every repeat you can hear has somewhere to be drawn. The count is the
  // thing that must never be cropped, which is why it takes precedence - the
  // cost is that at high feedback the run fills the lane and the spacing stops
  // saying much about the time. Below about 70% feedback the first term wins
  // and time reads normally.
  const slowest = Math.max(left, right);
  const gapsNeeded = audibleRepeats(gain) + 1;
  const lane = windowMs ?? Math.max(windowFor(slowest), gapsNeeded * slowest);

  return (
    <div className="pui-reset pui-tapscope" style={{ height }}>
      <span className="pui-tapscope__label pui-tapscope__label--l">L</span>
      <span className="pui-tapscope__label pui-tapscope__label--r">R</span>
      <div className="pui-tapscope__centre" />
      <Lane items={taps(left, lane, gain, wet)} spacing={left / lane} cycleMs={lane} />
      <Lane items={taps(right, lane, gain, wet)} spacing={right / lane} cycleMs={lane} bottom />
    </div>
  );
}
