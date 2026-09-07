/**
 * The band the Filter section leaves standing: one flat-topped curve rolling
 * off at both ends, which is what a low cut and a high cut together do to a
 * spectrum. Drawn as the shape of the *result* rather than as two separate
 * slopes - the section is one control over one band, and its icon should read
 * that way at 30px.
 *
 * Same construction as the other stage icons (StageHeader's slot): a stroked
 * path in `currentColor` on a viewBox the caller scales, so it inherits the
 * stage's ink and needs no colour of its own.
 */
export default function FilterIcon({ size = 38 }) {
  return (
    <svg
      className="pui-filter-icon"
      width={size}
      height={size * (31 / 38)}
      viewBox="0 0 44 36"
      fill="none"
      stroke="currentColor"
      strokeWidth="3.4"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <path d="M2 30 C9 30, 10 8, 17 8 L27 8 C34 8, 35 30, 42 30" />
    </svg>
  );
}
