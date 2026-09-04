import "./Card.css";

function Screw({ corner }) {
  return (
    <svg className={`pui-screw pui-screw--${corner}`} viewBox="0 0 20 20" width="14" height="14" aria-hidden="true">
      <circle cx="10" cy="10" r="8" fill="var(--pui-screw)" stroke="var(--pui-screw-shadow)" strokeWidth="1" />
      <path d="M5 10 H15 M10 5 V15" stroke="var(--pui-screw-shadow)" strokeWidth="1.6" strokeLinecap="round" />
    </svg>
  );
}

/**
 * The rack-module panel every pedal face is built from: a bordered card with
 * four corner screws and a title/subtitle header. Children lay out the
 * controls however the pedal needs.
 */
export default function Card({ title, subtitle, width, children, className = "" }) {
  return (
    <div className={`pui-reset pui-card ${className}`} style={width ? { width } : undefined}>
      <Screw corner="tl" />
      <Screw corner="tr" />
      <Screw corner="bl" />
      <Screw corner="br" />

      {(title || subtitle) && (
        <header className="pui-card__header">
          {title && <h1 className="pui-card__title">{title}</h1>}
          {subtitle && <span className="pui-card__subtitle">{subtitle}</span>}
        </header>
      )}

      <div className="pui-card__body">{children}</div>
    </div>
  );
}
