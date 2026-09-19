import { MODULES } from '../data.js';

function Squiggle({ color }) {
  return (
    <svg width="16" height="16" viewBox="0 0 16 16" style={{ flexShrink: 0 }} aria-hidden="true">
      <path
        d="M1,11 Q5,3 8,8 T15,5"
        stroke={color}
        strokeWidth="1.6"
        fill="none"
        strokeLinecap="round"
      />
    </svg>
  );
}

export default function ModuleExplorer({
  id = 'modules-explorer',
  title = 'Eleven engines in BitBit Alpine. One window.',
}) {
  return (
    <section id={id} className="bb-wrap" style={{ paddingTop: 120, paddingBottom: 120 }}>
      <h2 className="bb-h2" style={{ marginBottom: 44 }}>
        {title}
      </h2>
      <div className="bb-modgrid">
        {MODULES.map((mod) => (
          <div key={mod.key} className="bb-modcard">
            <div className="bb-modcard__bar" style={{ background: mod.color }} />
            <div className="bb-modcard__body">
              <div className="bb-modcard__head">
                <span className="bb-modcard__label" style={{ color: mod.color }}>
                  {mod.label}
                </span>
                <span className="bb-modcard__count">{mod.count}</span>
              </div>
              <h3 className="bb-modcard__name">{mod.name}</h3>
              <p className="bb-body" style={{ fontSize: 14, lineHeight: 1.6 }}>
                {mod.desc}
              </p>
              <div
                style={{ display: 'flex', flexDirection: 'column', gap: 8, marginTop: 4 }}
              >
                {mod.engines.map((eng) => (
                  <div key={eng.name} className="bb-engrow">
                    <Squiggle color={mod.color} />
                    <span>
                      <strong>{eng.name}</strong> · {eng.tag}
                    </span>
                  </div>
                ))}
              </div>
            </div>
          </div>
        ))}
      </div>
    </section>
  );
}
