import { useEffect, useRef, useState } from "react";
import Button from "./Button.jsx";
import "./PresetSaveDialog.css";

/**
 * Name-and-save, over the face. Two buttons normally - Cancel and Save, which
 * writes to the user's own bank - and a third when the build allows it, which
 * writes into the pedal's `presets/` source folder so the preset can be
 * committed and shipped with the next release.
 *
 * That third button is `canAuthor`, and the build decides it, not this
 * component and not the page: a release plugin has no source tree to write to
 * and its bridge refuses the call outright (see ee/plugin/PresetBridge.h). All
 * this does is not draw a button nobody can use.
 *
 * Hand-rolled rather than a Mantine Modal. `unstyled` on a Mantine overlay
 * component strips its positioning along with its look - the same trap
 * Dropdown.css documents for the combobox panel - and a dialog that has lost
 * its `position: fixed` lands in the middle of the pedal's layout and pushes
 * the knobs down. There is nothing here worth that: an overlay, a box, a
 * focused input and two key handlers.
 */
export default function PresetSaveDialog({ open, initialName = "", canAuthor = false, error, onSave, onCancel }) {
  const [name, setName] = useState(initialName);
  const inputRef = useRef(null);

  // Reseeded on each open rather than held across them, so the box always
  // offers the preset that is loaded now - and so a name abandoned with
  // Cancel is not still sitting there next time.
  useEffect(() => {
    if (open) {
      setName(initialName);
      // The input mounts with the dialog, so focus has to wait for it.
      requestAnimationFrame(() => inputRef.current?.select());
    }
  }, [open, initialName]);

  if (!open) return null;

  const trimmed = name.trim();
  const save = (kind) => trimmed && onSave?.(kind, trimmed);

  return (
    <div
      className="pui-reset pui-presetsave"
      // Escape anywhere in the dialog cancels, Enter in the input saves to the
      // user bank - never to the factory one, which is a deliberate act and
      // should cost a deliberate click.
      onKeyDown={(event) => {
        if (event.key === "Escape") {
          event.stopPropagation();
          onCancel?.();
        }
      }}
    >
      {/* Catches the click that closes the dialog, and also every click that
          would otherwise land on a knob behind it. */}
      <div className="pui-presetsave__scrim" onMouseDown={onCancel} />

      <div className="pui-presetsave__box" role="dialog" aria-modal="true" aria-label="Save preset">
        <div className="pui-presetsave__title">Save Preset</div>

        <input
          ref={inputRef}
          className="pui-presetsave__input"
          value={name}
          placeholder="Preset name"
          spellCheck={false}
          onChange={(event) => setName(event.target.value)}
          onKeyDown={(event) => {
            if (event.key === "Enter") save("user");
          }}
        />

        {/* Whatever the native side said went wrong - a name that sanitised
            away to nothing, a folder it could not write. Held here rather
            than dismissing the dialog on failure, so the name you typed is
            still in the box to fix. */}
        {error && <div className="pui-presetsave__error">{error}</div>}

        <div className="pui-presetsave__actions">
          <Button className="pui-presetsave__button" onClick={onCancel}>
            Cancel
          </Button>

          {canAuthor && (
            <Button
              className="pui-presetsave__button pui-presetsave__button--author"
              onClick={() => save("factory")}
              disabled={!trimmed}
              title="Write into this pedal's presets/ folder, to be committed and shipped"
            >
              Save to Factory
            </Button>
          )}

          <Button
            className="pui-presetsave__button pui-presetsave__button--primary"
            onClick={() => save("user")}
            disabled={!trimmed}
          >
            Save
          </Button>
        </div>
      </div>
    </div>
  );
}
