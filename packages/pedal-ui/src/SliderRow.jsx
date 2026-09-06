import { useEffect, useRef, useState } from "react";
import "./SliderRow.css";

/**
 * A horizontal slider - Delay's Tape/Mod stage strip. A different shape
 * from the vertical `Slider` (which Peak EQ/Wah's faces are built around),
 * not a mode of it - do not change Slider's own rendering for this. Drag
 * anywhere along the track (not just the thumb), arrow keys nudge, same
 * controlled 0..1 API as every other control here.
 */
export default function SliderRow({ name, value, onChange, onDragStart, onDragEnd, valueLabel, step = 0.01 }) {
  const [dragging, setDragging] = useState(false);
  const trackRef = useRef(null);

  // See Knob.jsx's matching comment: onChange/onDragEnd are fresh closures
  // every render, so the drag-tracking effect reads them through a ref
  // instead of depending on them directly.
  const latest = useRef({ onChange, onDragEnd });
  latest.current = { onChange, onDragEnd };

  const valueFromClientX = (clientX) => {
    const rect = trackRef.current.getBoundingClientRect();
    return Math.min(1, Math.max(0, (clientX - rect.left) / rect.width));
  };

  useEffect(() => {
    if (!dragging) return undefined;

    const handlePointerMove = (event) => latest.current.onChange(valueFromClientX(event.clientX));
    const handlePointerUp = () => {
      setDragging(false);
      latest.current.onDragEnd?.();
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
    setDragging(true);
    onChange(valueFromClientX(event.clientX));
  };

  const onKeyDown = (event) => {
    if (event.key === "ArrowRight" || event.key === "ArrowUp") {
      event.preventDefault();
      onChange(Math.min(1, value + step));
    } else if (event.key === "ArrowLeft" || event.key === "ArrowDown") {
      event.preventDefault();
      onChange(Math.max(0, value - step));
    }
  };

  return (
    <div className="pui-reset pui-slider-row">
      <span className="pui-slider-row__name">{name}</span>

      <div
        ref={trackRef}
        className="pui-slider-row__track"
        onPointerDown={onPointerDown}
        onKeyDown={onKeyDown}
        role="slider"
        tabIndex={0}
        aria-label={name}
        aria-valuemin={0}
        aria-valuemax={1}
        aria-valuenow={value}
      >
        <div className="pui-slider-row__fill" style={{ width: `${value * 100}%` }} />
        <div
          className={`pui-slider-row__thumb${dragging ? " pui-slider-row__thumb--dragging" : ""}`}
          style={{ left: `${value * 100}%` }}
        />
      </div>

      {valueLabel && <span className="pui-slider-row__value">{valueLabel}</span>}
    </div>
  );
}
