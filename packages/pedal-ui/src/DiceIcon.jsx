/**
 * A die showing five - the preset bar's randomise button. Drawn on the same
 * 16-unit grid and 1.5 stroke as SaveIcon's chrome variant, so the two read as
 * a pair side by side; the pips are filled rather than stroked, because a
 * 1.5-unit ring at 14px closes up into a dot anyway and a smaller solid one
 * leaves the face room to read as a face.
 */
export default function DiceIcon({ size = 14 }) {
  return (
    <svg width={size} height={size} viewBox="0 0 16 16" fill="none" aria-hidden="true">
      <rect x="2.25" y="2.25" width="11.5" height="11.5" rx="2.5" stroke="currentColor" strokeWidth="1.5" />
      <g fill="currentColor">
        <circle cx="5.6" cy="5.6" r="1.05" />
        <circle cx="10.4" cy="5.6" r="1.05" />
        <circle cx="8" cy="8" r="1.05" />
        <circle cx="5.6" cy="10.4" r="1.05" />
        <circle cx="10.4" cy="10.4" r="1.05" />
      </g>
    </svg>
  );
}
