import { useEffect, useRef, useState } from "react";
import "./Slider.css";

// The full-size fader: the tick ladder and moulded thumb Peak EQ's bands are
// built from.
const TICK_COUNT = 9;
const THUMB_LONG = 22; // along the travel
const THUMB_SHORT = 26; // across it

// The compact one: a round grip on a part-filled rail, no ladder behind it,
// sized to sit on a single line of a card header rather than in a control row.
// A different look from the full-size fader, but the same control underneath -
// one implementation, so both feel the same to drag.
// The grip is a 13px circle; the track around it stays 18px so there is still
// a comfortable amount of it to press. The two are separate numbers because
// the pointer target and the thing you can see are not the same size here -
// the full-size fader's cap fills its track, this one does not.
const COMPACT_THUMB_LONG = 13;
const COMPACT_THUMB_SHORT = 18;

// How far the pointer travels for the whole range in `fine` mode. The same
// figure Knob uses, deliberately: a fine fader is being asked to behave like a
// knob, so it should take the same amount of hand movement to cross.
const PIXELS_PER_FULL_SWEEP = 200;

/**
 * A fader: controlled (0..1), drag or arrow keys to move, double-click to
 * centre.
 *
 * `orientation`: "vertical" (default, 1 = top) is Peak EQ's band fader.
 * "horizontal" (1 = right) is the same control on its side.
 *
 * `compact`: the header variant - a circular grip on a rail that fills up to
 * it, no tick ladder. This is the whole of what used to be a second, separate
 * MiniSlider component - which had a 4px-tall track as its only pointer target,
 * so the visible thumb overhung the thing you actually had to hit and a click
 * on the dot itself often did nothing. The pointer target here is the whole
 * track box, thumb included, at whatever size the variant draws.
 *
 * `fine`: drag relative to where the press started, at PIXELS_PER_FULL_SWEEP,
 * rather than jumping to the pointer and tracking it one-for-one. A short
 * header fader is otherwise brutally coarse - 74px of travel for a 36 dB range
 * is half a decibel per pixel - and this makes it a knob you happen to be
 * pushing sideways instead. The cursor is locked for the drag where the browser
 * allows it, the same way Knob does, so the hand never runs out of track.
 *
 * `onReset`: what a double-click does, when the middle of the travel is not the
 * right answer. A gain fader should return to 0 dB, which is only the middle if
 * the range happens to be symmetrical - and only the caller knows the range, so
 * only the caller can work out where that is. Falls back to `centreValue`.
 *
 * JUCE-agnostic, like Knob - a plugin's jsui supplies value/onChange itself.
 */
export default function Slider({
  value,
  onChange,
  onDragStart,
  onDragEnd,
  caption,
  label,
  valueLabel,
  length = 180,
  orientation = "vertical",
  compact = false,
  fine = false,
  step = 0.01,
  centreValue = 0.5,
  onReset,
}) {
  const [dragging, setDragging] = useState(false);
  const trackRef = useRef(null);
  const dragStartRef = useRef(null);

  const horizontal = orientation === "horizontal";
  const thumbLong = compact ? COMPACT_THUMB_LONG : THUMB_LONG;
  const thumbShort = compact ? COMPACT_THUMB_SHORT : THUMB_SHORT;

  // Across the travel: the full-size cap spans the track, the compact grip is
  // a circle and so is as wide as it is long.
  const thumbCross = compact ? COMPACT_THUMB_LONG : THUMB_SHORT;
  const travel = length - thumbLong;

  // onChange/onDragEnd are fresh closures every render, so the drag-tracking
  // effect below reads them through a ref rather than depending on them
  // directly - see the matching comment in Knob.jsx for why depending on them
  // there tore the drag down after the first pixel of real mouse movement (a
  // bug a synthetic single-jump test never exposed).
  const latest = useRef({ onChange, onDragEnd, travel, horizontal, thumbLong, fine, value });
  latest.current = { onChange, onDragEnd, travel, horizontal, thumbLong, fine, value };

  const valueFromPointer = (event) => {
    const rect = trackRef.current.getBoundingClientRect();
    const { travel: t, horizontal: h, thumbLong: long } = latest.current;

    // Measured from the middle of the thumb, so the point under the cursor is
    // the value rather than the thumb's leading edge.
    const along = h ? event.clientX - rect.left - long / 2 : event.clientY - rect.top - long / 2;
    const fraction = Math.min(1, Math.max(0, along / t));

    // Vertical faders read 1 at the top, which is the low end of clientY.
    return h ? fraction : 1 - fraction;
  };

  // Relative travel, for `fine`. Once the OS cursor is locked to the track it
  // stops moving at all, so clientX/Y stay frozen at wherever the press landed
  // and the delta has to come from movementX/Y instead - folded onto the last
  // committed value rather than measured against a fixed start point. Same
  // shape as Knob's, for the same reason.
  const valueFromDrag = (event) => {
    const start = dragStartRef.current;
    if (!start) return latest.current.value;

    const { horizontal: h, value: current } = latest.current;
    const locked = document.pointerLockElement === trackRef.current;
    const moved = locked
      ? h
        ? event.movementX
        : -event.movementY
      : h
        ? event.clientX - start.x
        : start.y - event.clientY;

    const base = locked ? current : start.value;
    return Math.min(1, Math.max(0, base + moved / PIXELS_PER_FULL_SWEEP));
  };

  useEffect(() => {
    if (!dragging) return undefined;

    const handlePointerMove = (event) =>
      latest.current.onChange(latest.current.fine ? valueFromDrag(event) : valueFromPointer(event));

    const handlePointerUp = () => {
      dragStartRef.current = null;
      setDragging(false);
      latest.current.onDragEnd?.();
      if (document.pointerLockElement === trackRef.current) document.exitPointerLock?.();
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
    dragStartRef.current = { x: event.clientX, y: event.clientY, value };
    setDragging(true);

    if (fine) {
      // No jump to the pointer: a fine fader is grabbed where it is and moved,
      // and a press that also re-set the value would undo the point of it.
      trackRef.current?.requestPointerLock?.()?.catch(() => {});
      return;
    }

    onChange(valueFromPointer(event));
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

  // The same gesture a knob answers: back to a resting position, and reported
  // to the host as its own little drag so it lands in the undo history as one
  // move rather than as a value that changed with nothing around it.
  const onDoubleClick = (event) => {
    event.preventDefault();
    onDragStart?.();

    if (onReset) onReset();
    else onChange(centreValue);

    onDragEnd?.();
  };

  // The ladder is the full-size fader's; the compact one is a plain rail.
  const ticks = [];
  if (!compact) for (let i = 0; i < TICK_COUNT; i++) ticks.push(<div key={i} className="pui-fader__tick" />);

  const offset = (horizontal ? value : 1 - value) * travel;
  const filled = `${Math.min(1, Math.max(0, value)) * 100}%`;

  const className =
    "pui-reset pui-fader" +
    (horizontal ? " pui-fader--horizontal" : "") +
    (compact ? " pui-fader--compact" : "");

  return (
    <div className={className}>
      {label && <span className="pui-fader__label">{label}</span>}

      <div
        className={"pui-fader__track" + (dragging ? " pui-fader__track--dragging" : "")}
        ref={trackRef}
        style={horizontal ? { width: length, height: thumbShort } : { height: length, width: thumbShort }}
        onPointerDown={onPointerDown}
        onKeyDown={onKeyDown}
        onDoubleClick={onDoubleClick}
        role="slider"
        tabIndex={0}
        aria-label={caption || label}
        aria-valuemin={0}
        aria-valuemax={1}
        aria-valuenow={value}
        aria-valuetext={valueLabel}
        aria-orientation={orientation}
      >
        {!compact && <div className="pui-fader__ticks pui-fader__ticks--near">{ticks}</div>}

        <div className="pui-fader__rail">
          {/* How far up the travel the value is, drawn on the rail itself -
              only the compact variant has one; the full-size fader says it
              with the ladder and the cap's position instead. */}
          {compact && (
            <div
              className="pui-fader__fill"
              style={horizontal ? { width: filled } : { height: filled }}
            />
          )}
        </div>

        {!compact && <div className="pui-fader__ticks pui-fader__ticks--far">{ticks}</div>}

        {/* Drawn, not hit: every pointer event belongs to the track, so there
            is no way to press the visible thumb and miss the control. */}
        <div
          className="pui-fader__thumb"
          style={
            horizontal
              ? { left: offset, width: thumbLong, height: thumbCross }
              : { top: offset, height: thumbLong, width: thumbCross }
          }
        />
      </div>

      {valueLabel && <span className="pui-fader__value">{valueLabel}</span>}
      {caption && <div className="pui-caption pui-fader__caption">{caption}</div>}
    </div>
  );
}
