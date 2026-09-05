import "./Readout.css";

/**
 * A recessed value display - the flat-panel equivalent of a knob's own
 * printed value, used where the control is a small knob paired with a
 * separate readout (Delay's Time rows: a 42px knob turns it, this shows
 * it). Purely presentational; the caller owns formatting `value`/`unit`.
 */
export default function Readout({ label, value, unit }) {
  return (
    <div className="pui-reset pui-readout">
      <span className="pui-readout__label">{label}</span>
      <span className="pui-readout__value">{value}</span>
      {unit && <span className="pui-readout__unit">{unit}</span>}
    </div>
  );
}
