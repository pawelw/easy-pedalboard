import { useEffect, useRef, useState } from "react";
import "./MiniSlider.css";

/**
 * A compact horizontal fader: a caption, a short track, and the live value,
 * all on one line. Controlled (0..1), drag or arrow keys to move.
 *
 * Not a variant of Slider.jsx, which is the full-size vertical fader with a
 * tick ladder and a moulded thumb - a control you reach for. This is the one
 * you set once and read at a glance, so it is sized to sit in a card header
 * next to the title rather than in a control row.
 *
 * JUCE-agnostic, like Knob and Slider: a plugin's jsui supplies
 * value/onChange itself.
 */
export default function MiniSlider({
  value,
  onChange,
  onDragStart,
  onDragEnd,
  label,
  valueLabel,
  width = 74,
  step = 0.02,
}) {
  const [dragging, setDragging] = useState(false);
  const trackRef = useRef(null);

  // onChange/onDragEnd are fresh closures every render, so the drag-tracking
  // effect reads them through a ref rather than depending on them directly -
  // see the matching comment in Knob.jsx for why depending on them there tore
  // the drag down after the first pixel of real mouse movement.
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
    if (event.key === "ArrowUp" || event.key === "ArrowRight") {
      event.preventDefault();
      onChange(Math.min(1, value + step));
    } else if (event.key === "ArrowDown" || event.key === "ArrowLeft") {
      event.preventDefault();
      onChange(Math.max(0, value - step));
    }
  };

  const filled = `${Math.min(1, Math.max(0, value)) * 100}%`;

  return (
    <div className="pui-reset pui-minislider">
      {label && <span className="pui-minislider__label">{label}</span>}

      <div
        className={"pui-minislider__track" + (dragging ? " pui-minislider__track--dragging" : "")}
        style={{ width }}
        ref={trackRef}
        onPointerDown={onPointerDown}
        onKeyDown={onKeyDown}
        role="slider"
        tabIndex={0}
        aria-label={label}
        aria-valuemin={0}
        aria-valuemax={1}
        aria-valuenow={value}
        aria-valuetext={valueLabel}
        aria-orientation="horizontal"
      >
        <span className="pui-minislider__fill" style={{ width: filled }} />
        <span className="pui-minislider__thumb" style={{ left: filled }} />
      </div>

      {valueLabel && <span className="pui-minislider__value">{valueLabel}</span>}
    </div>
  );
}
