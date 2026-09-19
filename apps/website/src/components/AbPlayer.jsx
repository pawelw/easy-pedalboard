import { useState } from 'react';
import { SOURCES, PRESETS } from '../data.js';

const BAR_COUNT = 28;

/**
 * "Hear it" A/B demo player. UI only — no audio files exist yet, so play/pause
 * drives the waveform animation and nothing else.
 */
export default function AbPlayer({
  id = 'ab-player',
  eyebrow = 'Hear it',
  title = 'The same take, dry and processed.',
  presets = PRESETS,
  sources = SOURCES,
}) {
  const [isPlaying, setPlaying] = useState(false);
  const [wet, setWet] = useState(true);
  const [scrub, setScrub] = useState(20);
  const [sourceIdx, setSourceIdx] = useState(0);
  const [presetIdx, setPresetIdx] = useState(0);

  const preset = presets[presetIdx];

  return (
    <section id={id} className="bb-wrap" style={{ paddingBottom: 140 }}>
      <div className="bb-card" style={{ padding: 40 }}>
        <div className="bb-eyebrow" style={{ marginBottom: 8 }}>
          {eyebrow}
        </div>
        <h2 style={{ fontSize: 28, margin: '0 0 28px' }}>{title}</h2>

        <div className="bb-chips" style={{ marginBottom: 28 }}>
          {sources.map((label, i) => (
            <button
              key={label}
              type="button"
              className={`bb-pill${i === sourceIdx ? ' is-active' : ''}`}
              onClick={() => setSourceIdx(i)}
            >
              {label}
            </button>
          ))}
        </div>

        <div className="bb-well bb-player">
          <button
            type="button"
            className="bb-player__play"
            style={{ background: preset.color }}
            aria-label={isPlaying ? 'Pause' : 'Play'}
            onClick={() => setPlaying((p) => !p)}
          >
            {isPlaying ? (
              <svg width="18" height="18" viewBox="0 0 18 18" aria-hidden="true">
                <rect x="3" y="2" width="4" height="14" fill="#0f1315" />
                <rect x="11" y="2" width="4" height="14" fill="#0f1315" />
              </svg>
            ) : (
              <svg width="18" height="18" viewBox="0 0 18 18" aria-hidden="true">
                <polygon points="4,2 16,9 4,16" fill="#0f1315" />
              </svg>
            )}
          </button>

          <div style={{ flex: 1, minWidth: 200 }}>
            <div
              style={{
                display: 'flex',
                alignItems: 'baseline',
                gap: 10,
                marginBottom: 10,
                flexWrap: 'wrap',
              }}
            >
              <span style={{ fontSize: 15 }}>{preset.name}</span>
              <span
                style={{
                  fontSize: 11,
                  letterSpacing: '0.1em',
                  textTransform: 'uppercase',
                  color: preset.color,
                }}
              >
                {preset.engine}
              </span>
            </div>
            <div className="bb-player__bars">
              {Array.from({ length: BAR_COUNT }, (_, i) => (
                <div
                  key={i}
                  style={{
                    width: 4,
                    height: 6 + ((i * 37) % 24),
                    borderRadius: 2,
                    background: preset.color,
                    opacity: isPlaying ? 0.9 : 0.35,
                    animation: isPlaying
                      ? `bbPulse ${0.6 + (i % 5) * 0.08}s ease-in-out infinite`
                      : 'none',
                  }}
                />
              ))}
            </div>
            <input
              type="range"
              min="0"
              max="100"
              value={scrub}
              onChange={(e) => setScrub(Number(e.target.value))}
              aria-label="Scrub"
              style={{ width: '100%', marginTop: 12 }}
            />
          </div>

          <div
            style={{
              display: 'flex',
              flexDirection: 'column',
              alignItems: 'center',
              gap: 12,
              minWidth: 200,
            }}
          >
            <div className="bb-ab" role="group" aria-label="Dry or processed">
              <button
                type="button"
                className={`bb-ab__opt${wet ? '' : ' is-active'}`}
                style={wet ? undefined : { background: preset.color }}
                aria-pressed={!wet}
                onClick={() => setWet(false)}
              >
                Dry
              </button>
              <button
                type="button"
                className={`bb-ab__opt${wet ? ' is-active' : ''}`}
                style={wet ? { background: preset.color } : undefined}
                aria-pressed={wet}
                onClick={() => setWet(true)}
              >
                Processed
              </button>
            </div>
          </div>
        </div>

        <div className="bb-chips" style={{ marginTop: 24 }}>
          {presets.map((p, i) => (
            <button
              key={p.name}
              type="button"
              onClick={() => setPresetIdx(i)}
              style={{
                fontSize: 12,
                padding: '9px 16px',
                borderRadius: 999,
                cursor: 'pointer',
                border: `1px solid ${p.color}${i === presetIdx ? '' : '55'}`,
                background: i === presetIdx ? `${p.color}22` : 'none',
                color: i === presetIdx ? '#b9d3d9' : '#8ba3a9',
              }}
            >
              {p.name}
            </button>
          ))}
        </div>
      </div>
    </section>
  );
}
