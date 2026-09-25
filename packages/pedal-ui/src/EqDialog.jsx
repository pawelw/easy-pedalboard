import { useEffect, useMemo, useRef, useState } from "react";
import Button from "./Button.jsx";
import PowerToggle from "./PowerToggle.jsx";
import { EqShapeIcon } from "./EqIcon.jsx";
import { JuceKnob, useJuceChoiceValue, useJuceScaledValue, useJuceToggleValue } from "./juce.jsx";
import {
  EQ_BANDS,
  EQ_DEFAULTS,
  EQ_MAX_GAIN_DB,
  EQ_MAX_HZ,
  EQ_MAX_Q,
  EQ_MIN_HZ,
  EQ_MIN_Q,
  EQ_SIMPLE,
  EQ_TYPES,
  bandSections,
  curveFrequencies,
  sectionsDb,
  typeHasGain,
} from "./eq.js";
import "./EqDialog.css";

// The graph's own units. +/-18 dB of travel on a +/-15 dB control leaves the
// dots room at the top and bottom, and the curve room to show a resonant cut.
const W = 828;
const H = 300;
const DB_SPAN = 18;
const FREQS = curveFrequencies(320);
const LOG_LO = Math.log(EQ_MIN_HZ);
const LOG_HI = Math.log(EQ_MAX_HZ);
const GRID_HZ = [
  [50, "50"],
  [100, "100"],
  [200, "200"],
  [500, "500"],
  [1000, "1k"],
  [2000, "2k"],
  [5000, "5k"],
  [10000, "10k"],
];
const GRID_DB = [12, 6, 0, -6, -12];

const xOf = (hz) => ((Math.log(hz) - LOG_LO) / (LOG_HI - LOG_LO)) * W;
const hzOf = (x) => Math.exp(LOG_LO + (Math.min(W, Math.max(0, x)) / W) * (LOG_HI - LOG_LO));
const yOf = (db) => H / 2 - (db / DB_SPAN) * (H / 2);
const dbOf = (y) => ((H / 2 - y) / (H / 2)) * DB_SPAN;
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

/** A band's colour: one of the theme's accents, set per band in EqDialog.css
    so it follows the palette like everything else on the face. */
const bandColour = (i) => `var(--pui-eq-band-${i + 1})`;

/** Whether a band is actually shaping the sound - the same test the engine
    uses to leave a band out of the path (ee::dsp::Equaliser::wanted). */
function isActive(b) {
  return b.on && (!typeHasGain(b.type) || Math.abs(b.gain) > 0.01);
}

function linePath(dbs) {
  return dbs
    .map((db, i) => `${i === 0 ? "M" : "L"}${xOf(FREQS[i]).toFixed(1)} ${yOf(clamp(db, -DB_SPAN - 6, DB_SPAN + 6)).toFixed(1)}`)
    .join("");
}

/**
 * The response graph: the grid, one filled curve per band that is doing
 * something, the sum of them in ink on top, and a dot per band.
 *
 * Drag a dot to move its band - sideways is frequency, up and down is gain (a
 * cut or a notch has no gain, so only sideways). The wheel over the graph is Q
 * for the selected band. Double-click a dot to switch its band on or off, or
 * the empty graph to switch on the first unused band where you clicked, as a
 * bell.
 *
 * Every band carries its own setters, so the graph knows nothing about
 * parameters: the Simple face hands it three bands with no `setHz`, which is
 * what pins their dots to a frequency.
 */
function EqGraph({ bands, selected, onSelect, onAddAt }) {
  const svgRef = useRef(null);
  const drag = useRef(null);
  const latest = useRef({ bands, selected });
  latest.current = { bands, selected };

  const signature = bands.map((b) => `${b.type}|${b.hz}|${b.gain}|${b.q}|${b.on}`).join(";");
  const curves = useMemo(
    () => bands.map((b) => (isActive(b) ? FREQS.map((f) => sectionsDb(bandSections(b), f)) : null)),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [signature],
  );
  const total = useMemo(() => FREQS.map((_, k) => curves.reduce((sum, c) => sum + (c ? c[k] : 0), 0)), [curves]);

  // The wheel is Q. A native listener, because React's is passive and could
  // not stop the page scrolling underneath.
  useEffect(() => {
    const svg = svgRef.current;
    if (!svg) return undefined;
    const onWheel = (event) => {
      const { bands: all, selected: sel } = latest.current;
      const band = all[sel];
      if (!band?.setQ) return;
      event.preventDefault();
      const next = clamp(band.q * Math.exp(-event.deltaY * 0.0015), EQ_MIN_Q, EQ_MAX_Q);
      band.setQ(Math.round(next * 100) / 100);
    };
    svg.addEventListener("wheel", onWheel, { passive: false });
    return () => svg.removeEventListener("wheel", onWheel);
  }, []);

  const toView = (event) => {
    const r = svgRef.current.getBoundingClientRect();
    return { x: ((event.clientX - r.left) * W) / r.width, y: ((event.clientY - r.top) * H) / r.height };
  };

  const startDrag = (event, index) => {
    event.stopPropagation();
    onSelect?.(index);
    if (event.button !== 0) return;
    svgRef.current.setPointerCapture(event.pointerId);
    drag.current = index;
    latest.current.bands[index].begin?.();
  };

  const moveDrag = (event) => {
    if (drag.current == null) return;
    const band = latest.current.bands[drag.current];
    const { x, y } = toView(event);
    if (band.setHz) band.setHz(Math.round(hzOf(x) * 10) / 10);
    if (band.setGain && typeHasGain(band.type))
      band.setGain(Math.round(clamp(dbOf(y), -EQ_MAX_GAIN_DB, EQ_MAX_GAIN_DB) * 10) / 10);
  };

  const endDrag = () => {
    if (drag.current == null) return;
    latest.current.bands[drag.current].end?.();
    drag.current = null;
  };

  const dotAt = (b) => ({
    x: xOf(clamp(b.hz, EQ_MIN_HZ, EQ_MAX_HZ)),
    y: yOf(typeHasGain(b.type) ? b.gain : 0),
  });

  // One handler for the whole graph rather than one per dot: the drag's
  // pointer capture retargets the double-click to the <svg>, so a dot's own
  // handler would never see it. A hit on a dot toggles that band; anywhere
  // else switches on a free one there.
  const onDoubleClick = (event) => {
    const { x, y } = toView(event);
    const hit = bands.findIndex((b) => {
      const d = dotAt(b);
      return Math.hypot(d.x - x, d.y - y) <= 12;
    });
    if (hit >= 0) {
      bands[hit].setOn?.(!bands[hit].on);
      return;
    }
    onAddAt?.(hzOf(x), clamp(dbOf(y), -EQ_MAX_GAIN_DB, EQ_MAX_GAIN_DB));
  };

  const zero = yOf(0);

  return (
    <svg
      ref={svgRef}
      className="pui-eq__graph"
      viewBox={`0 0 ${W} ${H}`}
      onPointerMove={moveDrag}
      onPointerUp={endDrag}
      onPointerCancel={endDrag}
      onDoubleClick={onDoubleClick}
    >
      {GRID_HZ.map(([hz, label]) => (
        <g key={hz}>
          <line x1={xOf(hz)} x2={xOf(hz)} y1="0" y2={H} className="pui-eq__grid" />
          <text x={xOf(hz) + 2} y={H - 4} className="pui-eq__scale">
            {label}
          </text>
        </g>
      ))}
      {GRID_DB.map((db) => (
        <g key={db}>
          <line x1="0" x2={W} y1={yOf(db)} y2={yOf(db)} className={db === 0 ? "pui-eq__grid pui-eq__grid--zero" : "pui-eq__grid"} />
          {db !== 0 && (
            <text x="3" y={yOf(db) - 2} className="pui-eq__scale">
              {db > 0 ? `+${db}` : db}
            </text>
          )}
        </g>
      ))}

      {curves.map(
        (c, i) =>
          c && (
            <path
              key={i}
              d={`${linePath(c)}L${W} ${zero}L0 ${zero}Z`}
              className={`pui-eq__band-fill${i === selected ? " pui-eq__band-fill--selected" : ""}`}
              style={{ fill: bandColour(bands[i].colourIndex ?? i), stroke: bandColour(bands[i].colourIndex ?? i) }}
            />
          ),
      )}

      <path d={linePath(total)} className="pui-eq__total" />

      {bands.map((b, i) => {
        const { x: cx, y: cy } = dotAt(b);
        const colour = bandColour(b.colourIndex ?? i);
        return (
          <g
            key={i}
            className={`pui-eq__dot${b.on ? "" : " pui-eq__dot--off"}${i === selected ? " pui-eq__dot--selected" : ""}`}
            onPointerDown={(event) => startDrag(event, i)}
            style={{ color: colour }}
          >
            {i === selected && <circle cx={cx} cy={cy} r="13" className="pui-eq__dot-ring" />}
            <circle cx={cx} cy={cy} r="9.5" className="pui-eq__dot-body" />
            <text x={cx} y={cy + 3.5} textAnchor="middle" className="pui-eq__dot-label">
              {b.label}
            </text>
          </g>
        );
      })}
    </svg>
  );
}

/** The band-shape menu - EQ Eight's column of glyphs, opening upward over the
    graph so it never runs off the bottom of the dialog. */
function TypeMenu({ value, onChange }) {
  const [open, setOpen] = useState(false);
  const ref = useRef(null);

  useEffect(() => {
    if (!open) return undefined;
    const close = (event) => {
      if (!ref.current?.contains(event.target)) setOpen(false);
    };
    window.addEventListener("mousedown", close);
    return () => window.removeEventListener("mousedown", close);
  }, [open]);

  return (
    <div className="pui-eq__type" ref={ref}>
      <button
        type="button"
        className="pui-eq__type-button"
        onClick={() => setOpen(!open)}
        aria-haspopup="listbox"
        aria-expanded={open}
        title={EQ_TYPES[value]?.label}
      >
        <EqShapeIcon type={value} />
        <svg width="8" height="5" viewBox="0 0 8 5" aria-hidden="true">
          <path d="M0.5 0.5L4 4L7.5 0.5" fill="none" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" />
        </svg>
      </button>
      {open && (
        <div className="pui-eq__type-menu" role="listbox" aria-label="Band shape">
          {EQ_TYPES.map((t, i) => (
            <button
              type="button"
              key={t.id}
              role="option"
              aria-selected={i === value}
              className="pui-eq__type-option"
              onClick={() => {
                onChange(i);
                setOpen(false);
              }}
            >
              <EqShapeIcon type={i} />
              <span>{t.label}</span>
            </button>
          ))}
        </div>
      )}
      <div className="pui-caption pui-eq__type-caption">{EQ_TYPES[value]?.label}</div>
    </div>
  );
}

/** One Advanced band's parameters as the graph and the panel want them. Called
    a fixed EQ_BANDS times, in order, from EqAdvanced - never conditionally. */
function useEqBand(index) {
  const base = `eq.b${index + 1}.`;
  const d = EQ_DEFAULTS[index];
  const [on, setOn] = useJuceToggleValue(`${base}on`, d.on);
  const [type, setType] = useJuceChoiceValue(`${base}type`, EQ_TYPES.length, d.type);
  const [hz, setHz, hzState] = useJuceScaledValue(`${base}freq`, d.hz);
  const [gain, setGain, gainState] = useJuceScaledValue(`${base}gain`, 0);
  const [q, setQ] = useJuceScaledValue(`${base}q`, 0.71);

  return {
    label: index + 1,
    on,
    setOn,
    type,
    setType,
    hz,
    setHz,
    gain,
    setGain,
    q,
    setQ,
    // One host gesture per drag, for both of the parameters a drag moves.
    begin: () => {
      hzState.sliderDragStarted();
      gainState.sliderDragStarted();
    },
    end: () => {
      hzState.sliderDragEnded();
      gainState.sliderDragEnded();
    },
  };
}

function EqAdvanced() {
  const bands = [];
  for (let i = 0; i < EQ_BANDS; ++i) bands.push(useEqBand(i)); // eslint-disable-line react-hooks/rules-of-hooks
  const [selected, setSelected] = useState(2);
  const band = bands[selected];
  const base = `eq.b${selected + 1}.`;

  const addAt = (hz, db) => {
    const free = bands.findIndex((b) => !b.on);
    if (free < 0) return;
    const b = bands[free];
    b.setType(3);
    b.setHz(Math.round(hz * 10) / 10);
    b.setGain(Math.round(db * 10) / 10);
    b.setOn(true);
    setSelected(free);
  };

  const step = (delta) => setSelected((selected + delta + EQ_BANDS) % EQ_BANDS);

  return (
    <>
      <div className="pui-eq__screen">
        <EqGraph bands={bands} selected={selected} onSelect={setSelected} onAddAt={addAt} />
      </div>

      <div
        className={`pui-eq__panel${band.on ? "" : " pui-eq__panel--off"}`}
        style={{ "--pui-accent": bandColour(selected), "--pui-soft-lit": bandColour(selected) }}
      >
        <div className="pui-eq__bandsel">
          <button type="button" onClick={() => step(-1)} aria-label="Previous band">
            <svg width="8" height="5" viewBox="0 0 8 5" aria-hidden="true">
              <path d="M0.5 4.5L4 1L7.5 4.5" fill="none" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" />
            </svg>
          </button>
          <span className="pui-eq__bandnum">{selected + 1}</span>
          <button type="button" onClick={() => step(1)} aria-label="Next band">
            <svg width="8" height="5" viewBox="0 0 8 5" aria-hidden="true">
              <path d="M0.5 0.5L4 4L7.5 0.5" fill="none" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" />
            </svg>
          </button>
        </div>

        <div className="pui-eq__bandctl">
          <PowerToggle on={band.on} onToggle={band.setOn} ariaLabel={`Band ${selected + 1}`} />
          <TypeMenu value={band.type} onChange={band.setType} />
        </div>

        <div className="pui-eq__knobs">
          <div className={typeHasGain(band.type) ? undefined : "pui-eq__knob--unused"}>
            <JuceKnob parameterId={`${base}gain`} variant="flat" size={36} caption="Gain" scaleFrom="centre" showValueBelow showValueLabel={false} />
          </div>
          <JuceKnob parameterId={`${base}freq`} variant="flat" size={36} caption="Freq" showValueBelow showValueLabel={false} />
          <JuceKnob parameterId={`${base}q`} variant="flat" size={36} caption="Q" showValueBelow showValueLabel={false} />
        </div>
      </div>
    </>
  );
}

// Every knob on the dialog is a 42px dial: the flat variant draws its dial
// six wider than the `size` it is given (Knob's dialSize), hence 36.
const SIMPLE_IDS = ["eq.low", "eq.mid", "eq.high"];

function EqSimple() {
  const gains = SIMPLE_IDS.map((id) => useJuceScaledValue(id, 0)); // eslint-disable-line react-hooks/rules-of-hooks
  const bands = EQ_SIMPLE.map((s, i) => ({
    label: s.label[0],
    colourIndex: [0, 2, 4][i],
    on: true,
    type: s.type,
    hz: s.hz,
    q: s.q,
    gain: gains[i][0],
    setGain: gains[i][1],
    begin: () => gains[i][2].sliderDragStarted(),
    end: () => gains[i][2].sliderDragEnded(),
  }));
  const [selected, setSelected] = useState(-1);

  return (
    <>
      <div className="pui-eq__screen">
        <EqGraph bands={bands} selected={selected} onSelect={setSelected} />
      </div>

      <div className="pui-eq__simple">
        {EQ_SIMPLE.map((s, i) => (
          <div
            key={s.label}
            className="pui-eq__simple-band"
            style={{ "--pui-accent": bandColour(bands[i].colourIndex), "--pui-soft-lit": bandColour(bands[i].colourIndex) }}
          >
            <JuceKnob parameterId={SIMPLE_IDS[i]} variant="flat" size={36} caption={s.label} scaleFrom="centre" showValueBelow showValueLabel={false} />
            <span className="pui-eq__simple-hz">{s.caption}</span>
          </div>
        ))}
      </div>
    </>
  );
}

/** Everything behind the open dialog. Its own component so the ~40 relay
    subscriptions exist only while the dialog does. */
function EqBody({ onClose }) {
  const [on, setOn] = useJuceToggleValue("eq.on", true);
  const [mode, setMode] = useJuceChoiceValue("eq.mode", 2, 0);

  return (
    <div className="pui-eq__box" role="dialog" aria-modal="true" aria-label="Pre EQ">
      <div className="pui-eq__head">
        <PowerToggle on={on} onToggle={setOn} ariaLabel="Pre EQ" />
        <div className="pui-eq__title">Pre EQ</div>
        <div className="pui-eq__mode" role="group" aria-label="EQ mode">
          <button type="button" aria-pressed={mode === 0} onClick={() => setMode(0)}>
            Simple
          </button>
          <button type="button" aria-pressed={mode === 1} onClick={() => setMode(1)}>
            Advanced
          </button>
        </div>
        <Button className="pui-eq__close" onClick={onClose} aria-label="Close EQ">
          <svg width="10" height="10" viewBox="0 0 10 10" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" aria-hidden="true">
            <path d="M1.5 1.5l7 7M8.5 1.5l-7 7" />
          </svg>
        </Button>
      </div>

      <div className={`pui-eq__body${on ? "" : " pui-eq__body--off"}`}>{mode === 1 ? <EqAdvanced /> : <EqSimple />}</div>
    </div>
  );
}

/**
 * The header's pre-EQ, over the face - the tuner's overlay, but wider than
 * the tuner's box: a graph you drag dots around on wants the room.
 *
 * Bound straight to the processor's `eq.*` parameters (BitBit Alpine's - see
 * plugins/bitbit-alpine/src/Params.h): Simple is three fixed bands at EQ
 * Three's split, Advanced is eight parametric ones on a graph with the
 * selected band's controls fixed along the bottom, EQ Eight style. Both are
 * saved with the session and every preset; which one is heard is `eq.mode`.
 */
export default function EqDialog({ open, onClose }) {
  useEffect(() => {
    if (!open) return undefined;
    const onKey = (event) => {
      if (event.key === "Escape") onClose?.();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [open, onClose]);

  if (!open) return null;

  return (
    <div className="pui-reset pui-eq">
      <div className="pui-eq__scrim" onMouseDown={onClose} />
      <EqBody onClose={onClose} />
    </div>
  );
}
