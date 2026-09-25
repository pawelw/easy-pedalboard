import Button from "./Button.jsx";
import "./Pill.css";

/**
 * The icon+label toggle pill (LINKED, MS on the onyx Delay layout). Built
 * on the shared Button rather than a fork of it - `pressed` already gives a
 * button the lit/unlit distinction a pill needs, so this only adds Pill's
 * own shape/typography (see Pill.css) and an icon slot.
 *
 * `labelSet` is every label this pill can ever show. A pill that cycles
 * through values is otherwise as wide as whichever one it is on, so the row
 * it sits in re-flows on each press - "Normal" and "Ping Pong" differ by 12px.
 * Given the set, it holds the width of the widest and stops moving.
 */
export default function Pill({ icon, label, pressed, onClick, className, labelSet }) {
  return (
    <Button className={className ? "pui-pill " + className : "pui-pill"} pressed={pressed} onClick={onClick}>
      {icon}
      {label &&
        (labelSet ? (
          <span className="pui-pill__label">
            <span>{label}</span>
            {labelSet.map((entry) => (
              <span key={entry} className="pui-pill__label-ghost" aria-hidden="true">
                {entry}
              </span>
            ))}
          </span>
        ) : (
          <span>{label}</span>
        ))}
    </Button>
  );
}
