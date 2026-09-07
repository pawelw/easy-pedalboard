import Logo from "./Logo.jsx";
import "./Card.css";

/**
 * The rack-module panel every pedal face is built from: a bordered card with
 * four corner screws and a title/subtitle header. Children lay out the
 * controls however the pedal needs. `headerCenter` and `headerRight` are
 * slots for whatever a pedal wants beside the title (a preset bar, a pair of
 * level faders) - Card itself stays generic and doesn't know what either of
 * those is. The centre slot is centred on the *card*, not on what is left
 * between its neighbours, so a preset bar in it stays put as the title or the
 * right-hand slot changes width. `showLogo` defaults on; turn it off for a
 * pedal that places the brand mark somewhere else on its own face.
 * Colour theme is picked once, above Card, with <PedalUIProvider theme="...">
 * - Card itself only ever reads tokens, never chooses them.
 */
export default function Card({
  title,
  subtitle,
  width,
  headerCenter,
  headerRight,
  showLogo = true,
  children,
  className = "",
}) {
  return (
    <div className={`pui-reset pui-card ${className}`} style={width ? { width } : undefined}>
      {(title || subtitle || headerCenter || headerRight) && (
        <header className="pui-card__header">
          <div className="pui-card__header-left">
            {showLogo && <Logo className="pui-card__logo" />}
            {title && <h1 className="pui-card__title">{title}</h1>}
            {subtitle && <span className="pui-card__subtitle">{subtitle}</span>}
          </div>
          {headerCenter && <div className="pui-card__header-center">{headerCenter}</div>}
          {headerRight && <div className="pui-card__header-right">{headerRight}</div>}
        </header>
      )}

      <div className="pui-card__body">{children}</div>
    </div>
  );
}
