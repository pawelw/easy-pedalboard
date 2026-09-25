import { useEffect } from "react";
import Button from "./Button.jsx";
import "./TunerDialog.css";

// The meter's geometry, in the SVG's own units. The arc is a shallow slice of
// a big circle whose centre sits well below the box, so it bows upward the way
// the reference does; +/-50 ct lands at +/-ARC_DEG either side of the top.
const W = 360;
const CX = W / 2;
const CY = 330;
const R = 280;
const ARC_DEG = 30;
const RANGE_CT = 50;

// Within this many cents the dot is "in tune" and turns green. Five is about
// where two strings stop beating audibly against each other.
const IN_TUNE_CT = 5;

function pointAt(cents) {
  const a = ((Math.max(-RANGE_CT, Math.min(RANGE_CT, cents)) / RANGE_CT) * ARC_DEG * Math.PI) / 180;
  return { x: CX + R * Math.sin(a), y: CY - R * Math.cos(a) };
}

function SpeakerIcon({ muted }) {
  return (
    <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinejoin="round" strokeLinecap="round" aria-hidden="true">
      <path d="M2.5 6h2.5l3.5-3v10l-3.5-3h-2.5z" />
      {muted ? <path d="M11 6l3.5 4M14.5 6l-3.5 4" /> : <path d="M11 5.5a3.5 3.5 0 0 1 0 5M12.75 3.75a6 6 0 0 1 0 8.5" />}
    </svg>
  );
}

/**
 * The needle half of the tuner: a +/-50 ct arc, a hollow target at 0, a dot
 * that sits where the pitch is, and the note name and cents beneath.
 *
 * `reading` is the processor's "tuner" event as it arrives - `signal`,
 * `holding`, `note`, `cents`. With no signal the dot parks at 0 and the text
 * shows dashes; while holding (the string has stopped, the last note stays up
 * for a second) everything dims.
 */
export function TunerMeter({ reading }) {
  const signal = !!reading?.signal;
  const cents = signal ? reading.cents : 0;
  const inTune = signal && Math.abs(cents) <= IN_TUNE_CT;
  const left = pointAt(-RANGE_CT);
  const right = pointAt(RANGE_CT);
  const top = pointAt(0);
  const dot = pointAt(cents);
  const state = !signal ? "idle" : inTune ? "in" : "out";

  return (
    <div className={`pui-tuner__screen pui-tuner--${state}${reading?.holding ? " pui-tuner--holding" : ""}`}>
      <svg className="pui-tuner__arc" viewBox={`0 0 ${W} 110`} aria-hidden="true">
        <path d={`M ${left.x} ${left.y} A ${R} ${R} 0 0 1 ${right.x} ${right.y}`} className="pui-tuner__track" />
        <text x={top.x} y={top.y - 22} className="pui-tuner__scale" textAnchor="middle">
          0
        </text>
        <text x={left.x - 4} y={left.y + 22} className="pui-tuner__scale" textAnchor="middle">
          -50
        </text>
        <text x={right.x + 4} y={right.y + 22} className="pui-tuner__scale" textAnchor="middle">
          +50
        </text>
        <circle cx={top.x} cy={top.y} r="11" className="pui-tuner__target" />
        {signal && <circle cx={dot.x} cy={dot.y} r="12" className="pui-tuner__dot" />}
      </svg>

      <div className="pui-tuner__readout" aria-live="polite">
        <span className="pui-tuner__note">
          {signal ? reading.note : "–"}
          {signal && <sub className="pui-tuner__octave">{reading.octave}</sub>}
        </span>
        <span className="pui-tuner__cents">{signal ? `${cents > 0 ? "+" : ""}${cents.toFixed(1)} ct` : "– ct"}</span>
      </div>
    </div>
  );
}

/**
 * The header's tuner, over the face - the same fixed overlay the preset save
 * dialog uses, for the same reason (see PresetSaveDialog). Standard pitch only
 * (A4 = 440 Hz); there is nothing to set but the mute.
 *
 * `mute` silences the pedal's output while this is open, and only then - the
 * processor ignores it once the tuner closes, so a forgotten mute can never
 * outlive the dialog.
 */
export default function TunerDialog({ open, reading, mute = false, onMute, onClose }) {
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
    <div className="pui-reset pui-tuner">
      <div className="pui-tuner__scrim" onMouseDown={onClose} />

      <div className="pui-tuner__box" role="dialog" aria-modal="true" aria-label="Tuner">
        <div className="pui-tuner__head">
          <div className="pui-tuner__title">Tuner</div>
          <Button
            className={`pui-tuner__mute${mute ? " pui-tuner__mute--on" : ""}`}
            onClick={() => onMute?.(!mute)}
            aria-pressed={mute}
            title={mute ? "Output muted while tuning - click to hear it" : "Mute the output while tuning"}
          >
            <SpeakerIcon muted={mute} />
            <span>{mute ? "Muted" : "Mute"}</span>
          </Button>
          <Button className="pui-tuner__close" onClick={onClose} aria-label="Close tuner">
            <svg width="10" height="10" viewBox="0 0 10 10" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" aria-hidden="true">
              <path d="M1.5 1.5l7 7M8.5 1.5l-7 7" />
            </svg>
          </Button>
        </div>

        <TunerMeter reading={reading} />
      </div>
    </div>
  );
}
