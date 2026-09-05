import Logo from "./Logo.jsx";
import "./Card.css";

/**
 * The rack-module panel every pedal face is built from: a bordered card with
 * four corner screws and a title/subtitle header. Children lay out the
 * controls however the pedal needs. `headerRight` is a slot for whatever a
 * pedal wants opposite the title (a preset bar, say) - Card itself stays
 * generic and doesn't know what presets are. `showLogo` defaults on; turn it
 * off for a pedal that places the brand mark somewhere else on its own face.
 */
export default function Card({ title, subtitle, width, headerRight, showLogo = true, children, className = "" }) {
  return (
    <div className={`pui-reset pui-card ${className}`} style={width ? { width } : undefined}>
      {(title || subtitle || headerRight) && (
        <header className="pui-card__header">
          <div className="pui-card__header-left">
            {showLogo && <Logo className="pui-card__logo" />}
            {title && <h1 className="pui-card__title">{title}</h1>}
            {subtitle && <span className="pui-card__subtitle">{subtitle}</span>}
          </div>
          {headerRight && <div className="pui-card__header-right">{headerRight}</div>}
        </header>
      )}

      <div className="pui-card__body">{children}</div>
    </div>
  );
}
