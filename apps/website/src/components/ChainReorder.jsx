import { useState } from 'react';
import { MODULE_BY_KEY, ORDER_NOTES } from '../data.js';

const INITIAL = ['artifact', 'modulation', 'delay', 'reverb'];

export default function ChainReorder() {
  const [order, setOrder] = useState(INITIAL);
  const [dragFrom, setDragFrom] = useState(null);

  const drop = (to) => {
    if (dragFrom == null || dragFrom === to) return;
    const next = [...order];
    const [moved] = next.splice(dragFrom, 1);
    next.splice(to, 0, moved);
    setOrder(next);
    setDragFrom(null);
  };

  const pairKey = `${order[0]}-${order[1]}`;
  const annotation =
    ORDER_NOTES[pairKey] ||
    `${MODULE_BY_KEY[order[0]].name} before ${MODULE_BY_KEY[order[1]].name}: order changes what ${MODULE_BY_KEY[order[1]].name} hears.`;

  return (
    <section id="chain" className="bb-band" style={{ padding: '100px 0' }}>
      <div className="bb-wrap">
        <div className="bb-eyebrow" style={{ marginBottom: 8 }}>
          Drag to reorder
        </div>
        <h2 className="bb-h2" style={{ marginBottom: 32 }}>
          The order is the instrument.
        </h2>

        <div className="bb-chain">
          {order.map((key, i) => {
            const mod = MODULE_BY_KEY[key];
            return (
              <div key={key} style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
                <div
                  className="bb-chain__block"
                  style={{ border: `1px solid ${mod.color}55` }}
                  draggable
                  onDragStart={() => setDragFrom(i)}
                  onDragOver={(e) => e.preventDefault()}
                  onDrop={() => drop(i)}
                >
                  <span className="bb-chain__dot" style={{ background: mod.color }} />
                  {mod.name}
                </div>
                {i < order.length - 1 && (
                  <span style={{ color: 'var(--bb-ink-faint)' }}>→</span>
                )}
              </div>
            );
          })}
        </div>

        <p className="bb-body" style={{ marginTop: 24 }}>
          {annotation}
        </p>

        <div style={{ display: 'flex', gap: 24, flexWrap: 'wrap', marginTop: 40 }}>
          <figure style={{ flex: 1, minWidth: 300, margin: 0 }}>
            <img src="/assets/alpine-full.png" alt="Alpine in its default chain order" className="bb-shot" />
            <figcaption style={{ fontSize: 12, color: 'var(--bb-ink-faint)', marginTop: 10 }}>
              Default: Artifact → Modulation → Delay → Reverb
            </figcaption>
          </figure>
          <figure style={{ flex: 1, minWidth: 300, margin: 0 }}>
            <img src="/assets/alpine-reordered.png" alt="Alpine with the chain reordered" className="bb-shot" />
            <figcaption style={{ fontSize: 12, color: 'var(--bb-ink-faint)', marginTop: 10 }}>
              Reordered: the room hears the source before it is crushed
            </figcaption>
          </figure>
        </div>
      </div>
    </section>
  );
}
