/**
 * Three faders at different heights - the preset bar's pre-EQ button. Same
 * 16-unit grid and 1.5 stroke as TunerIcon and ThemeIcon, so the header's own
 * controls read as one family.
 */
export default function EqIcon({ size = 14 }) {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 16 16"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.5"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <path d="M3.5 2v12M8 2v12M12.5 2v12" />
      <path d="M2 10h3M6.5 5h3M11 8.5h3" />
    </svg>
  );
}

/**
 * One glyph per pre-EQ band shape, in ee::dsp::eq::FilterType's order - the
 * type menu's rows, after Ableton EQ Eight's. A 20x12 box: wide enough that a
 * 12 dB cut and a 48 dB one differ by their slope alone.
 */
const SHAPES = [
  "M2 11C5 11 6 3 8 3H18", // low cut 48 - near-vertical wall
  "M2 10C4 10 5 3 10 3H18", // low cut 12 - a gentle slope
  "M2 9H6C8 9 9 4 11 4H18", // low shelf
  "M2 9H5C7 9 8 3 10 3S13 9 15 9H18", // bell
  "M2 5H7C8 5 9 11 10 11S12 5 13 5H18", // notch
  "M2 4H9C11 4 12 9 14 9H18", // high shelf
  "M2 3H10C15 3 16 10 18 10", // high cut 12
  "M2 3H12C14 3 15 11 18 11", // high cut 48
];

export function EqShapeIcon({ type, width = 20, height = 12 }) {
  return (
    <svg
      width={width}
      height={height}
      viewBox="0 0 20 14"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.5"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <path d={SHAPES[type] ?? SHAPES[3]} />
    </svg>
  );
}
