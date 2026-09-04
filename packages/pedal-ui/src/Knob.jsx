import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import "./Knob.css";

const MIN_ANGLE = -135;
const MAX_ANGLE = 135;
const PIXELS_PER_FULL_SWEEP = 200;
const TICK_COUNT = 11;
const RIDGE_COUNT = 56;
const LIGHT_ANGLE_DEG = -135; // upper-left, matches the reference photo

const RIDGE_DARK = [20, 20, 18];
const RIDGE_MID = [45, 45, 41];
const RIDGE_LIGHT = [92, 90, 82];

function angleFor(value01) {
  return MIN_ANGLE + value01 * (MAX_ANGLE - MIN_ANGLE);
}

function lerp3(a, b, t) {
  return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
}

/** One ridge's shade: a two-stop lerp through a shadow floor so the lit side
    and the shadow side both read as distinct facets, not a flat gradient. */
function ridgeColor(angleDeg) {
  const diff = ((angleDeg - LIGHT_ANGLE_DEG + 540) % 360) - 180; // -180..180
  const lit = 1 - Math.abs(diff) / 180; // 1 at the light, 0 opposite it
  const [r, g, b] = lit > 0.5 ? lerp3(RIDGE_MID, RIDGE_LIGHT, (lit - 0.5) * 2) : lerp3(RIDGE_DARK, RIDGE_MID, lit * 2);
  return `rgb(${r | 0}, ${g | 0}, ${b | 0})`;
}

/** The knurled ring: many short radial ridges over a dark base disc, shaded
    by a single fixed light direction so it reads as one lit metal edge
    rather than a flat repeating pattern. */
function Knurl({ radius }) {
  const ridges = useMemo(() => {
    const inner = radius * 0.74;
    const marks = [];
    for (let i = 0; i < RIDGE_COUNT; i++) {
      const angleDeg = (i / RIDGE_COUNT) * 360;
      const angle = (angleDeg * Math.PI) / 180;
      marks.push(
        <line
          key={i}
          x1={Math.cos(angle) * inner}
          y1={Math.sin(angle) * inner}
          x2={Math.cos(angle) * radius}
          y2={Math.sin(angle) * radius}
          stroke={ridgeColor(angleDeg)}
        />,
      );
    }
    return marks;
  }, [radius]);

  return (
    <svg className="pui-knob__knurl" viewBox={`${-radius} ${-radius} ${radius * 2} ${radius * 2}`} width={radius * 2} height={radius * 2}>
      <circle cx="0" cy="0" r={radius} fill="var(--pui-knob-body)" />
      <g strokeWidth={radius * 0.16} strokeLinecap="round">
        {ridges}
      </g>
      <circle cx="0" cy="0" r={radius * 0.73} fill="none" stroke="rgba(0,0,0,0.55)" strokeWidth="1" />
    </svg>
  );
}

function Ticks({ diameter }) {
  const r = diameter / 2 + 9;
  const marks = [];
  for (let i = 0; i < TICK_COUNT; i++) {
    const t = i / (TICK_COUNT - 1);
    const angleDeg = angleFor(t) - 90;
    const angle = (angleDeg * Math.PI) / 180;
    const x1 = Math.cos(angle) * (r - 3.5);
    const y1 = Math.sin(angle) * (r - 3.5);
    const x2 = Math.cos(angle) * r;
    const y2 = Math.sin(angle) * r;
    marks.push(<line key={i} x1={x1} y1={y1} x2={x2} y2={y2} />);
  }
  return (
    <svg className="pui-knob__ticks" viewBox={`${-r} ${-r} ${r * 2} ${r * 2}`} width={r * 2} height={r * 2}>
      <g stroke="var(--pui-knob-tick)" strokeWidth="1.3" strokeLinecap="round">
        {marks}
      </g>
    </svg>
  );
}

/** The lit progress arc laid over the ring's outer edge, from the knob's
    minimum up to its current value - not decorative, it is the knob's
    at-a-glance reading (see the "analog" control style in CLAUDE.md). Drawn
    on top of the dark knurl (not outside it, against the pale panel) so it
    reads as a lit facet the way it does in hardware photos. */
function ValueArc({ radius, value }) {
  const r = radius * 0.95;
  const startDeg = MIN_ANGLE - 90;
  const endDeg = angleFor(value) - 90;
  const toRad = (d) => (d * Math.PI) / 180;
  const x1 = Math.cos(toRad(startDeg)) * r;
  const y1 = Math.sin(toRad(startDeg)) * r;
  const x2 = Math.cos(toRad(endDeg)) * r;
  const y2 = Math.sin(toRad(endDeg)) * r;
  const largeArc = endDeg - startDeg > 180 ? 1 : 0;

  if (value <= 0.002) return null;

  return (
    <svg className="pui-knob__value-arc" viewBox={`${-radius} ${-radius} ${radius * 2} ${radius * 2}`} width={radius * 2} height={radius * 2}>
      <path
        d={`M ${x1} ${y1} A ${r} ${r} 0 ${largeArc} 1 ${x2} ${y2}`}
        fill="none"
        stroke="var(--pui-knob-value-arc)"
        strokeWidth={radius * 0.06}
        strokeLinecap="round"
      />
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
        <Ticks diameter={size} />
        <ValueArc radius={radius} value={value} />

        {cornerLabels?.topLeft && <span className="pui-knob__corner pui-knob__corner--tl">{cornerLabels.topLeft}</span>}
        {cornerLabels?.topRight && <span className="pui-knob__corner pui-knob__corner--tr">{cornerLabels.topRight}</span>}
        {cornerLabels?.bottomLeft && (
          <span className="pui-knob__corner pui-knob__corner--bl">{cornerLabels.bottomLeft}</span>
        )}
        {cornerLabels?.bottomRight && (
          <span className="pui-knob__corner pui-knob__corner--br">{cornerLabels.bottomRight}</span>
        )}

        <div
          className={"pui-knob__body" + (dragging ? " pui-knob__body--dragging" : "")}
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
          <Knurl radius={radius} />
          <div className="pui-knob__cap">
            <div className="pui-knob__dot" style={{ transform: `rotate(${angle}deg)` }} />
          </div>
        </div>
      </div>

      {caption && <div className="pui-caption pui-knob__caption">{caption}</div>}
      <div className="pui-knob__readout">{dragging ? valueLabel : " "}</div>
    </div>
  );
}
