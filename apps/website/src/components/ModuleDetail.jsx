import { useState } from 'react';
import { MODULE_PRICE } from '../data.js';

/**
 * One Lifeline-style module section: screenshot on one side, copy plus a
 * clickable engine list on the other, and that module's own price and CTA.
 * Picking an engine swaps the screenshot.
 */
export default function ModuleDetail({ module: mod, flip = false }) {
  const [engineIdx, setEngineIdx] = useState(0);
  const engine = mod.engines[engineIdx];

  return (
    <section id={mod.key} className={`bb-detail${flip ? ' bb-detail--flip' : ''}`}>
      <div className="bb-detail__media">
        <img
          src={engine.img}
          alt={`${mod.name} — ${engine.name}`}
          className="bb-shot"
          style={{ border: `1px solid ${mod.color}33` }}
        />
        <div className="bb-chips" style={{ marginTop: 20 }}>
          {engine.pills.map((pill) => (
            <span key={pill} className="bb-chip">
              {pill}
            </span>
          ))}
        </div>
      </div>

      <div className="bb-detail__copy">
        <div
          className="bb-modcard__label"
          style={{ color: mod.color, marginBottom: 12 }}
        >
          {mod.label} · {mod.count}
        </div>
        <h2 className="bb-h2" style={{ marginBottom: 8 }}>
          {mod.name}
        </h2>
        <p style={{ fontSize: 18, color: 'var(--bb-ink)', margin: '0 0 18px' }}>
          {mod.headline}
        </p>
        <p className="bb-body">{mod.long}</p>

        <div className="bb-detail__engines">
          {mod.engines.map((eng, i) => (
            <button
              key={eng.name}
              type="button"
              className={`bb-engrow${i === engineIdx ? ' is-active' : ''}`}
              style={i === engineIdx ? { borderColor: `${mod.color}88` } : undefined}
              onClick={() => setEngineIdx(i)}
              aria-pressed={i === engineIdx}
            >
              <span
                className="bb-chain__dot"
                style={{ background: mod.color, width: 8, height: 8, flexShrink: 0 }}
              />
              <span>
                <strong>{eng.name}</strong>
                <span style={{ color: 'var(--bb-ink-muted)' }}> — {eng.desc}</span>
              </span>
            </button>
          ))}
        </div>

        <div className="bb-detail__buy">
          <span className="bb-price">${MODULE_PRICE}</span>
          <a href="#" className="bb-btn bb-btn--filled bb-btn--sm">
            Buy {mod.name}
          </a>
          <span className="bb-detail__buy-note">
            Sold on its own · the exact DSP that ships inside Alpine
          </span>
        </div>
      </div>
    </section>
  );
}
