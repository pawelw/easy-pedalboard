import "./Card.css";

/**
 * The rack-module panel every pedal face is built from: a bordered card with
 * four corner screws and a title/subtitle header. Children lay out the
 * controls however the pedal needs.
 */
export default function Card({ title, subtitle, width, children, className = "" }) {
  return (
    <div className={`pui-reset pui-card ${className}`} style={width ? { width } : undefined}>

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
