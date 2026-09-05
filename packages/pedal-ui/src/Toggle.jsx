import { Switch } from "@mantine/core";
import "./Toggle.css";

/**
 * The pill switch (POLY, Sync, Mono/Stereo...), built on Mantine's Switch
 * with its styling stripped so ours applies instead - keyboard toggling and
 * ARIA state come from Mantine for free.
 */
export default function Toggle({ checked, onChange, caption }) {
  return (
    <div className="pui-reset pui-toggle">
      <Switch
        unstyled
        checked={checked}
        onChange={(event) => onChange(event.currentTarget.checked)}
        classNames={{
          root: "pui-toggle__root",
          track: "pui-toggle__track",
          thumb: "pui-toggle__thumb",
          input: "pui-toggle__input",
        }}
      />
      {caption && <div className="pui-caption pui-toggle__caption">{caption}</div>}
    </div>
  );
}
