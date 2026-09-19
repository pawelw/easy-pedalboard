import SideModule from "./SideModule.jsx";
import { REVERB_ENGINES } from "./engines.jsx";

/**
 * BitBit Alpine's Reverb module - Space / Spring - as a face of its own. BitBit
 * Reverb wraps it in its own Card and binds its plain ids (`mix`,
 * `space.decay`), BitBit Alpine drops it into its module row bound through
 * `rev.`. Every prop is SideModule's - see there.
 */
export default function ReverbFace(props) {
  return <SideModule name="Reverb" accent="var(--pui-accent-reverb)" engines={REVERB_ENGINES} {...props} />;
}
