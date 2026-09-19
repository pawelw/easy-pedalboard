import SideModule from "./SideModule.jsx";
import { MOD_ENGINES } from "./engines.jsx";

/**
 * BitBit Alpine's Modulation module - Tape / Trem / Chorus / Phaser / Filter - as
 * a face of its own. One component, two hosts, the way `ArtifactFace` is: BitBit
 * Modulation wraps it in its own Card and binds its plain ids (`mix`,
 * `trem.rate`), BitBit Alpine drops it into its module row bound through `mod.`.
 * Every prop is SideModule's - see there.
 */
export default function ModulationFace(props) {
  return <SideModule name="Mod" accent="var(--pui-accent-mod)" engines={MOD_ENGINES} {...props} />;
}
