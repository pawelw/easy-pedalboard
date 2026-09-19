# Release plan

Everything between here and the first paid download. Written against the tree as
it stands, so the gaps below are real gaps, not hypotheticals.

The plan is ordered by **gates**, not dates. A gate is a state the project is in;
nothing downstream of a gate should be started while an upstream one is red,
because the rework is what kills a release schedule.

---

## Decisions

### D1. The name — **SETTLED: BitBit Audio.** ✅

Done and pushed (`84ea8a9`, `2d2f102`). Manufacturer code `BtBt`, plugin codes
`B???`, bundles `com.bitbitaudio.*`, products `BitBit X`, user presets under
`~/Library/BitBit/<Product>/Presets`, `peak-grain` ships as **BitBit Grains**.

What this now costs to change again: everything. The AU/VST3 ids are what a host
looks a plugin up by, so from the first sale onward the codes in
`plugins/*/CMakeLists.txt` and the manufacturer code in
`cmake/AddPeakPlugin.cmake` are frozen. Put them in the golden file in G1.1.

Two loose ends, neither blocking:

- The **in-plugin mark** is still `packages/pedal-ui/src/peak-logo.png`. The
  website has the pixel waveform; the faces do not. The new mark is ≈2.7:1
  against the old art's ≈1.3:1, so swapping it widens every Card header by
  ~35 px and needs a pass over all faces in the gallery.
- Internal names are deliberately still `peak-*` (folders, CMake targets, the
  `ee::` namespace, the `@synthpeak` npm scope). Nothing user-visible; leave them.

### D2. What is for sale — **SETTLED: six products.** ✅

| Product | Tree | Price |
| --- | --- | --- |
| BitBit Alpine | `peak-alpine` | `$99` |
| BitBit Grains | `peak-grain` | `$49` |
| BitBit Artifact | `peak-artifact` | `$19` |
| BitBit Modulation | `peak-modulation` | `$19` |
| BitBit Delay | `peak-delay` | `$19` |
| BitBit Reverb | `peak-reverb` | `$19` |

**Single engines are not products.** The nine one-engine pedals in the tree —
`peak-chorus`, `peak-eq`, `peak-overdrive`, `peak-phase`, `peak-spring`,
`peak-sympathy`, `peak-tape`, `peak-trem-pan`, `peak-wah` — are **out of scope
for 1.0**. One of them may later become a free giveaway; that is a decision for
after launch, not a reason to carry them now.

This is the decision that shrinks everything downstream, so hold the line on it:

- **QA is six products**, not fifteen — which, once D3 multiplies it by four
  formats and two platforms, is already 18 bundles a release. Six is what makes
  that survivable. Same for pluginval, the soak harness, the host matrix and the
  installers.
- **Presets are needed for six banks**, not fifteen.
- **Screenshots and demo audio are needed for six faces.**
- The nine stay in the repo and keep building — they share `ee_dsp`, several are
  the engines inside the four modules, and `ee_dsp_tests` covers them. They are
  simply not packaged, not tested in hosts, not given presets and not on the site.
- `scripts/package-macos.sh` ships the six. The other nine are listed there
  commented out, so turning one into the free giveaway later is one line.
- **If the free plugin happens**, it needs everything a paid one needs except the
  licence check: notarisation, a preset bank, a face that says BitBit, host
  testing. Budget it as a small release of its own, never as "we already have it".

### D3. The platform promise — **SETTLED: plugins, on macOS and Windows.** ✅

| | macOS | Windows |
| --- | --- | --- |
| **1.0** | VST3, AU | VST3 |
| Deferred | AAX, Standalone | AAX, Standalone |

**AAX and the Standalone apps are out of 1.0.** Both were in scope an hour ago
and both are cut in favour of selling sooner. What that buys is not marginal:

- **No AAX** removes the entire Avid and PACE chain — the developer agreement,
  the SDK, the `wraptool` signing step, a Pro Tools licence, and the awkward fact
  that AAX has no offline validator. That was the longest and least predictable
  part of the whole plan.
- **No Standalone** removes app icons, audio-device defaults, the Windows ASIO
  licensing question, and an entire extra artefact to sign and notarise.

**The Standalone still builds — it just is not sold.** It is the dev loop the
`fast` preset exists for ("a real app you can launch and hear"), so nothing
comes out of `AddPeakPlugin.cmake`; it is simply left out of the installers.
Do not "tidy up" by removing the format.

That leaves **18 shipping bundles**: six products × VST3 + AU on macOS, six ×
VST3 on Windows.

**The website is now wrong again and must be corrected before launch.** The spec
table claims `VST3 · AU · AAX`; it needs to say VST3 and AU on macOS, VST3 on
Windows, with AAX either absent or explicitly "planned". Claiming a format you
do not ship is the one marketing error that converts directly into refunds.

When AAX comes back, it comes back as its own small release with its own G0:
Avid, PACE, Pro Tools, and a host pass. Nothing in this plan forecloses it —
the plugin codes, the categories and the latency reporting it will need are all
already right.

---

## G0 — Accounts, licences and SDKs

**Start this first and start it today.** Everything in G0 is someone else's
approval queue, and none of it can be compressed by working harder. G1 does not
depend on G0, so run them side by side — but G2 (distribution) cannot begin
until G0 is done.

**One signing identity, and it already exists.** Everything signed on both
platforms goes out under the existing Polish sole proprietorship, **Paweł
Witkowski** — which is also the entity that owns `bitbitaudio.com`. No new
company is being formed for this release. Register the Windows signing identity
(0.2) under that same identity so the two agree; accounts opened piecemeal under
slightly different names is a mess that cannot be tidied afterwards.

### 0.1 Apple

**Decided: Individual enrolment, under the existing JDG (Paweł Witkowski).**
An Organization account would need a `sp. z o.o.` — a sole proprietorship is not
a legal entity and Apple does not accept DBAs or trade names — and forming one
purely to change a name in a dialog is the wrong trade. **A D-U-N-S number is
therefore not needed and is off the critical path**, which removes the longest
lead item this gate had.

The cost is exact and small: **the OS trust dialogs say "Paweł Witkowski"**, not
BitBit Audio. That is two moments per customer — the macOS first-launch /
installer sheet and the Windows SmartScreen publisher field. Everywhere the
customer actually spends time says BitBit Audio: the website, the LemonSqueezy
checkout, the plugin faces, and the manufacturer string in the DAW's plugin
browser (`COMPANY_NAME` in `cmake/AddPeakPlugin.cmake`).

Two consequences to carry forward:

- **Put it in the FAQ and the installer.** "Why does macOS say Paweł Witkowski?"
  — one line saying BitBit Audio is the trading name of a registered Polish sole
  proprietorship. Answering it before it is asked converts a moment of doubt at
  the install screen into a signal that a real person is behind the software.
- **Identity is sticky.** If a `sp. z o.o.` ever happens, moving to it means
  re-signing and re-notarising everything and starting SmartScreen reputation
  from zero. That is an argument for doing it *before* launch if it is likely
  within the year, and for not thinking about it again if it is not.

Then, once enrolled:

- **One Developer ID Application certificate.** It identifies the account, not a
  product — every `.vst3` and `.component` across all six products is signed with
  the same one. There is no per-product certificate, and Apple caps how many may
  be held anyway.
- **One Developer ID Installer certificate**, for the `.pkg`. Different type,
  same "one for everything" rule.
- Bundle identifiers stay per-product (`com.bitbitaudio.alpine`, …) and do **not**
  need registering as App IDs in the portal — that is an App Store and
  entitlements concern, not a Developer ID one.
- An **App Store Connect API key** for `notarytool`, in preference to an
  app-specific password: it survives an Apple ID password change and is what CI
  should hold.
- Notarisation is per *submission*, not per product. One signed `.pkg` with
  everything in it is one round trip and one ticket to staple.
- **Kill `--timestamp=none`.** `scripts/package-macos.sh` passes it today. A
  secure timestamp is what keeps already-sold binaries validating after the
  certificate expires (they run on a five-year clock); without one, everything
  shipped stops working on expiry day.
- **Back up the private key** (`.p12`, in a secret store, plus a copy you
  control). Notarised software survives certificate expiry. It does not survive
  losing the key and having to become a new identity.

### 0.2 Windows

- A **code-signing identity** for the same sole proprietorship as 0.1. Since
  mid-2023 the private key has to live in hardware or an HSM, so this is not a
  file you download. An **individual-validation** certificate is what fits here,
  not OV — **Certum** issues them and is Polish, which makes the identity check
  straightforward; SSL.com is the other option. Check current eligibility for
  **Azure Trusted Signing** rather than assuming, since its individual tier and
  its trading-history requirements have both moved. Start this early: the
  verification step is the slow part, and the publisher name it establishes is
  what SmartScreen reputation accrues against.
- A **Windows x64 build machine** — a physical box, a VM, or a
  `windows-latest` CI runner. A Windows-on-ARM VM under Parallels is fine for
  *running* a DAW but is the wrong shape for producing x64 release builds; keep
  the release artefact coming from one known x64 machine or runner.
- **WebView2**: the six WebView faces need it. It is evergreen and present on
  Windows 11 and most Windows 10 installs, but the installer must detect it and
  run Microsoft's bootstrapper when it is missing, or those six plugins open on
  a blank window.

### 0.3 The licence audit nobody remembers until later

Do this once, write down the answers, and keep the receipts:

- **A trademark search on "BitBit", and do it first.** EUIPO for the EU, UPRP
  for Poland, plus a plain search for existing audio software using the name.
  This is not a formality: the brand is already frozen into the plugin codes,
  the bundle ids and the preset directory, and those cannot change after the
  first sale without every saved session losing its plugins. An afternoon of
  searching now against the most expensive class of problem this project has.
  Registering the mark is optional; *knowing whether someone else holds it* is
  not.
- **JUCE.** Dual-licensed. Confirm which tier covers a closed-source commercial
  release at your revenue, and whether that tier still requires the JUCE splash
  screen. This is not optional and it is easy to get wrong by assuming.
- **Steinberg VST3 SDK.** Also dual-licensed — GPLv3, or Steinberg's proprietary
  agreement. Shipping a closed-source VST3 means signing their licensing
  agreement and registering the product. Very commonly missed.
- **DaisySP**, which the shimmer's `PitchShifter` comes from, and every other
  vendored dependency in `vendor/` and `_deps/` — check each licence and collect
  the attributions.
- **Fonts.** Space Grotesk and anything else embedded in the faces or the site.
- The result is one **third-party notices** file that ships in the installer and
  is linked from the site. Write it once now; assembling it under launch pressure
  is how the wrong thing gets shipped.

### 0.4 Deferred with AAX

Not needed for 1.0, listed so the cost is remembered rather than rediscovered:
an **Avid developer registration** and the AAX SDK, a **PACE/iLok developer
account** for `wraptool` (a shipping Pro Tools refuses unsigned AAX whether or
not you use copy protection), and a **Pro Tools licence** to test in, because
there is no offline AAX validator and `pluginval` does not cover it.

Worth knowing now so it does not distort the G3 decision: **your customers never
need an iLok.** PACE *signing* is mandatory for AAX; PACE *licensing* is a
separate product you are free not to buy.

**ASIO** goes with the Standalone: the SDK comes from Steinberg under a
non-redistributable agreement, and without it a Windows Standalone falls back to
WASAPI — fine for auditioning, poor for playing through.

### 0.5 Exit criteria

G0 is done when you can, on a clean machine, produce a signed and notarised
macOS installer and a signed Windows installer containing VST3 and AU (macOS)
and VST3 (Windows) for all six products — even if the plugins in them are still
buggy. Prove the *pipeline* before you polish the *product*.

---

## G1 — Correctness. Nothing ships until this is green

This is item 2 on your list, and it is the gate that matters most. A crash in a
paid plugin costs a refund, a review and a support thread; a crash in a beta
costs nothing.

### 1.1 Freeze the contract first

Before hunting bugs, freeze what must never change:

- **Parameter IDs per plugin.** Add a golden-file test: dump every parameter ID,
  range, default and version-hint to a checked-in text file and fail the build
  when it changes without the file being updated. An accidental rename silently
  breaks every saved session and preset, and there is no test for it today.
- **Engine enum order.** Already a house rule (`CLAUDE.md`: append LAST). Put it
  in the golden file too.
- **The preset format.** Write a state-version tag into the APVTS tree now, even
  though nothing reads it — the release where you need to migrate state is much
  easier if 1.0 already stamped a version.

### 1.2 pluginval, in CI, at strictness 10

This is the highest-value single item in the whole plan and the tree has none of
it. `pluginval` is what every host developer's bug report will otherwise tell you.

- Run it over **each of the six products** × every format at `--strictness-level 10`
  `--validate-in-process` and `--repeat 5`, **on macOS and on Windows**.
- `auval -strict -v aufx <CODE> <MFR>` for every AU, in **both architectures**
  (`arch -arm64` and `arch -x86_64`) — the universal-binary trap in `CLAUDE.md`
  means "it loads here" proves nothing.
- **AAX has no offline validator** — `pluginval` does not cover it and Avid ships
  no equivalent, so the only test is Pro Tools itself. That asymmetry is part of
  why AAX is deferred, and it is the reason to add it as a separate small
  release once the same DSP has been beaten up through VST3 and AU.
- Wire it all into GitHub Actions on **a macOS runner and a Windows runner**,
  alongside `ee_dsp_tests`, `ee_preset_tests` and the `*_regress` checksums
  (per-platform baselines — see G2.2). **There is no CI at all right now**, which
  means the two known failures are the only ones anyone notices.

### 1.3 Sanitizers, because you already know there is a race

Build the offline host tools (`ee_*_host`, `ee_dsp_tests`, `ee_*_stress`) under:

- **ASan + UBSan** — cheap, catches the overwhelming majority of real crashes.
- **TSan** — you have a confirmed data race waiting: `daisysp::myrand()` backs the
  shimmer's modulation slew and is one function-local `static uint32_t seed`
  advanced per sample, shared by every instance in the process and touched from
  multiple audio threads (`CLAUDE.md`, "Shimmer is not bit-reproducible"). It is
  documented as inaudible. It is still undefined behaviour in shipping code, and
  it blocks checksumming any shimmered render. Fork the DaisySP `PitchShifter` to
  take a per-instance seed and the race and the reproducibility gap both close.
- **MSan or valgrind** for the second known failure: *"chorus is silent on a
  silent input" fails on roughly half of runs with no rebuild in between.*
  Nondeterminism with a fixed binary and fixed input is uninitialised memory, a
  shared mutable static, or a denormal/FTZ difference. **Do not ship with this
  open** — it is the shape of bug that becomes "it sometimes drops out in Logic".

### 1.4 The two known failures

`CLAUDE.md` lists them as baseline and `scripts/dev-check.sh` filters them out.
That filter is right for daily work and wrong for a release. Both get fixed, or
explicitly re-baselined with a written reason, before G1 closes:

- `tape at 100 % moves the level too far` — either the test's expectation or the
  voicing is wrong. Decide which.
- `chorus is silent on a silent input` — see above.

### 1.5 A real soak harness

You already have the pieces: `ee_grain_host`, `ee_alpine_host`,
`ee_modulation_host`, `ee_reverb_host` drive real processors, and
`tests/RegressHarness.h` has the ragged block sizes and the `FakePlayHead`.
Generalise them into one `ee_soak <plugin>` that runs for hours and asserts on
every block:

- output is finite, and below a sane ceiling
- no allocation on the audio thread (hook `operator new` during `processBlock`)
- no lock taken on the audio thread

…while randomising, continuously:

- every parameter, including engine switches mid-note (engine switching is a
  known click risk — `ee_module_stress` covers a slice of this already)
- block size 1 … 8192, including 1-sample and prime sizes
- sample rate 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz, with `prepareToPlay`
  re-entered mid-run
- transport: rolling, stopped, looping, and **relocating** — the tempo-locked
  engines only reach their alignment code with a playhead
- preset loads *during* processing. This is the exact shape of the
  `PeakDelayProcessor::installState` bug ("a whole tree arriving at once is not a
  knob being turned"); assume there are more of them.
- editor open / close cycles, and instance create / destroy
- 32+ instances in one process — the only reliable way to surface shared statics
  like the DaisySP one

Run it overnight per product — six of them, per D2. A clean 8-hour soak per
product is a reasonable bar
for G1.

### 1.6 Input fuzzing where user data enters

`PresetStore` reads XML the user can edit, move and corrupt. Fuzz it with
truncated, malformed, wrong-schema and enormous files — a plugin that throws on a
bad preset file takes the host down with it.

### 1.7 Other correctness checks worth running once each

- `ScopedNoDenormals` present in every `processBlock` — a missing one is a 10×
  CPU spike on a reverb tail and reads to the user as "it freezes".
- Offline bounce vs realtime render must match. Hosts render offline faster than
  realtime and anything that reads a wall clock will break.
- Mono→stereo, stereo→stereo and any mono→mono configuration the hosts will try.
- Leak check across 100 editor open/close cycles.
- CPU ceiling per product at 96 kHz with everything on, and Alpine with all four
  modules active. Publish the numbers; set expectations before a reviewer does.

### 1.8 Then, and only then, humans

A closed beta of 10–20 people, on real projects, with a bug-report template that
asks for host + host version + OS + architecture + plugin format + plugin version
+ steps. Recruit for the matrix you actually ship: you need Windows testers,
and they are the hardest to find late.

Most of what they report will be usability rather than crashes, which is exactly
the category you cannot test for yourself.

---

## G2 — The Windows build, and installers both platforms trust

Six products: VST3 + AU on macOS, VST3 on Windows. **18 shipping bundles per
release** — enough that every step below belongs in CI rather than in your
hands. The Standalone keeps building on both platforms as the dev loop; it is
simply not copied into either installer.

### 2.1 The Windows build

The good news first: the macOS-specific CMake is already behind `if(APPLE)`, and
`ccache` is a `find_program` that simply does not fire elsewhere, so the tree
should configure on Windows without a fight. What will need doing:

- MSVC toolchain, Ninja, and a decision on `sccache` to replace ccache.
- **WebView2** for the six WebView faces. `NEEDS_WEBVIEW2` and
  `JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1` are already set in
  `AddPeakPlugin.cmake` — verify the package actually resolves on the build
  machine, because a face that silently falls back is a blank plugin window.
- **Paths.** `PresetStore::userDirectory()` uses
  `juce::File::userApplicationDataDirectory`, which lands in `%APPDATA%` on
  Windows — correct as written, but the comment in that file and in `CLAUDE.md`
  says `~/Library` as though it were the only case. Fix the comment and verify
  the folder is created on a fresh profile.
- VST3 installs to `C:\Program Files\Common Files\VST3`.
- Compile the whole tree once with warnings turned up: MSVC will find narrowing
  conversions and unused-parameter cases that Clang waves through.
- Run `ee_dsp_tests` and the `*_regress` batteries on Windows. **Expect the
  checksums to differ from macOS** — different compiler, different FMA and
  library maths. The batteries are per-platform baselines, not one shared
  number; decide that now rather than debugging a "failure" later.
- Denormal handling: confirm `ScopedNoDenormals` behaves the same under MSVC.

### 2.2 Signing and installing — macOS

Today `scripts/package-macos.sh` does `codesign --force --sign -` — **ad-hoc**.
That is fine for your own machine and unusable for customers: Gatekeeper blocks
it, and the `INSTALL.txt` instruction to run `xattr -dr com.apple.quarantine` is
the moment a normal buyer asks for a refund.

1. **Developer ID Application** certificate — sign every `.vst3` and
   `.component` with `--options runtime` (hardened runtime) and a secure
   timestamp. Check whether any host needs
   `com.apple.security.cs.disable-library-validation`.
2. **Notarisation** — `xcrun notarytool submit --wait`, then `xcrun stapler
   staple` every bundle **and** the installer.
3. **A real installer.** `pkgbuild` per component + `productbuild` with a
   distribution XML, signed with a **Developer ID Installer** certificate. Users
   do not drag plugins into `~/Library`. The installer should let them choose
   formats, show the EULA and the third-party notices, and install to the system
   folders.

### 2.3 Signing and installing — Windows

1. Sign every `.vst3` and the installer `.exe` with the identity from G0.2,
   timestamped. An unsigned installer gets a SmartScreen wall, and SmartScreen
   reputation only accrues once you are consistently signing with one identity —
   which is a reason to get the certificate early even if you ship late.
2. An installer — **Inno Setup** or **WiX** — with the EULA, the WebView2
   bootstrapper check from G0.2, and an entry in Add/Remove Programs.
3. Test on a **clean Windows VM** with no Visual C++ runtime, no WebView2 and no
   DAW preinstalled. This is where a missing redistributable shows up.

### 2.4 Both platforms

- **Uninstallers.** macOS: a script or a documented path list. Windows:
  Add/Remove Programs, which the installer gives you for free if you use it.
- **Verify on machines that have never seen the plugin**, with the installer
  downloaded through a browser so the quarantine bit and SmartScreen are real.
  VM snapshots are the only honest test.
- **One entry point per platform**, `package-macos.sh` and its Windows
  counterpart, both driven from CI so a release is never hand-built. With 18
  bundles to sign and notarise, manual is not a strategy.

---

## G3 — Commerce and licensing (your item 1, the LemonSqueezy part)

There is **no licence check, no activation, no trial and no DRM** anywhere in the
codebase. That is a feature to build, not a setting to switch on.

### Store

LemonSqueezy is a good pick specifically because it is **Merchant of Record** — it
handles EU VAT, US sales tax, invoices and chargebacks, which is otherwise a
genuinely large amount of work for a solo product.

Set up: one product per SKU (Alpine, Grains, four modules), a bundle if you want
one, the customer portal for re-downloads, and webhooks for order events.

### Licensing — build the smallest thing that works

Recommended shape, in order of how much it costs you:

1. **LemonSqueezy licence keys** (built into the platform: generate on purchase,
   `activate` / `validate` / `deactivate` endpoints, activation limit per key).
2. **In-plugin activation screen** — email + key, one HTTP call, on a background
   thread, never the audio thread. Success writes a licence file to
   `~/Library/Application Support/<Brand>/license.json`.
3. **Offline after activation.** Do not phone home on every launch; a studio
   machine may be permanently offline. Re-validate on a long interval with a
   generous grace period, and never disable the plugin because a request failed.
4. **Sign the licence file** (Ed25519, public key compiled in) so the offline
   check needs no server and cannot be forged by editing JSON.
5. **Device limit** of 3–5 activations with a self-service deactivate in the
   customer portal.

Do not build more than this. Every additional layer of protection costs you more
in support tickets from paying customers than it recovers from people who were
never going to pay.

### Free codes for friends and YouTubers (your item 1)

Use **100 %-off discount codes** in LemonSqueezy rather than a separate code path
in the plugin. The recipient "buys" for `$0`, gets a real licence key through the
normal flow, and you get a row in your dashboard telling you which code was used.
One mechanism, no special-case code, full attribution.

Do **not** put a public "free codes" page on the site. Distribute codes privately;
if you want a redemption page, make it an unlisted `/redeem` route that only
validates server-side.

### Trial

Decide explicitly. A 14-day full-featured trial pairs naturally with the 14-day
refund promise already on the site and removes most pre-sale support questions.
The alternative — a periodic noise burst or a silence every 60 s — is cheaper to
build and much worse for reviews.

---

## G4 — Updates (your item 3)

Nothing exists today; the version is `0.10.0` in the top-level `CMakeLists.txt`.

**Recommended for 1.0 — the boring option that works:**

- Publish `https://<domain>/updates.json` with `{product: {version, notes_url,
  download_url, min_macos}}`.
- On editor open, at most once per day, on a background thread, fetch it. If it is
  newer, show a small dismissable pill in the face linking to the download page.
  Failures are silent. Make it switchable off, and say so in the privacy policy —
  a version check is a network request and you now have a privacy page to keep
  honest.
- Downloading and installing stays manual: the notarised `.pkg` from the customer
  portal. Auto-install of an audio plugin while the host has it loaded is a
  category of bug you do not want to own in 1.0.
- **Sparkle** is worth it later, and only if a Standalone app is ever sold.

**The discipline that matters more than the mechanism:**

- Semantic versioning, and a git tag plus an archived signed artefact for every
  release. Users *will* need to roll back; if you cannot hand them the previous
  build, a regression becomes a refund.
- Never remove or rename a parameter ID. New parameters get defaults that
  reproduce the old behaviour exactly — the `*_regress` checksum batteries are
  exactly the tool that proves it, and `ee_spring_regress` already demonstrates
  the pattern for an additive change.
- A public changelog page. It is also your best "still actively developed" signal.

---

## G5 — Content: presets, the plate engine, latency, the preset browser

Do the DSP-changing items (**plate**, **latency**) *before* you freeze the release
regression checksums — both alter audio, and re-baselining after the fact removes
the only evidence that nothing else moved.

### 5.1 Presets (your item 4)

Where it stands, for the six that ship:

| Product | Factory presets |
| --- | --- |
| BitBit Delay | 22 |
| BitBit Artifact | 1 (Init) |
| BitBit Reverb | 1 (Init) |
| BitBit Modulation | 1 (Init) |
| BitBit Grains | 1 (Init) |
| BitBit Alpine | 0 |

(`peak-sympathy` has 7 and is out of scope; the rest have none.)

For products you are charging for, this is the largest content gap in the plan.
The website already promises "global presets across the whole chain".

- Set a target per product. 30–40 for Alpine, 25+ for Grains, 15–20 per module is
  a defensible floor. That is six banks, not fifteen — D2 is what makes this
  achievable at all.
- Author with `-DEE_PRESET_AUTHOR=ON` (writes straight into the pedal's `presets/`
  folder for committing).
- Use the `Category - Name.xml` filename convention — the prefix *is* the category
  in the picker, and a flat glob is all a factory preset gets.
- `ee_preset_tests` already enforces that **every parameter appears in every
  preset**; keep it in CI, because a partial preset silently inherits from
  whatever was loaded before it.
- Alpine's presets should exist to show off the thing the product is sold on:
  make several of them reorder the chain.
- `peak-grain` is already on `ee::plugin::PresetStore` — the note in `CLAUDE.md`
  about it having its own flat, user-only store is stale. Its user presets did
  move with the rebrand, though: anything under `~/Library/Peak/Peak Grain` needs
  copying to `~/Library/BitBit/BitBit Grains` on your own machine.
- Get a second pair of ears. Presets are the demo most buyers judge you on, and
  the author is the worst judge of their own.

### 5.2 Plate reverb engine (your item 6)

Note first: **`ee::dsp::FdnReverb` is already plate-voiced** — its own header
describes it as a plate-voiced 16-line FDN, and it is what Peak Reverb's Space
engine runs. So "add a Plate engine" is one of two quite different jobs:

- **Cheap and credible:** a third engine in `ee::fx::ReverbModule` that runs
  `FdnReverb` from a new `PlateConfig.h` — shorter predelay, higher input
  diffusion, tighter modulation, no shimmer — voiced deliberately against Space so
  the two are obviously different in a preset A/B.
- **Expensive and distinctive:** a true Dattorro plate as a separate engine. A
  real project, with its own voicing loop and reference material.

Either way, the checklist is the one in `CLAUDE.md` for adding an Artifact engine,
transposed: **engine enum appended LAST**, `ReverbModule` prepare/render/reset,
`peak-reverb` params + `Init.xml`, `peak-alpine` `rev.` bindings, the
`module-face` engine list, `tests/UiSnapshot.cpp` mirrored (nothing catches drift
there), `ee_reverb_host` coverage, a `*_regress` section asserting the new
controls at their defaults are **bit-identical** to before, and the README.

### 5.3 Latency (your item 7)

Where it stands:

| Product | Reported latency | Comes from |
| --- | --- | --- |
| Peak Tape | ~264 samples (~6 ms @ 44.1 k) | `TapeTransport` 198 + `TapeCharacter` 66 |
| Peak Delay | tape input latency | `DelayModule::tapeIn`, pre-section only |
| Peak Modulation | tape engine only | other engines report 0 |
| Peak Reverb | 0 | both engines latency-free |
| Peak Alpine | sum of Artifact + Modulation + Delay | per-module |
| Everything else | 0 | no `setLatencySamples` |

So the site's "~6 ms, reported and compensated" is true of the tape path and
*wrong* for Grains and Reverb, which are zero. Make the claim per product.

Work worth doing, in order:

1. **Prove the compensation.** Null test in a real host: the same audio on two
   tracks, one through the plugin at Mix 0, polarity inverted. It should cancel to
   silence. If it does not, PDC is wrong, and that is a far worse bug than 6 ms.
2. **Re-report on every change.** Latency must be pushed to the host when the
   engine changes or a module is bypassed/reordered, not only in `prepareToPlay`.
   Verify Alpine does this — a stale latency number is a subtle timing bug the
   user will blame on their own playing.
3. **Then reduce it.** The 198-sample nominal in `TapeTransport` is the budget the
   wow/flutter modulation is allowed to swing within; shortening it narrows the
   movement before the read pointer crosses the write pointer. A **"Low latency"**
   switch on the tape stage that halves the nominal and clamps flutter depth gives
   the user the choice instead of you guessing.
4. Anything below ~5 ms stops mattering to most players. Do not trade the tape
   voicing — which is the product — for a number.

### 5.4 Full-screen preset browser (your item 8)

Today: `PresetPicker.jsx` is a Mantine Cascader dropped out of a 118 px (joined) /
190 px (separated) combobox in `PresetBar.jsx`, `withinPortal: false`, opening
inline under the bar.

A full-face browser is a different component, not a size change:

- New component in `packages/pedal-ui` so all six WebView pedals get it at once.
  The nine native `ee::ui` pedals keep their JUCE menu — do not try to unify.
- Overlay portalled to the face root, covering the whole `.pui-card`, with a
  backdrop, ESC and click-outside to close.
- Category column + preset list + a search field, arrow-key navigation, Enter to
  load, and the current preset highlighted on open.
- Keep `PresetBar`'s prev/next steppers and the Save box exactly as they are —
  they are the fast path and the browser is the browse path.
- Both variants (`joined`, `separated`) must keep working; Peak Artifact runs with
  `showSteppers={false}`.
- Watch the gallery CSS-collision rule in `CLAUDE.md`: scoping classes must be
  unique across *all* WebView pedals, not just within one.
- `apps/pedal-gallery` is the dev loop; remember `useJuceSliderValue` has no echo
  outside a host.

---

## G6 — Hosts and platforms (your item 5)

Per host, run the same list: scan/validate, PDC null test, automation write and
read back, save and reload the session, change the sample rate with the plugin
loaded, offline bounce, editor resize, several instances, CPU under load, and
close/reopen the editor twenty times.

**macOS**

| Host | Formats | Why it is on the list |
| --- | --- | --- |
| Ableton Live 11 + 12 | AU, VST3 | Your primary target. Test **native and under Rosetta** — Live ships universal and the architecture trap in `CLAUDE.md` bites here |
| Logic Pro | AU only | Strictest AU validation; sandboxed; will reject things `auval` lets through |
| Reaper | VST3, AU | Best bug-finder: exotic block sizes, channel counts, offline render modes |
| Cubase / Nuendo | VST3 | Strictest VST3 bus and parameter rules |
| Studio One | VST3, AU | Large user base, aggressive plugin scanning |
| Bitwig | VST3 | Sandboxed per plugin; surfaces crashes cleanly |
| FL Studio | VST3, AU | Unusual parameter/automation model |
| GarageBand | AU | Free, sandboxed, and where your least technical users are |

**Windows** — a second full pass, not a spot check. The formats differ (no AU),
the paths differ, the WebView differs, and the compiler differs, so a macOS pass
proves nothing here.

| Host | Formats | Why it is on the list |
| --- | --- | --- |
| Ableton Live 11 + 12 | VST3 | Same primary target, different plugin host |
| Cubase / Nuendo | VST3 | Steinberg's own host on Steinberg's own format |
| Reaper | VST3 | Cheap licence, ruthless edge cases |
| FL Studio | VST3 | Far bigger on Windows than on macOS |
| Studio One / Bitwig | VST3 | Round out the scan-and-crash coverage |

Plus, on both: the **Standalone** app. It is not sold, but it is how you and
your beta testers reproduce anything without a host in the way, so it should at
least launch and make sound on a clean machine.

**Two release artefacts, two CI runners.** With Windows in scope, every gate
above doubles at the point where it touches a binary: `pluginval` runs on both,
the soak harness runs on both, the `*_regress` checksums have a baseline per
platform, and the installer is built and smoke-tested on both.

---

## G7 — Website (your item 1)

### Screenshots

Make them generated, not hand-captured, so they never drift from the product:

- `ee_ui_snapshot` already renders every native face to PNG. Extend it, or add a
  Playwright script at `deviceScaleFactor: 2` for the WebView faces, and commit
  the output into `apps/website/public/assets/`.
- Fix one standard: same zoom, same window chrome, a chosen preset per shot rather
  than Init, transparent or consistent background.
- Note the trap in `CLAUDE.md`: **`tests/UiSnapshot.cpp` duplicates every pedal's
  parameter layout and spec, and nothing checks it against the real plugin.** If
  the marketing screenshots come from it, that duplication is now a marketing bug
  too. Prefer capturing the real editor.
- The Grains page currently crops one screenshot per section (`focus` / `zoom` /
  `aspect` in `apps/website/src/data.js`) because only two captures exist. Real
  per-section captures replace both the crops and those fields.

### Audio demos

- Record one take per source (Dry Guitar, Clean Rhythm, Synth Pad, Drum Loop) and
  render a wet pass per preset from the **same** take, at matched loudness — the
  A/B switch is worthless if the wet one is simply louder.
- Wire `AbPlayer.jsx`: two preloaded `<audio>` elements sharing a transport,
  switching on the Dry/Processed toggle, with the scrub bound to real time.
- Replace the fake waveform bars with real peaks — analyse the files at build time
  into a small JSON.
- Normalise, trim silence, export as AAC or Opus, and keep them short.

### Video

- Under 90 seconds per product. No talking head. Play the riff dry, then wet, and
  show the one control that explains the product (Alpine: the drag-reorder;
  Grains: Freeze and Stretch).
- Embed click-to-load: a poster image that swaps in the YouTube iframe on click.
  Faster, and it keeps YouTube's cookies off the page until the user asks for them.

### Analytics, errors, legal

- **Plausible or Fathom**, not GA4 — cookieless and consent-banner-free, which
  matters because the rest of this page is a sales page.
- **Sentry** for the site's JS. For the plugin, there is no crash reporter today;
  if you want one, the unsold Standalone is the safe place to put it — opt-in,
  and never inside a host. Otherwise rely on the beta
  and on a good bug-report template.
- **EULA, privacy policy, terms, refund policy.** Linked in the footer, and the
  EULA shown in the installer. The privacy policy must mention the update check
  and the licence activation call.
- **Trader details in the footer.** EU consumer law wants the trader
  identifiable: the registered name (Paweł Witkowski), address, NIP and a contact
  address. BitBit Audio is the trading name over the top of that, and the FAQ
  entry from G0.1 explaining the signature name belongs alongside it.

### Launch

- Netlify from GitHub: build `npm run build --workspace bitbit-website`, publish
  `apps/website/dist`. The SPA fallback is already in place
  (`apps/website/public/_redirects`) — without it `/alpine` and `/grains` 404 on
  reload.
- Domain, HTTPS, www → apex redirect, and a `support@` mailbox that a human reads.
- `og:image` and Twitter card per page, favicon, sitemap, `robots.txt`.
- Lighthouse pass, real mobile devices, Safari **and** Firefox — `backdrop-filter`
  and `aspect-ratio` are both used in the styles.
- Then: replace every `href="#"` Buy button with its LemonSqueezy checkout, and
  **correct the spec table**. It currently claims `VST3 · AU · AAX`; 1.0 is VST3
  and AU on macOS and VST3 on Windows, with no AAX and no Standalone. List AAX
  as planned or not at all — claiming a format you do not ship is the marketing
  error that turns straight into refunds. Add the per-format install paths and
  the WebView2 note somewhere a Windows buyer will find them.

---

## What else you need, that is not on your list

1. **Every account and licence in G0.** All three decisions are made, so the only
   thing that can still stall the release is someone else's approval queue —
   Apple and the Windows certificate authority. Start them today.
2. **EULA, privacy policy, terms**, and the third-party notices file from G0.3.
   Required by Apple's paperwork and by the first customer who asks.
3. **A manual.** `README.md` is already an excellent one — publish it as a docs
   site or a PDF in the installer rather than writing a second one. Trim it to
   the six products, or mark the other nine clearly as not-for-sale.
4. **A support pipeline.** An inbox, canned answers for the ten questions you will
   be asked ten times, and a bug-report template.
5. **An uninstaller.**
6. **Trial or no trial** — decide it, do not default into it.
7. **Beta testers**, recruited before you need them.
8. **Launch assets** — press kit, a short product one-pager, banner images, and a
   list of people to send free codes to. This is the same list as your YouTuber
   codes; write it early.
9. **Archived builds per release.** Tag the commit, keep the signed artefact.
10. **An upgrade path** from a `$19` module to Alpine. Customers will ask
    immediately, and it is a per-customer discount code in LemonSqueezy.
11. **An installer icon**, and `.icns` for the Standalone apps if they are ever
    sold.
12. **Verify the macOS deployment target** actually matches the "11+" claim.
13. **VST3 subcategory metadata** so hosts file the plugins under the right
    headings.
14. **Parameter display names** — hosts surface them in automation lanes. Check
    they are readable and, once shipped, frozen.
15. **Published CPU figures**, so a reviewer does not invent their own.
16. **Accessibility**: keyboard access and text scaling in the WebView faces, and
    the native pedals' existing zoom range.
17. **A rollback story** for the first bad release, because there will be one.

---

## Gate summary

```
D1 ✅ BitBit     D2 ✅ six products     D3 ✅ plugins only — mac VST3/AU, win VST3

G0  paperwork  ──────────────┐   Apple Individual (existing JDG) · Windows IV
    (start today, runs in    │   cert · Windows x64 runner · trademark check ·
     parallel with G1)       │   JUCE and VST3 licence audit · third-party
                             │   notices
                             │
G1  correctness              │   golden param file · CI on both platforms ·
    (needs nothing from G0)  │   pluginval 10 · auval strict · ASan/UBSan/TSan ·
                             │   both known failures closed · soak harness ·
                             │   preset-loader fuzzing          — six products
   ↓                         │
G5a DSP content              │   plate engine · latency work
    ← before checksums freeze│
   ↓                         │
G1b re-baseline              │   *_regress checksums frozen, per platform
   ↓                         │
   └─────────────────────────┴──→  both must be green before:
   ↓
G2  builds +      Windows build · Developer ID + notarised .pkg · signed Windows
    distribution  installer · uninstallers · clean-machine install test on both
   ↓
G3  commerce      LemonSqueezy SKUs · licence keys · activation UI · free codes ·
                  trial decision
   ↓
G4  updates       updates.json + in-face notice · versioning · changelog · archive
   ↓
G5b content       six preset banks to target · preset browser
   ↓
G6  hosts         the macOS matrix + the Windows matrix
   ↓
G7  website       spec table corrected · screenshots · audio · video · analytics ·
                  legal · domain · live
   ↓
beta (incl. Windows testers) → soft launch → launch

deferred          AAX (Avid + PACE + Pro Tools, its own small release) ·
                  Standalone apps · the nine single-engine pedals
```

All three decisions are closed, so the plan no longer has an unknown in it — only
work and queues, and cutting AAX and the Standalone apps removed most of the
queues. What is left that can still sink the schedule is **the Windows
certificate started late**, since its verification moves at someone else's
speed, and **pluginval skipped**, because it is the cheapest bug-finding in the
whole document.
