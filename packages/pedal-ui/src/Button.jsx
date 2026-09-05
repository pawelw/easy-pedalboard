import { Button as MantineButton } from "@mantine/core";
import "./Button.css";

/**
 * A rack-panel pill button (SOLO / BYPASS), built on Mantine's Button with
 * Mantine's own styling stripped (`unstyled`) so ours applies instead - still
 * gets Mantine's focus/keyboard/ARIA handling for free.
 *
 * `led`: omit for a plain button, or pass a boolean (lit/unlit) for a status
 * dot before the label, as on a BYPASS switch.
 */
export default function Button({ children, led, onClick, pressed, ...rest }) {
  return (
    <MantineButton
      unstyled
      onClick={onClick}
      data-pressed={pressed || undefined}
      leftSection={led !== undefined ? <span className={"pui-button__led" + (led ? " pui-button__led--on" : "")} /> : undefined}
      classNames={{ root: "pui-button", label: "pui-button__label", inner: "pui-button__inner" }}
      {...rest}
    >
      {children}
    </MantineButton>
  );
}
