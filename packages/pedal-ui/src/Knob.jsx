import { useCallback, useEffect, useMemo, useRef, useState } from "react";
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
    sitting proud of it. */
function Collar({ radius }) {
  const teeth = useMemo(() => {
    const marks = [];
    for (let i = 0; i < TOOTH_COUNT; i++) {
      const angle = (i / TOOTH_COUNT) * Math.PI * 2;
      marks.push(
        <circle
          key={i}
          cx={Math.cos(angle) * radius * TOOTH_CENTRE}
          cy={Math.sin(angle) * radius * TOOTH_CENTRE}
          r={radius * TOOTH_RADIUS}
        />,
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
  step = 0.01,
}) {
  const [dragging, setDragging] = useState(false);
  const dragStartRef = useRef(null);

  const handlePointerMove = useCallback(
    (event) => {
      const start = dragStartRef.current;
      if (!start) return;
      const delta = (start.y - event.clientY) / PIXELS_PER_FULL_SWEEP;
      onChange(Math.min(1, Math.max(0, start.value + delta)));
    },
    [onChange],
  );

  const handlePointerUp = useCallback(() => {
    dragStartRef.current = null;
    setDragging(false);
    onDragEnd?.();
    window.removeEventListener("pointermove", handlePointerMove);
    window.removeEventListener("pointerup", handlePointerUp);
  }, [handlePointerMove, onDragEnd]);

  useEffect(() => () => handlePointerUp(), [handlePointerUp]);

  const onPointerDown = (event) => {
    event.preventDefault();
    onDragStart?.();
    dragStartRef.current = { y: event.clientY, value };
    setDragging(true);
    window.addEventListener("pointermove", handlePointerMove);
    window.addEventListener("pointerup", handlePointerUp);
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

        {cornerLabels?.topLeft && <span className="pui-knob__corner pui-knob__corner--tl">{cornerLabels.topLeft}</span>}
        {cornerLabels?.topRight && <span className="pui-knob__corner pui-knob__corner--tr">{cornerLabels.topRight}</span>}
        {cornerLabels?.bottomLeft && (
          <span className="pui-knob__corner pui-knob__corner--bl">{cornerLabels.bottomLeft}</span>
        )}
        {cornerLabels?.bottomRight && (
          <span className="pui-knob__corner pui-knob__corner--br">{cornerLabels.bottomRight}</span>
        )}

        <div
          className="pui-knob__body"
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
          <Collar radius={radius} />
          <div className="pui-knob__cap">
            <div className="pui-knob__dot" style={{ transform: `rotate(${angle}deg)` }} />
            {icon && <div className="pui-knob__icon">{icon(value)}</div>}
          </div>
        </div>
      </div>

      {caption && <div className="pui-caption pui-knob__caption">{caption}</div>}
      <div className="pui-knob__readout">{dragging ? valueLabel : " "}</div>
    </div>
  );
}
