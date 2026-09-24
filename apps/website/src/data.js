export const ACCENT = {
  artifact: '#c00001',
  modulation: '#e0b23c',
  delay: '#a3ce7a',
  reverb: '#7fd2d8',
  grains: '#b39bd8',
};

export const MODULE_PRICE = 19;
export const ALPINE_PRICE = 99;
export const GRAINS_PRICE = 49;

/** The four BitBit Alpine modules, their engines and the copy for each. */
export const MODULES = [
  {
    key: 'artifact',
    label: 'Module 01',
    name: 'Artifact',
    color: ACCENT.artifact,
    tag: 'destruction & character',
    count: '4 ENGINES',
    headline: 'Break it on purpose.',
    desc: 'Digital bit decimation, harsh magnetic rust grit, and vintage transformer push.',
    long: 'Artifact is the damage module. Four engines take the same signal apart four different ways — a ring modulator with a rectifier behind it, a crusher that quantises and jitters the clock, a rust stage that wears the note down as it rings, and a tube amp with a transfer-curve bite. Every one of them keeps its own Mix, so the destruction can sit under the source rather than replace it.',
    engines: [
      {
        name: 'Ring',
        tag: 'Twisted carrier ribbon',
        desc: 'A sine carrier with a post low-pass and a bipolar rectifier. Two voicings, switched by a Wobble/Octave toggle.',
        pills: ['Freq', 'Tweak', 'Filter', 'Rectify', 'Wobble/Octave', 'Mix'],
        img: '/assets/artifact-ring.png',
      },
      {
        name: 'Crasher',
        tag: 'Quantized staircase',
        desc: 'Sample-and-hold downsampling plus bit-depth quantisation, with jitter on the sample clock.',
        pills: ['Bits', 'Rate', 'Filter', 'Jitter', 'Mix'],
        img: '/assets/artifact-crasher.png',
      },
      {
        name: 'Rust',
        tag: 'Jagged tape grit',
        desc: 'Degradation with a memory — a wear state that corrodes a note as it rings and heals when you stop.',
        pills: ['Grind', 'Tone', 'Oxide/Contact', 'Mix'],
        img: '/assets/artifact-rust.png',
      },
      {
        name: 'Amp',
        tag: 'Transfer saturation',
        desc: 'Tube drive with a peaking mid, a bipolar tone tilt, sample-rate reduction.',
        pills: ['Drive', 'Mids', 'Bit', 'Tone', 'Mix'],
        img: '/assets/artifact-amp.png',
      },
    ],
  },
  {
    key: 'modulation',
    label: 'Module 02',
    name: 'Modulation',
    color: ACCENT.modulation,
    tag: 'movement & warmth',
    count: '5 ENGINES',
    headline: 'Nothing should sit perfectly still.',
    desc: 'Analog mechanical movement from rotary flutter to dimensional bucket-brigade detuning.',
    long: 'Modulation is where a static take starts breathing. A whole tape machine with capstan flutter and wear, an optical tremolo with a tube bias stage, a wide stereo chorus, a swept phaser, and a resonant filter that an LFO drags across the spectrum. Trem and Filter lock their rate to the host tempo from a Sync pill; Tape runs fully wet and uses its power toggle as its own dry/wet.',
    engines: [
      {
        name: 'Tape',
        tag: 'Capstan wow/flutter',
        desc: 'A whole tape machine — saturation, flutter, wear, noise, tone tilt. Runs fully wet; the power toggle is its dry/wet.',
        pills: ['Saturation', 'Flutter', 'Wear', 'Noise', 'Tone', 'Mono/Stereo'],
        img: '/assets/mod-tape.png',
      },
      {
        name: 'Trem',
        tag: 'Optical opto squiggles',
        desc: 'Amplitude modulation with a shape control and a tube bias stage. A Sync pill locks the rate to host tempo.',
        pills: ['Amount', 'Rate', 'Shape', 'Tube', 'Sync', 'Mix'],
        img: '/assets/mod-trem.png',
      },
      {
        name: 'Chorus',
        tag: 'Thick ensemble pitch',
        desc: 'A wide stereo chorus — bucket-brigade detuning across the image rather than a single voice doubled.',
        pills: ['Rate', 'Depth', 'Phase', 'Mix'],
        img: '/assets/mod-chorus.png',
      },
      {
        name: 'Phaser',
        tag: 'Swept notch filters',
        desc: 'A sweeping phaser, notches walking up and down the spectrum under one Rate and Depth pair.',
        pills: ['Rate', 'Depth', 'Mix'],
        img: '/assets/mod-phaser.png',
      },
      {
        name: 'Filter',
        tag: 'Resonant 24dB ladder',
        desc: 'A resonant low-pass swept by an LFO, with a Triangle/Ramp/Square picker and a stereo phase mode.',
        pills: ['Freq', 'Q', 'Range', 'Time', 'Sync', 'Wave', 'Mono/Stereo', 'Mix'],
        img: '/assets/mod-filter.png',
      },
    ],
  },
  {
    key: 'delay',
    label: 'Module 03',
    name: 'Delay',
    color: ACCENT.delay,
    tag: 'repeats, space in time',
    count: '3 ROUTINGS',
    headline: 'Echoes with a tape machine in the loop.',
    desc: 'A flexible delay and repeat machine — clean digital echoes or tape-loop character, with tape saturation and wobble on the repeats.',
    long: 'One delay engine in three routings. Independent left and right times, tempo-synced or free, linked or deliberately apart, with a tape stage on the repeats themselves - the note you play stays clean, and the pedal adds no latency. Left clean it is a bit-exact digital delay; wound up it is a loop that drifts a little further from the source every time round.',
    engines: [
      {
        name: 'Normal',
        tag: 'Centered mono loop',
        desc: 'One centred loop. Independent L/R tempo-synced times with tape on the repeats.',
        pills: ['Mix', 'Feedback', 'L Time', 'R Time', 'Link', 'Sync'],
        img: '/assets/delay-standalone.png',
      },
      {
        name: 'Wide',
        tag: 'Haas offset repeats',
        desc: 'The two sides offset against each other so the repeats open the image out.',
        pills: ['Mix', 'Feedback', 'L Time', 'R Time', 'Link', 'Sync'],
        img: '/assets/delay-standalone.png',
      },
      {
        name: 'Ping Pong',
        tag: 'Bouncing L/R spatial',
        desc: 'Repeats alternate across the stereo field, each side feeding the other.',
        pills: ['Mix', 'Feedback', 'L Time', 'R Time', 'Link', 'Sync'],
        img: '/assets/delay-standalone.png',
      },
    ],
  },
  {
    key: 'reverb',
    label: 'Module 04',
    name: 'Reverb',
    color: ACCENT.reverb,
    tag: 'room, tail, air',
    count: '2 ENGINES',
    headline: 'A room, or a box of springs.',
    desc: 'A flexible reverb machine — shimmer and beautiful hall reverbs, or dual electromechanical spring tanks.',
    long: 'Space is a feedback-delay-network reverb with decays from a third of a second out to forty, a shimmer octave folded back into the tail, and a resonance control over the tank. Spring is two detuned mechanical tanks running in stereo, with the drip and the tension of a guitar amp. Both sit behind a Low Cut that keeps a long tail from turning to mud.',
    engines: [
      {
        name: 'Space',
        tag: 'Concentric feedback',
        desc: 'A feedback-delay-network reverb, 0.3–40 second decays and a shimmer octave.',
        pills: ['Decay', 'Shimmer', 'Low Cut', 'Reso', 'Mix'],
        img: '/assets/reverb-space.png',
      },
      {
        name: 'Spring',
        tag: 'Dual mechanical coils',
        desc: 'Two detuned spring tanks in stereo, with a Tension control over the drip.',
        pills: ['Decay', 'Tension', 'Low Cut', 'Mix'],
        img: '/assets/reverb-spring.png',
      },
    ],
  },
];

export const MODULE_BY_KEY = Object.fromEntries(MODULES.map((m) => [m.key, m]));

export const ORDER_NOTES = {
  'reverb-artifact': 'Reverb before Artifact: crush the tail, not the note.',
  'artifact-reverb': 'Artifact before Reverb: the room hears the damage already done.',
  'modulation-delay': 'Modulation before Delay: every repeat wobbles a little further from the last.',
  'delay-modulation': 'Delay before Modulation: the tape colours the repeats, not the source.',
  'artifact-modulation': 'Artifact before Modulation: the crush gets warmed and softened afterward.',
  'modulation-artifact': 'Modulation before Artifact: warmth goes in, comes out crushed.',
  'delay-reverb': 'Delay before Reverb: repeats dissolve into the tail.',
  'reverb-delay': 'Reverb before Delay: the tail itself starts repeating.',
  'artifact-delay': "Artifact before Delay: it's the crushed signal that echoes.",
  'delay-artifact': 'Delay before Artifact: the crush lands on the source, not the repeats.',
  'modulation-reverb': 'Modulation before Reverb: the wobble blooms in the room.',
  'reverb-modulation': 'Reverb before Modulation: the tail itself starts to wobble.',
};

export const SOURCES = ['Dry Guitar', 'Clean Rhythm', 'Synth Pad', 'Drum Loop'];

export const PRESETS = [
  { name: 'Clean Quarters', engine: 'Delay', color: ACCENT.delay },
  { name: 'Dub Chamber', engine: 'Reverb · Space', color: ACCENT.reverb },
  { name: 'Warble Slap', engine: 'Delay + Mod', color: ACCENT.modulation },
  { name: 'Deep Wow', engine: 'Modulation · Phaser', color: ACCENT.modulation },
  { name: 'Crystal Triplets', engine: 'Delay', color: ACCENT.delay },
  { name: 'Tape Wash', engine: 'Modulation · Tape', color: ACCENT.modulation },
];

export const GRAINS_PRESETS = [
  { name: 'Driving Around', engine: 'Grain · Live', color: ACCENT.grains },
  { name: 'Frozen Choir', engine: 'Freeze · Stretch', color: ACCENT.grains },
  { name: 'Sixteenth Cloud', engine: 'Grid · Density', color: ACCENT.delay },
  { name: 'Octaves Down', engine: 'Pitch · Low', color: ACCENT.reverb },
  { name: 'Backwards Plate', engine: 'Random · Reverse', color: ACCENT.modulation },
  { name: 'Gentle and Polite', engine: 'Reverb · Whole', color: ACCENT.grains },
];

export const SPECS = [
  ['Formats', 'VST3 · AU (macOS) · VST3 (Windows)'],
  ['macOS', '11+, Universal, Apple Silicon native'],
  ['Windows', '10+, 64-bit'],
  ['Latency', 'Reported and compensated — 0–6 ms by product'],
  ['Refund window', '14 days'],
  ['Updates', 'Free'],
];

export const SPEC_CHIPS = [
  'VST3',
  'AU',
  'macOS 11+ Universal',
  'Windows 10+ 64-bit',
  '14-day refund',
  'Free updates',
];

/** BitBit Grains — the three headline sections, then the effects and the LFO. */
export const GRAINS_SECTIONS = [
  {
    key: 'grain',
    label: 'Section 01',
    name: 'Grain',
    color: ACCENT.grains,
    headline: 'One grain at a time, up to thirty-two at once.',
    desc: 'The input runs into a ten-and-a-half second circular buffer, and on a jittered timer the engine spawns a grain — a windowed voice reading that buffer from a point behind the write head, at its own pitch, direction and pan. Grain decides what one of those voices is.',
    img: '/assets/grain-mod-tab.png',
    focus: '0% 31%',
    controls: [
      ['Size', '20 ms – 1.00 s', 'Grain length. Under ~40 ms fragments turn into a metallic buzz at the spawn rate; over ~300 ms whole notes come back.'],
      ['Density', '1 – 40 /s', 'Grains spawned per second — sparse and countable at the bottom, a continuous cloud at the top. The engine divides out the overlap, so it is not a volume knob.'],
      ['Shape', '0 – 100 %', 'Envelope lean: soft and spread at 0, a plucky click of an attack at 100. Envelope energy is divided back out.'],
      ['Window', 'Narrow / Wide', 'How far either side of the tap point grains are allowed to land.'],
    ],
  },
  {
    key: 'pitch',
    label: 'Section 02',
    name: 'Pitch',
    color: ACCENT.reverb,
    headline: 'A cloud that stays in key.',
    desc: 'Low, Unison and High are weights against each other rather than positions on a scale. Any two at once is a chord, not a transposition — which is the whole reason they are three knobs. The Scale block underneath colours which notes High lands on, and nothing else.',
    img: '/assets/grain-mod-tab.png',
    focus: '34% 31%',
    controls: [
      ['Low', 'weight', 'How often a grain drops a whole octave. Octaves only, whatever the scale says — weight at the bottom that can never land on a wrong note.'],
      ['Unison', 'weight', 'How often it plays at pitch.'],
      ['High', 'weight', 'How often it jumps into the octave above — a plain octave up until the Scale block turns some of those grains into other notes.'],
      ['Scale', '5 scales', 'Major, Minor, Pentatonic Major, Pentatonic Minor or Chromatic, with its own on/off. Chromatic is the loosest; the rest are progressively more consonant.'],
      ['Mix', '0 – 100 %', "How much of that scale High takes. It colours High's interval and never its weight, so High stays exactly as loud either way."],
      ['Root', 'C – B', 'Which note to treat as the tonic. There is no pitch tracking — Root is a best-effort assumption, dialled in by ear.'],
    ],
  },
  {
    key: 'random',
    label: 'Section 03',
    name: 'Random',
    color: ACCENT.modulation,
    headline: 'The difference between a machine and a cloud.',
    desc: 'Three knobs over everything the engine is allowed to decide for itself: which grains play backwards, how far the timing wanders, and how wide they are thrown across the image. At zero it is a metronome spraying identical grains. Wound up, the cloud stops repeating.',
    img: '/assets/grain-mod-tab.png',
    focus: '76% 31%',
    controls: [
      ['Reverse', '0 – 100 %', 'Share of grains that play backwards. Past halfway the phrase stops being followable at all — which is sometimes the point.'],
      ['Scatter', '0 – 100 %', 'One knob over all the timing randomness: the gap between grains and how far each length strays from Size. Frozen on a grid, it also picks which sixteenth a grain replays.'],
      ['Stereo', '0 – 100 %', 'Width of the random pan placement, equal-power so the middle never dips.'],
      ['Mod', '0 – 100 %', 'How deeply the LFO underneath reaches into the section it is assigned to.'],
    ],
  },
];

export const GRAINS_EFFECTS = {
  key: 'effects',
  label: 'Effects',
  name: 'Delay, Reverb and the Mixer',
  color: ACCENT.delay,
  headline: 'A granular delay into a plate.',
  desc: 'The Effects tab holds everything behind the cloud. Delay is what the grains are tapped from — tempo-synced L and R times with feedback that writes the granulated output back in, so every repeat is granulated again on the way round. Reverb is a plate behind all of it, with a Source switch for what feeds it. Down the right-hand edge the Mixer keeps dry and grains as two independent levels rather than one crossfade.',
  img: '/assets/grain-effects-tab.png',
  controls: [
    ['Time', '20 ms – 2 s', 'How far behind the write head grains are tapped from. Scatter sprays around this point, so it is the centre of a window rather than one hard offset.'],
    ['Feedback', '0 – 92 %', 'Share of the granulated output written back into the buffer. Capped below unity — the path is no longer feed-forward.'],
    ['Stretch', '-100 – +100 %', 'Frozen only: the rate the read head scans the capture. +100 % holds the delay steady, 0 stutters on one moment, -100 % scrubs backwards — all without shifting pitch.'],
    ['Reverb', 'Mix · Decay · Low Cut', 'A plate with its own dry/wet. Source picks Whole (the blend plus the repeats) or Grains (the cloud straight off the engine, skipping dry and delay entirely).'],
    ['Dry / Grains', '-inf – +6 dB', 'Two independent faders, Link-able. "All of both at once" is a position you can reach, which a single Mix crossfade never could.'],
    ['Filter', '0 – 100 %', "One pole over the cloud's lowpass, 13 kHz down to 320 Hz. It filters the grains only — the dry path is never touched."],
  ],
};

export const GRAINS_LFO = {
  key: 'lfo',
  label: 'Mod',
  name: 'The LFO editor',
  color: ACCENT.delay,
  headline: 'Draw the shape, then point it at something.',
  desc: 'The Mod tab is a hand-drawn breakpoint editor rather than a shape picker. Drag points into the curve you want, or start from one of the named shapes and bend it from there. Rate runs free or locks to the host tempo from a Sync pill, and the whole thing can be switched out without losing the drawing.',
  img: '/assets/grain-mod-tab.png',
  focus: '34% 97%',
  zoom: '200% auto',
  aspect: '16 / 9',
  controls: [
    ['Breakpoints', 'drawn', 'Add, drag and remove points anywhere in the frame. The curve between them is what modulates — no fixed waveform list to pick from.'],
    ['Rate', 'free / synced', 'One cycle of the drawing. The Sync pill snaps it to note divisions of the host tempo.'],
    ['Shapes', 'named starting points', 'Pluck, ramp, stair and the rest — a starting drawing you are free to move every point of.'],
    ['Power', 'on / off', 'Switches the modulation out and leaves the drawing intact.'],
  ],
};

/** Grid rows for the Grains "at a glance" strip. */
export const GRAINS_HIGHLIGHTS = [
  ['Up to 32 grains', 'Overlapping, each with its own pitch, direction and pan.'],
  ['Live or Freeze', 'A granular delay, or a held buffer you scrub with Stretch.'],
  ['Always on the grid', 'With the transport rolling, grains read from whole sixteenth notes.'],
  ['Attack octaves', 'The first grain after a struck note is placed, not left to chance.'],
];
