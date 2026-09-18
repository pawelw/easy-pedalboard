# Handoff: BitBit Audio Website (Home + Pricing)

## Overview
Marketing website for BitBit Audio, maker of two audio plugins: BitBit Alpine (a 4-module effects chain host) and BitBit Grains (a granular delay + reverb). Two pages: Home (hero, A/B demo player, product intros, drag-to-reorder chain explainer, module/engine explorer, pricing strip, footer) and Pricing (full pricing matrix + spec table).

## About the Design Files
The bundled files (`Home.dc.html`, `Pricing.dc.html`) are **design references built in HTML** — they show intended layout, copy, color, typography, and interaction, not production code to copy verbatim. Recreate these designs in the target codebase's existing framework (React, Vue, etc.) using its established component patterns, routing, and styling approach. If no frontend framework exists yet for this site, choose the most appropriate one and implement there.

## Fidelity
**High-fidelity.** Colors, typography, spacing, and copy are final as shown. Interactions (drag-to-reorder, A/B player controls, module card hover) should be recreated faithfully.

## Screens / Views

### 1. Home
- **Purpose**: Introduce both plugins, let visitors preview the sound, explain the modular chain concept, and funnel to Pricing.
- **Layout**: Single column, `max-width: 1280px` centered, `padding: 0 32px` per section. Sticky top nav.
- **Sections top to bottom**:
  1. **Nav** — logo (wordmark + small waveform icon, SVG polyline), 5 text links (Alpine, Grains, Modules, Demos, Support), pill-shaped "Buy" CTA button linking to Pricing.dc.html. Sticky, `rgba(15,19,21,0.82)` background with `backdrop-filter: blur(12px)`, bottom border `1px solid #222b2e`.
  2. **Hero** — two-column flex (text left, product image right), headline "Colour, not correction." at `clamp(40px,6.4vw,80px)`, subhead paragraph, two CTAs ("Hear it" filled pill, "Explore the plugins" outlined pill). Decorative radial gradient blob background (`#3ddc8422`).
  3. **A/B Player ("Hear it")** — dark card (`#171d20`, `border-radius:20px`). Source-selection pills (Dry Guitar / Clean Rhythm / Synth Pad / Drum Loop). Player row: circular play/pause button, animated waveform bar visualization (28 bars, color-coded to active preset, pulse animation when playing), scrub slider, Dry/Wet mix slider, Loop toggle pill. Preset chip row below (6 presets, each tinted to its module color). **Note: audio is not wired up yet — this is UI only, silent/placeholder.**
  4. **Product cards** — two side-by-side cards: BitBit Alpine ($99, full-width product screenshot, 3 bullet features) and BitBit Grains ($49, screenshot, 3 bullets). Each has price + "Learn more" link to Pricing.
  5. **Drag-to-reorder explainer** — full-width band (`#171d20` background, top/bottom border). Shows 4 draggable chain blocks (Artifact/Modulation/Delay/Reverb) with arrows between; dragging reorders them and updates a contextual one-line annotation below explaining the sonic effect of that ordering (12 pair-specific strings, see Design Tokens/Data). Two side-by-side screenshot examples underneath (default order vs. reordered).
  6. **Module/Engine explorer ("Eleven engines. One window.")** — 4-column grid (`repeat(auto-fit, minmax(240px,1fr))`) of module cards, one per module (Artifact, Modulation, Delay, Reverb). Each card: 4px top color bar in module accent, "MODULE 0X" label + engine/routing count, module name (24px), one-sentence description, list of engine rows (small squiggle icon + engine name + short tag) inside `#101416` pill rows.
  7. **Pricing strip** — 3 price tiles (Single module $19, Grains $49, Alpine $99) + bundle math sentence + link to full Pricing page + spec chips row (VST3, AU, AAX, OS support, refund, updates).
  8. **Footer** — 4-column link grid (Products, Modules, Resources, Company) + bottom bar with copyright and small logo repeat.

### 2. Pricing
- **Purpose**: Full pricing comparison and spec sheet.
- **Layout**: Same nav (logo links home). Hero blurb, then 3-column pricing card row (Single module $19 / BitBit Grains $49, purple-accented / BitBit Alpine $99, best-value, light-accented with gradient top bar), each with feature bullets and a CTA button. Below: a highlighted bundle-math callout card ("$19 × 4 = $76" vs Alpine's $99 value case). Below that: a full-width spec table (Formats, macOS, Windows, Latency, Refund window, Updates) as bordered rows. Footer: copyright + back-to-home link.

## Interactions & Behavior
- **Nav**: anchor links scroll to in-page sections (`#alpine`, `#grains`, `#modules-explorer`, `#ab-player`); "Buy" navigates to Pricing page.
- **A/B player**: clicking a source pill selects it (visual state only, no real audio yet); play/pause toggles an icon and starts/stops the waveform pulse animation; scrub and mix are native range inputs (no wiring); preset chips switch the active preset's name/color shown in the player; Loop is a toggle pill.
- **Drag-to-reorder**: HTML5 drag and drop (`draggable`, `onDragStart`, `onDragOver` with `preventDefault`, `onDrop`) reorders an array of 4 module keys; the annotation text below is looked up from a map keyed by the first two modules in the new order.
- **Module explorer**: static, no interaction (previously had expand/collapse per module — simplified to always-expanded compact cards per latest revision).
- Hover states: links dim/brighten (`#8ba3a9` → `#b9d3d9`), outlined buttons brighten border on hover, filled buttons lighten background on hover.

## State Management
Home page component state (React-equivalent):
- `chainOrder: string[]` — order of `['artifact','modulation','delay','reverb']`, reorderable by drag/drop.
- `dragFrom: number|null` — index currently being dragged.
- `isPlaying: boolean`, `loop: boolean`, `abMix: number (0–100)`, `scrub: number (0–100)`, `sourceIdx: number`, `presetIdx: number` — all A/B player UI state, no real audio backing yet.

No data fetching; all copy/data is static/hardcoded (see Design Tokens below for the full data set).

## Design Tokens

**Colors**
- Background (page): `#0f1315`
- Card background: `#171d20`
- Inset/well background: `#101416`
- Border: `#222b2e`
- Primary text: `#b9d3d9`
- Bright text/hover: `#e5f2f5`
- Muted text: `#8ba3a9`
- Faint label text: `#6c8288`
- Module accents: Artifact `#c00001` (red), Modulation `#e0b23c` (amber), Delay `#a3ce7a` (green), Reverb `#7fd2d8` (cyan)
- Grains accent: `#b39bd8` (purple)

**Typography**: Space Grotesk (Google Fonts), weights 300/400/500/700. Headlines use weight 400 (not bold) at large sizes with `letter-spacing:-0.01em` to `-0.02em`. Labels/eyebrows: 11–13px, `letter-spacing:0.06em–0.14em`, uppercase, `#6c8288`.

**Radii**: pills `999px`; cards `14–20px`; small chips `8–10px`.

**Shadows**: cards use `0 20px 44px rgba(0,0,0,0.4)`; inset wells use `inset 0 2px 6px rgba(0,0,0,0.6)`.

**Data — pricing**: Single module $19, BitBit Grains $49, BitBit Alpine $99 (bundle math: 4×$19=$76; Alpine adds host/reorder/trims/presets for +$23 over that).

**Data — modules/engines**:
- Artifact (red): Ring, Crasher, Rust, Amp
- Modulation (amber): Tape, Trem, Chorus, Phaser, Filter
- Delay (green): Normal, Wide, Ping Pong (3 routings of one engine)
- Reverb (cyan): Space, Spring

**Data — chain-order annotations**: 12 hardcoded sentences, one per ordered pair of adjacent modules (e.g. "Artifact before Reverb: the room hears the damage already done."). Full map is in the component source — copy verbatim.

**Data — A/B demo presets**: Clean Quarters (Delay), Dub Chamber (Reverb·Space), Warble Slap (Delay+Mod), Deep Wow (Modulation·Phaser), Crystal Triplets (Delay), Tape Wash (Modulation·Tape). Source clips: Dry Guitar, Clean Rhythm, Synth Pad, Drum Loop (labels only — no audio files exist yet).

## Assets
Product screenshots (in `assets/` alongside the HTML files) are real plugin UI captures from the BitBit Alpine/Grains repo, used as placeholders for hero, product cards, and chain-reorder examples:
- `alpine-full.png`, `alpine-reordered.png` — Alpine in two chain orders
- `grain-mod-tab.png` — Grains UI
- Various `mod-*.png`, `artifact-*.png`, `reverb-*.png`, `delay-standalone.png` — per-engine screenshots (currently unused in the simplified module explorer, kept for reference/future use)

No audio assets exist yet — the A/B player is UI-only and needs real dry/processed audio clips wired in before launch.

## Files
- `Home.dc.html` — Home page (full source, inline-styled, single-file component)
- `Pricing.dc.html` — Pricing page
- `assets/` — screenshot images referenced by both pages
