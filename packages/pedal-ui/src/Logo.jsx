import peakLogo from "./peak-logo.png";
import "./Logo.css";

/**
 * The Peak brand mark - one asset, recoloured to pure black via a CSS
 * filter (the source art isn't pure black itself) rather than shipping a
 * second tinted copy. Used by Card's header by default, and directly by any
 * pedal that wants the mark placed somewhere Card doesn't offer a slot for.
 */
export default function Logo({ size = 26, className = "" }) {
  return <img className={`pui-logo ${className}`} src={peakLogo} alt="" style={{ height: size }} />;
}
