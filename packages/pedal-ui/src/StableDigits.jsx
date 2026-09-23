import "./StableDigits.css";

/**
 * A value readout whose digits each sit in a fixed-width cell.
 *
 * Urbanist has no tabular figures (its only GSUB features are ccmp and frac),
 * and its "1" is under half the width of its "0" - so a centred readout
 * re-centred itself on every value, and the whole label shuffled left and
 * right as a knob turned. Boxing the digits makes the string's width depend
 * only on how many digits it has, not which ones.
 *
 * Anything that is not a string (a node a caller built itself) passes through.
 */
export default function StableDigits({ children }) {
  if (typeof children !== "string") return children;

  return children.split(/(\d)/).map((part, i) =>
    /^\d$/.test(part) ? (
      <span key={i} className="pui-digit">
        {part}
      </span>
    ) : (
      part
    ),
  );
}
