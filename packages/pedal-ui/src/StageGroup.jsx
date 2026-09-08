import "./StageGroup.css";

/**
 * One half of Peak Delay's footer: a `StageRouter` header over a row of
 * `StageControl` knobs (COMPONENTS.md #5). Both halves hold two knobs - WEAR +
 * FLUTTER on the tape side, CHORUS + PHASER on the mod side - and the 50/50
 * balance of the footer depends on keeping them paired.
 *
 * It fills in the `--pui-stage-*` ink its header and knob captions read, and
 * everything inside inherits them - so neither Knob nor StageRouter ever
 * learns it is in a stage.
 *
 * There is no longer a `tone`. The Tape section used to take one, which
 * re-pointed the knob tokens to a green group so the same Knob rendered green
 * inside its band and plain outside it; the band is gone and every footer knob
 * is now the same control (see tokens.css). A section says what it is with the
 * coloured glyph in its `StageHeader` instead.
 *
 * `header` is a node rather than the router's own props: the router is bound to
 * a parameter, and no JUCE reaches inside `pedal-ui`.
 */
export default function StageGroup({ header, children }) {
  return (
    <div className="pui-reset pui-stage-group">
      {header}
      <div className="pui-stage-group__knobs">{children}</div>
    </div>
  );
}
