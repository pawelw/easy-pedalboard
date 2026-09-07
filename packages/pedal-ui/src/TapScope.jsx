import { useEffect, useRef } from "react";
import "./TapScope.css";

const MAX_TAP_HEIGHT = 30;

// A floor on the *decay*, so the quietest repeat in a long run is still a mark
// rather than a hairline. Deliberately not a floor on the finished height: it
// is applied before the wet level scales the run, so Mix at zero takes every
// tap to nothing instead of leaving a row of stubs behind on a face that is
// passing no delay at all. Same for the dry mark at the other end of the
// crossfade - at Mix 100 % there is no dry signal and nothing to draw.
const MIN_TAP_HEIGHT = 2;

// The dry line's own full height, taller than a repeat's because it is not one
// of them: it is the signal every tap to its right is a copy of, drawn once, at
// the instant the note is played.
const MAX_DRY_HEIGHT = 35;

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

// The decay is drawn on a curve, not at its true amplitude. Straight
// `gain^(n-1)` puts the last audible repeat at AUDIBLE_FLOOR - 3% of the first,
// a single pixel that clamps to MIN_TAP_HEIGHT and reads as a row of stubs
// however much feedback there is. Raising the amplitude to this power lifts the
// whole run: the last tap lands at 0.032^0.4, a quarter of the first, and every
// tap in between rises with it, so the comb reads as a decay rather than as one
// tall bar followed by dust.
//
// It compresses the *shape* only. The count still comes from the real
// amplitude, mix still scales the whole run, and the tallest tap is unchanged -
// so nothing about which control does what moves.
const TAP_DECAY_CURVE = 0.4;

// How a lit tap fades back down, as a fraction of the gap to its neighbour: a
// flash has to be over before the next repeat lands or a fast delay reads as
// one solid glow. Clamped so a 3-second delay doesn't hold a tap lit for a
// second and a half, and a 62ms one still flashes long enough to see.
const FLASH_OF_SPACING = 0.55;
const FLASH_MIN_MS = 45;
const FLASH_MAX_MS = 260;

// Resting and lit opacity. The resting value is what every tap sat at before
// there was anything to animate against, so a silent scope looks exactly like
// the old one did between flashes.
const DIM = 0.26;
const LIT = 1;

// A playhead is only as bright as the note that started it, so a quiet passage
// reads as a quiet scope - but never so dim it can't be followed, which is what
// the floor is for.
const MIN_STRIKE_BRIGHTNESS = 0.4;

// Playheads in flight. A strum is several onsets inside a second and each one
// owns a run down the lane, so they overlap; past a handful the oldest is
// dropped rather than tracked forever.
const MAX_PLAYHEADS = 8;

/** How many repeats are worth hearing at this feedback - the same
    log(floor)/log(gain) the delay engine uses for its own tail estimate. */
function audibleRepeats(gain) {
  if (gain <= 0.001) return 1;
  return Math.min(MAX_TAPS, Math.max(1, Math.floor(Math.log(AUDIBLE_FLOOR) / Math.log(gain)) + 1));
}

/**
 * One channel's repeats. Tap `n` sits at `n x timeMs` along the shared window
 * and stands `feedback^(n-1)` as tall as the first - through TAP_DECAY_CURVE -
 * scaled by the wet level. Time sets the spacing, feedback sets how far the run
 * trails off, and mix sets the overall height.
 */
function taps(timeMs, windowMs, gain, wet) {
  const out = [];
  const count = audibleRepeats(gain);

  for (let n = 1; n <= count; n++) {
    const at = (n * timeMs) / windowMs;
    if (at > 1) break;

    const amplitude = Math.pow(Math.pow(gain, n - 1), TAP_DECAY_CURVE);

    out.push({
      at,
      atMs: n * timeMs,
      height: wet * Math.max(MIN_TAP_HEIGHT, MAX_TAP_HEIGHT * amplitude),
    });
  }

  return out;
}

function Lane({ items, spacing, dryHeight, flashMs, bottom }) {
  // Never wider than the default, never thinner than a hairline, and in
  // between it tracks the gap between taps.
  const width = `clamp(${TAP_WIDTH_MIN}px, ${spacing * TAP_WIDTH_OF_SPACING * 100}%, ${TAP_WIDTH_MAX}px)`;

  return (
    <div className={`pui-tapscope__lane${bottom ? " pui-tapscope__lane--bottom" : ""}`}>
      {/* The dry signal: one mark at the origin of the time axis, the note
          itself rather than a copy of it. Its height is the dry leg of the
          same equal-power crossfade the repeats' is the wet leg of, so the
          two move against each other as Mix turns - all dry and it towers
          over an empty lane, all wet and it shrinks to nothing while the
          comb comes up. Drawn in both lanes at once: the input is one
          signal, not a stereo pair, so it reads as a single line crossing
          the centre. */}
      <span
        className="pui-tapscope__tap pui-tapscope__tap--dry"
        data-at-ms="0"
        data-flash-ms={flashMs}
        style={{ left: 0, height: dryHeight, width: TAP_WIDTH_MAX }}
      />

      {items.map((t, i) => (
        <span
          key={i}
          className="pui-tapscope__tap"
          // Read by the playhead loop, which walks the DOM rather than
          // re-rendering: at 60fps a React pass per frame for fifty-odd taps
          // is a lot of work to change one number on each of them.
          data-at-ms={t.atMs}
          data-flash-ms={flashMs}
          style={{ left: `${t.at * 100}%`, height: t.height, width }}
        />
      ))}
    </div>
  );
}

/**
 * The stereo repeat visualiser above Delay's control row: two lanes hanging
 * off a shared centre line, left channel above it, right below, with the dry
 * signal standing at the origin and its repeats running off to the right.
 *
 * The three controls it draws are meant to be separable at a glance:
 *
 *   - `leftMs`/`rightMs` set tap *spacing* against a fixed time base, and do
 *     it independently per lane - so a short time visibly tightens the comb,
 *     and an unlinked pair reads as two combs that don't line up.
 *   - `feedback01` sets how far the run trails off - how many taps there are
 *     and how gently they shrink. It must not touch spacing.
 *   - `mix01` scales every tap's height together, leaving the count alone, and
 *     moves the dry line the opposite way.
 *
 * That separation is the whole point, and it is what the first version got
 * wrong: it laid taps out with `justify-content: space-between`, so adding a
 * tap re-spaced all of them and feedback read as a *time* change.
 *
 * Nothing on it moves on its own. `strikes` is a monotonic count of note
 * onsets from the processor (PeakDelayProcessor::strikeCountUi); each new one
 * sends a playhead down the time axis, lighting the dry line as the note
 * sounds and then each repeat as it comes back, as brightly as `level` said
 * that note was loud. With no signal there are no strikes and the scope is
 * still - which is the point: what it shows is the delay responding to
 * something played, and an idle pedal is not doing that.
 */
export default function TapScope({
  height = 78,
  leftMs = 500,
  rightMs = 500,
  feedback01 = 0.6,
  mix01 = 1,
  windowMs,
  strikes = 0,
  level = 1,
}) {
  const left = Math.max(1, leftMs);
  const right = Math.max(1, rightMs);

  const gain = Math.min(0.999, Math.max(0, feedback01) * MAX_FEEDBACK);
  // The two legs of the equal-power crossfade the processor mixes with, so the
  // drawn heights track what the repeats and the dry signal actually do as Mix
  // moves.
  const mix = Math.min(1, Math.max(0, mix01));
  const wet = Math.sin(mix * Math.PI * 0.5);
  const dry = Math.cos(mix * Math.PI * 0.5);

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

  const dryHeight = MAX_DRY_HEIGHT * dry;
  const flashFor = (timeMs) => Math.min(FLASH_MAX_MS, Math.max(FLASH_MIN_MS, timeMs * FLASH_OF_SPACING));

  const rootRef = useRef(null);
  const playheads = useRef([]);
  const frame = useRef(0);
  const lastStrike = useRef(strikes);

  // Read inside the effect below, which must not re-run when the level moves -
  // only a new strike starts a playhead, and the level is 45Hz noise between
  // them. What it wants is the level at the instant a note was counted.
  const levelNow = useRef(level);
  levelNow.current = level;

  useEffect(() => {
    const root = rootRef.current;
    if (!root) return undefined;

    if (window.matchMedia?.("(prefers-reduced-motion: reduce)").matches) return undefined;

    // A strike is only ever a *new* onset. The count arrives on a repeating
    // 45Hz feed, so most of the frames it comes in on are saying nothing has
    // happened - and the first value the page ever sees is whatever the
    // processor had already counted before the editor opened, which must not
    // fire a flash for a note played before anyone was looking.
    if (strikes === lastStrike.current) return undefined;
    lastStrike.current = strikes;

    const brightness = Math.max(MIN_STRIKE_BRIGHTNESS, Math.min(1, levelNow.current));
    playheads.current = [...playheads.current, { at: performance.now(), brightness }].slice(-MAX_PLAYHEADS);

    if (frame.current !== 0) return undefined;

    const step = () => {
      const now = performance.now();
      const marks = root.querySelectorAll(".pui-tapscope__tap");

      let live = false;

      marks.forEach((mark) => {
        const atMs = Number(mark.dataset.atMs);
        const flashMs = Number(mark.dataset.flashMs);
        let brightest = 0;

        for (const head of playheads.current) {
          // Time since this playhead reached this tap's instant. Negative
          // means it hasn't got there yet; past the flash it's over.
          const since = now - head.at - atMs;

          if (since >= 0 && since < flashMs) {
            brightest = Math.max(brightest, head.brightness * (1 - since / flashMs));
            live = true;
          } else if (since < 0) {
            live = true;
          }
        }

        mark.style.opacity = DIM + (LIT - DIM) * brightest;
      });

      // Drop playheads that have run off the end of every tap in the lane.
      // Keeping the test on the marks themselves rather than on a computed
      // tail length means a playhead outlives a knob move that shortens the
      // run under it, instead of being stranded lit.
      if (live) {
        frame.current = requestAnimationFrame(step);
      } else {
        frame.current = 0;
        playheads.current = [];
        marks.forEach((mark) => {
          mark.style.opacity = DIM;
        });
      }
    };

    frame.current = requestAnimationFrame(step);

    return undefined;
  }, [strikes]);

  // Only on unmount: the loop above has to survive a re-render (every knob
  // move is one) or a flash would be cut short by turning any control.
  useEffect(
    () => () => {
      if (frame.current !== 0) cancelAnimationFrame(frame.current);
      frame.current = 0;
    },
    []
  );

  return (
    <div className="pui-reset pui-tapscope" style={{ height }} ref={rootRef}>
      <span className="pui-tapscope__label pui-tapscope__label--l">L</span>
      <span className="pui-tapscope__label pui-tapscope__label--r">R</span>
      <div className="pui-tapscope__centre" />
      <Lane
        items={taps(left, lane, gain, wet)}
        spacing={left / lane}
        dryHeight={dryHeight}
        flashMs={flashFor(left)}
      />
      <Lane
        items={taps(right, lane, gain, wet)}
        spacing={right / lane}
        dryHeight={dryHeight}
        flashMs={flashFor(right)}
        bottom
      />
    </div>
  );
}
