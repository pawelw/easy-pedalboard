/**
 * A Lifeline-style feature section used by the Grains page: screenshot on one
 * side, headline plus a control table on the other.
 */
export default function FeatureDetail({ section, flip = false, id }) {
  return (
    <section id={id ?? section.key} className={`bb-detail${flip ? ' bb-detail--flip' : ''}`}>
      <div className="bb-detail__media">
        {section.focus ? (
          <div
            className="bb-detail__frame"
            role="img"
            aria-label={`BitBit Grains — ${section.name}`}
            style={{
              backgroundImage: `url(${section.img})`,
              backgroundSize: section.zoom ?? '260% auto',
              backgroundPosition: section.focus,
              aspectRatio: section.aspect,
              border: `1px solid ${section.color}33`,
            }}
          />
        ) : (
          <img
            src={section.img}
            alt={`BitBit Grains — ${section.name}`}
            className="bb-shot"
            style={{ border: `1px solid ${section.color}33` }}
          />
        )}
      </div>

      <div className="bb-detail__copy">
        <div className="bb-modcard__label" style={{ color: section.color, marginBottom: 12 }}>
          {section.label}
        </div>
        <h2 className="bb-h2" style={{ marginBottom: 8 }}>
          {section.name}
        </h2>
        <p style={{ fontSize: 18, color: 'var(--bb-ink)', margin: '0 0 18px' }}>
          {section.headline}
        </p>
        <p className="bb-body">{section.desc}</p>

        <div className="bb-detail__engines">
          {section.controls.map(([name, range, what]) => (
            <div key={name} className="bb-engrow" style={{ alignItems: 'flex-start' }}>
              <span
                className="bb-chain__dot"
                style={{ background: section.color, width: 8, height: 8, marginTop: 6, flexShrink: 0 }}
              />
              <span>
                <strong>{name}</strong>
                <span style={{ color: 'var(--bb-ink-faint)' }}> · {range}</span>
                <br />
                <span style={{ color: 'var(--bb-ink-muted)', lineHeight: 1.6 }}>{what}</span>
              </span>
            </div>
          ))}
        </div>
      </div>
    </section>
  );
}
