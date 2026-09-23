import "./MixerMeter.css";

// The scale both meters share. 0 dB at the top down to -60 at the floor, a
// tick every 6, numbered every other one so the ladder still reads at this
// width. Mirrors nothing in the DSP - it is only how the ladder is drawn.
const TOP_DB = 0;
const FLOOR_DB = -60;
const TICK_STEP_DB = 6;
const NUMBERED_EVERY = 2;

function fractionForDb(db) {
  return Math.min(1, Math.max(0, (db - FLOOR_DB) / (TOP_DB - FLOOR_DB)));
}

const TICKS = [];
for (let i = 0; TOP_DB - i * TICK_STEP_DB >= FLOOR_DB; i++) {
  const db = TOP_DB - i * TICK_STEP_DB;
  TICKS.push({ db, numbered: i % NUMBERED_EVERY === 0 });
}

/** The dB ladder beside a mixer fader - a static scale, no live level bar (that
    used to ride beside it and, together with the ladder's own tick numbers,
    overlap the value readout below the fader's thumb). `side` is which way
    the ladder sits, and `height` has to match the fader's own `length` so the
    two line up (the strip aligns them on their shared bottom edge). */
export default function MixerMeter({ side = "right", height = 110 }) {
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
    </div>
  );
}
