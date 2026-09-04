import { useCallback, useEffect, useRef, useState } from "react";
import "./Slider.css";

const TICK_COUNT = 9;
const THUMB_HEIGHT = 22;

/**
 * A vertical fader: controlled (0..1, 1 = top), drag or arrow keys to move.
 * JUCE-agnostic, like Knob - a plugin's jsui supplies value/onChange itself.
 */
export default function Slider({ value, onChange, onDragStart, onDragEnd, caption, height = 180, step = 0.01 }) {
  const [dragging, setDragging] = useState(false);
  const trackRef = useRef(null);

  const travel = height - THUMB_HEIGHT;

  const valueFromClientY = useCallback(
    (clientY) => {
      const rect = trackRef.current.getBoundingClientRect();
      const y = clientY - rect.top - THUMB_HEIGHT / 2;
      return Math.min(1, Math.max(0, 1 - y / travel));
    },
    [travel],
  );

  const handlePointerMove = useCallback(
    (event) => onChange(valueFromClientY(event.clientY)),
    [onChange, valueFromClientY],
  );

  const handlePointerUp = useCallback(() => {
    setDragging(false);
    onDragEnd?.();
    window.removeEventListener("pointermove", handlePointerMove);
    window.removeEventListener("pointerup", handlePointerUp);
  }, [handlePointerMove, onDragEnd]);

  useEffect(() => () => handlePointerUp(), [handlePointerUp]);

  const onPointerDown = (event) => {
    event.preventDefault();
    onDragStart?.();
    setDragging(true);
    onChange(valueFromClientY(event.clientY));
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

  const ticks = [];
  for (let i = 0; i < TICK_COUNT; i++) {
    ticks.push(<div key={i} className="pui-fader__tick" />);
  }

  const thumbTop = (1 - value) * travel;

  return (
    <div className="pui-reset pui-fader" style={{ height: height + 20 }}>
      <div className="pui-fader__track" ref={trackRef} style={{ height }}>
        <div className="pui-fader__ticks pui-fader__ticks--left">{ticks}</div>
        <div className="pui-fader__rail" />
        <div className="pui-fader__ticks pui-fader__ticks--right">{ticks}</div>

        <div
          className={"pui-fader__thumb" + (dragging ? " pui-fader__thumb--dragging" : "")}
          style={{ top: thumbTop, height: THUMB_HEIGHT }}
          onPointerDown={onPointerDown}
          onKeyDown={onKeyDown}
          role="slider"
          tabIndex={0}
          aria-label={caption}
          aria-valuemin={0}
          aria-valuemax={1}
          aria-valuenow={value}
          aria-orientation="vertical"
        />
      </div>

      {caption && <div className="pui-caption pui-fader__caption">{caption}</div>}
    </div>
  );
}
