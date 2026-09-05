import { useEffect, useMemo, useRef, useState } from "react";
import "./Knob.css";

const MIN_ANGLE = -135;
const MAX_ANGLE = 135;
const PIXELS_PER_FULL_SWEEP = 200;

// The value sweep, in pixels off the knob's rim so it keeps the same visual
// gap at every knob size.
const SWEEP_GAP = 6;
const SWEEP_WIDTH = 3.2;

// The collar, as fractions of the knob radius. The teeth are circles buried
// deep in the ring so only a shallow cap of each one shows: that gives wide
// scallops parted by narrow notches, the reference's edge rather than a knurl.
const TOOTH_COUNT = 24;
const RING_OUTER = 0.915;
const TOOTH_CENTRE = 0.775;
const TOOTH_RADIUS = 0.18;
const COLLAR_INNER = 0.79; // where the cap starts

function angleFor(value01) {
  return MIN_ANGLE + value01 * (MAX_ANGLE - MIN_ANGLE);
}

function arcPath(r, fromDeg, toDeg) {
  const toRad = (d) => ((d - 90) * Math.PI) / 180;
  const x1 = Math.cos(toRad(fromDeg)) * r;
  const y1 = Math.sin(toRad(fromDeg)) * r;
  const x2 = Math.cos(toRad(toDeg)) * r;
  const y2 = Math.sin(toRad(toDeg)) * r;
  return `M ${x1} ${y1} A ${r} ${r} 0 ${toDeg - fromDeg > 180 ? 1 : 0} 1 ${x2} ${y2}`;
}

/** The collar: one flat dark scalloped ring. No facet shading - it reads as a
    moulded plastic collar, one colour, and the light comes from the cap
    sitting proud of it. It's a physical part of the knob, not a fixed bezel
    around it, so it turns with `angle` - the cap's own gradient stays put,
    since that's a fixed light source's reflection, not something that
    should spin with the plastic underneath it. */
function Collar({ radius, angle }) {
  const teeth = useMemo(() => {
    const marks = [];
    for (let i = 0; i < TOOTH_COUNT; i++) {
      const a = (i / TOOTH_COUNT) * Math.PI * 2;
      marks.push(
        <circle key={i} cx={Math.cos(a) * radius * TOOTH_CENTRE} cy={Math.sin(a) * radius * TOOTH_CENTRE} r={radius * TOOTH_RADIUS} />,
      );
    }
    return marks;
  }, [radius]);

  return (
    <svg
      className="pui-knob__collar"
      viewBox={`${-radius} ${-radius} ${radius * 2} ${radius * 2}`}
      width={radius * 2}
      height={radius * 2}
      style={{ transform: `rotate(${angle}deg)` }}
    >
      <g fill="var(--pui-knob-body)">
        <circle cx="0" cy="0" r={radius * RING_OUTER} />
        {teeth}
      </g>
      <circle
        cx="0"
        cy="0"
        r={radius * (COLLAR_INNER + 0.015)}
        fill="none"
        stroke="var(--pui-knob-body-hi)"
        strokeWidth={radius * 0.03}
      />
    </svg>
  );
}

/** The value readout: one continuous arc outside the knob, a pale track with
    the part up to the value lit. Outside rather than on the collar - a light
    line on the dark ring reads as part of the knob, not as its value. */
function Sweep({ diameter, value }) {
  const r = diameter / 2 + SWEEP_GAP;
  const box = r + SWEEP_WIDTH;

  return (
    <svg
      className="pui-knob__sweep"
      viewBox={`${-box} ${-box} ${box * 2} ${box * 2}`}
      width={box * 2}
      height={box * 2}
    >
      <g fill="none" strokeWidth={SWEEP_WIDTH} strokeLinecap="round">
        <path d={arcPath(r, MIN_ANGLE, MAX_ANGLE)} stroke="var(--pui-knob-sweep)" />
        {value > 0.004 && <path d={arcPath(r, MIN_ANGLE, angleFor(value))} stroke="var(--pui-knob-sweep-lit)" />}
      </g>
    </svg>
  );
}

/** A fixed label printed just outside the arc's top end, the way hardware
    prints a mark ("MAX", an infinity sign) next to a knob's end of travel -
    always there, not tied to the current value the way the lit sweep is. */
function EndMarker({ label, radius, lit }) {
  if (!label) return null;
  const r = radius + SWEEP_GAP + SWEEP_WIDTH + 7;
  const toRad = (d) => ((d - 90) * Math.PI) / 180;
  const x = Math.cos(toRad(MAX_ANGLE)) * r;
  const y = Math.sin(toRad(MAX_ANGLE)) * r;
  return (
    <span
      className={`pui-knob__end-marker${lit ? " pui-knob__end-marker--lit" : ""}`}
      style={{ left: `calc(50% + ${x}px)`, top: `calc(50% + ${y}px)` }}
    >
      {label}
    </span>
  );
}

/**
 * A rotary knob: controlled (0..1), drag-vertically to turn, arrow keys to
 * nudge. JUCE-agnostic - a plugin's jsui wires this to a WebSliderRelay by
 * passing value/onChange/onDragStart/onDragEnd itself.
 */
export default function Knob({
  value,
  onChange,
  onDragStart,
  onDragEnd,
  caption,
  valueLabel,
  size = 72,
  cornerLabels,
  icon,
  endMarkerLabel,
  step = 0.01,
}) {
  const [dragging, setDragging] = useState(false);
  const dragStartRef = useRef(null);
  const bodyRef = useRef(null);

  // onChange/onDragEnd are fresh closures every render (each carries the
  // caller's own current `value`), so the drag-tracking effect below reads
  // them through a ref instead of depending on them directly. It used to
  // depend on them: every onChange call re-rendered the parent with a new
  // onChange identity, which re-ran this effect's cleanup mid-drag - nulling
  // dragStartRef and tearing down the window listeners after the first
  // pixel of movement. A single big jump (as in a synthetic test) never
  // triggers a second pointermove and so never exposed it; an actual mouse
  // drag, which fires many, stopped dead after the first one.
  const latest = useRef({ onChange, onDragEnd, value });
  latest.current = { onChange, onDragEnd, value };

  useEffect(() => {
    if (!dragging) return undefined;

    const handlePointerMove = (event) => {
      const start = dragStartRef.current;
      if (!start) return;
      // Once the OS cursor is locked to the knob (see onPointerDown), it no
      // longer moves at all - clientY stays frozen at wherever it was on
      // pointer-down, so the delta has to come from movementY (the raw
      // relative motion the OS still reports) instead, folded onto the last
      // committed value rather than measured against a fixed start point.
      const locked = document.pointerLockElement === bodyRef.current;
      const raw = locked ? -event.movementY : start.y - event.clientY;
      const base = locked ? latest.current.value : start.value;
      latest.current.onChange(Math.min(1, Math.max(0, base + raw / PIXELS_PER_FULL_SWEEP)));
    };

    const handlePointerUp = () => {
      dragStartRef.current = null;
      setDragging(false);
      latest.current.onDragEnd?.();
      if (document.pointerLockElement === bodyRef.current)
        document.exitPointerLock?.();
    };

    window.addEventListener("pointermove", handlePointerMove);
    window.addEventListener("pointerup", handlePointerUp);
    return () => {
      window.removeEventListener("pointermove", handlePointerMove);
      window.removeEventListener("pointerup", handlePointerUp);
    };
  }, [dragging]);

  const onPointerDown = (event) => {
    event.preventDefault();
    onDragStart?.();
    dragStartRef.current = { y: event.clientY, value };
    setDragging(true);
    // Locks the OS cursor in place for the rest of the drag, the way a
    // hardware knob's own travel isn't tied to how far your hand moves -
    // falls back to plain (cursor-visible, unfrozen) dragging above wherever
    // the browser refuses the lock, so this is never required for dragging
    // to work.
    bodyRef.current?.requestPointerLock?.()?.catch(() => {});
  };

  const onKeyDown = (event) => {
    if (event.key === "ArrowUp" || event.key === "ArrowRight") {
      event.preventDefault();
      onChange(Math.min(1, value + step));
    } else if (event.key === "ArrowDown" || event.key === "ArrowLeft") {
      event.preventDefault();
      onChange(Math.max(0, value - step));
    }
  };

  const angle = angleFor(value);
  const radius = size / 2;

  return (
    <div className="pui-reset pui-knob" style={{ width: size + 28 }}>
      <div className="pui-knob__dial" style={{ width: size, height: size }}>
        <Sweep diameter={size} value={value} />
        <EndMarker label={endMarkerLabel} radius={radius} lit={value >= 0.999} />

        {cornerLabels?.topLeft && <span className="pui-knob__corner pui-knob__corner--tl">{cornerLabels.topLeft}</span>}
        {cornerLabels?.topRight && <span className="pui-knob__corner pui-knob__corner--tr">{cornerLabels.topRight}</span>}
        {cornerLabels?.bottomLeft && (
          <span className="pui-knob__corner pui-knob__corner--bl">{cornerLabels.bottomLeft}</span>
        )}
        {cornerLabels?.bottomRight && (
          <span className="pui-knob__corner pui-knob__corner--br">{cornerLabels.bottomRight}</span>
        )}

        <div
          ref={bodyRef}
          className={`pui-knob__body${dragging ? " pui-knob__body--dragging" : ""}`}
          style={{ width: size, height: size }}
          onPointerDown={onPointerDown}
          onKeyDown={onKeyDown}
          role="slider"
          tabIndex={0}
          aria-label={caption}
          aria-valuemin={0}
          aria-valuemax={1}
          aria-valuenow={value}
        >
          <Collar radius={radius} angle={angle} />
          <div className="pui-knob__cap">
            <div className="pui-knob__dot" style={{ transform: `rotate(${angle}deg)` }} />
            {icon && <div className="pui-knob__icon">{icon(value)}</div>}
          </div>
        </div>
      </div>

      {/* The value replaces the caption in place while dragging, rather than
          appearing as a second line below it - a second line meant the row's
          height (and everything below it in the grid) changed the instant
          you touched a knob. */}
      <div className="pui-caption pui-knob__caption">{dragging && valueLabel ? valueLabel : caption}</div>
    </div>
  );
}
