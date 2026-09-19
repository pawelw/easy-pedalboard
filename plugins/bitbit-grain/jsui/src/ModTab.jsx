import LfoEditor from "./LfoEditor.jsx";

/** The Mod tab's body, replacing Delay/Reverb's own row - it sits on the
    plate's own background (no chrome of its own), the same ground Delay and
    Reverb's sections already sit on. */
export default function ModTab() {
  return (
    <div className="pg-mod">
      <LfoEditor />
    </div>
  );
}
