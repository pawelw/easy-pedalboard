import { useEffect, useRef } from "react";
import "./MixerMeter.css";

// The scale both meters share. 0 dB at the top down to -60 at the floor, a
// tick every 6, numbered every other one so the ladder still reads at this
// width. Mirrors nothing in the DSP - the levels arrive linear and this is
// only how they are drawn.
const TOP_DB = 0;
const FLOOR_DB = -60;
const TICK_STEP_DB = 6;
const NUMBERED_EVERY = 2;

/** The `grainCosmos` feed's two level fields, decoded once for the whole face.
    Both meters read the same 30 Hz payload the Cosmos panel already gets (see
    BitBitGrainWebEditor::timerCallback), so subscribing per component would
    only parse it twice. Module-level rather than context: there is exactly one
    backend and exactly one payload, and nothing here is per-tree state. */
const levels = { dry: 0, wet: 0 };
let listening = false;

function listenOnce() {
  if (listening) return;

  const handle = window.__JUCE__?.backend?.addEventListener("grainCosmos", (payload) => {
    let data = payload;
    if (typeof data === "string") {
      try {
        data = JSON.parse(data);
      } catch {
        return;
      }
    }
    if (!data) return;
    if (Number.isFinite(data.dry)) levels.dry = data.dry;
    if (Number.isFinite(data.level)) levels.wet = data.level;
  });

  // Only latched once the backend actually took it: in the gallery there is
  // none, and a later mount inside a real editor should still get to try.
  listening = handle != null;
}

function fractionForDb(db) {
  return Math.min(1, Math.max(0, (db - FLOOR_DB) / (TOP_DB - FLOOR_DB)));
}

function fractionForLevel(linear) {
  if (!(linear > 0)) return 0;
  return fractionForDb(20 * Math.log10(linear));
}

const TICKS = [];
for (let i = 0; TOP_DB - i * TICK_STEP_DB >= FLOOR_DB; i++) {
  const db = TOP_DB - i * TICK_STEP_DB;
  TICKS.push({ db, numbered: i % NUMBERED_EVERY === 0 });
}

/** One fader's level, with the dB ladder between the two - the scale hard
    against its own fader and the bar beyond it, so the two bars end up either
    side of the link button and each set of numbers reads as belonging to the
    slider it touches.

    `channel` picks which of the two the payload carries; `side` is which way
    the ladder sits, and `height` has to match the fader's own `length` so the
    two line up (the strip aligns them on their shared bottom edge).

    The bar is driven by writing a transform in an animation frame rather than
    by React state: at 30 Hz in and 60 fps out this would otherwise re-render
    the mixer column twice a frame for a number nothing else reads. */
export default function MixerMeter({ channel, side = "right", height = 110 }) {
  const fillRef = useRef(null);

  useEffect(() => {
    listenOnce();

    let raf = 0;
    let shown = 0;

    const tick = () => {
      const target = fractionForLevel(levels[channel]);

      // Instant up, eased down: the native side already holds a peak for
      // ~0.12 s, so this only smooths its 30 Hz steps into the display's own
      // frame rate rather than adding a second envelope on top.
      shown = target > shown ? target : shown + (target - shown) * 0.25;

      if (fillRef.current) fillRef.current.style.transform = `scaleY(${shown.toFixed(3)})`;
      raf = requestAnimationFrame(tick);
    };

    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [channel]);

  return (
    <div className={`pg-meter pg-meter--${side}`} style={{ height }}>
      <div className="pg-meter__scale">
        {TICKS.map(({ db, numbered }) => (
          <div
            key={db}
            className={`pg-meter__tick${numbered ? " pg-meter__tick--numbered" : ""}`}
            style={{ top: `${((1 - fractionForDb(db)) * 100).toFixed(2)}%` }}
          >
            {numbered && <span className="pg-meter__num">{-db}</span>}
          </div>
        ))}
      </div>
      <div className="pg-meter__bar">
        <div className="pg-meter__fill" ref={fillRef} />
      </div>
    </div>
  );
}
