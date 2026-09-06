import "./SectionLabel.css";

/**
 * A small group heading for a strip of controls that need one, the way
 * "PRE-STAGE" / "POST-STAGE" label Tape/Mod on the onyx Delay layout's
 * stage strip. Plain text, no interaction - purely a caption.
 */
export default function SectionLabel({ children }) {
  return <span className="pui-reset pui-section-label">{children}</span>;
}
