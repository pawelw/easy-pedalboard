import { SPEC_CHIPS } from '../data.js';

/** Closing buy band used at the bottom of the Alpine and Grains pages. */
export default function BuyCta({
  id = 'buy',
  eyebrow = 'Get it',
  title,
  blurb,
  price,
  cta,
  accent = 'var(--bb-ink)',
  secondary,
}) {
  return (
    <section id={id} className="bb-band" style={{ padding: '100px 0' }}>
      <div className="bb-wrap">
        <div className="bb-eyebrow" style={{ marginBottom: 8 }}>
          {eyebrow}
        </div>
        <h2 className="bb-h2" style={{ marginBottom: 20 }}>
          {title}
        </h2>
        <p className="bb-lede" style={{ marginBottom: 36 }}>
          {blurb}
        </p>

        <div style={{ display: 'flex', alignItems: 'center', gap: 24, flexWrap: 'wrap' }}>
          <span className="bb-price" style={{ fontSize: 44 }}>
            ${price}
          </span>
          <a
            href="#"
            className="bb-btn"
            style={{ background: accent, color: '#0f1315', fontWeight: 600 }}
          >
            {cta}
          </a>
          {secondary}
          <span style={{ fontSize: 13, color: 'var(--bb-ink-faint)' }}>
            One-time · free updates · 14-day refund
          </span>
        </div>

        <div
          className="bb-chips"
          style={{
            gap: 24,
            marginTop: 36,
            paddingTop: 28,
            borderTop: '1px solid var(--bb-border)',
            fontSize: 12,
            letterSpacing: '0.06em',
            color: 'var(--bb-ink-faint)',
          }}
        >
          {SPEC_CHIPS.map((chip) => (
            <span key={chip}>{chip}</span>
          ))}
        </div>
      </div>
    </section>
  );
}
