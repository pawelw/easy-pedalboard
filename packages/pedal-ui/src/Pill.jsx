import Button from "./Button.jsx";
import "./Pill.css";

/**
 * The icon+label toggle pill (LINKED, MS on the onyx Delay layout). Built
 * on the shared Button rather than a fork of it - `pressed` already gives a
 * button the lit/unlit distinction a pill needs, so this only adds Pill's
 * own shape/typography (see Pill.css) and an icon slot.
 */
export default function Pill({ icon, label, pressed, onClick }) {
  return (
    <Button className="pui-pill" pressed={pressed} onClick={onClick}>
      {icon}
      {label && <span>{label}</span>}
    </Button>
  );
}
