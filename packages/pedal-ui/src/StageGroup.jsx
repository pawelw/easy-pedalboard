import "./StageGroup.css";

/**
 * One half of Peak Delay's footer: a `StageRouter` header over a row of
 * `StageControl` knobs (COMPONENTS.md #5). Both halves hold two knobs - WEAR +
 * FLUTTER on the tape side, CHORUS + PHASER on the mod side - and the 50/50
 * balance of the footer depends on keeping them paired.
 *
 * `tone="tape"` re-points the plain `--pui-*` knob tokens to the `--pui-tape-*`
 * group on this wrapper (see StageGroup.css) and fills in the `--pui-stage-*`
 * ink the header and the knob captions read. Everything inside inherits them,
 * so the very same Knob renders green here and unchanged everywhere else -
 * Knob and StageRouter never learn what a tone is.
 *
 * `header` is a node rather than the router's own props: the router is bound to
 * a parameter, and no JUCE reaches inside `pedal-ui`.
 *
 * The band background, and the negative margins that bleed it to the card's
 * edge, are deliberately NOT here: how far a footer half reaches is a property
 * of the face it sits in (its card padding and corner radius), not of the
 * control. The face styles `.pd-footer__half--tape` for that.
 */
export default function StageGroup({ header, tone = "default", children }) {
  return (
    <div className={`pui-reset pui-stage-group pui-stage-group--${tone}`}>
      {header}
      <div className="pui-stage-group__knobs">{children}</div>
    </div>
  );
}
