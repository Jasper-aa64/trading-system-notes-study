# Illustration Generation Specification

This specification defines the preferred visual style for blog illustrations in this workspace.

Use it whenever generating or revising images for technical notes, especially AI engineering, agent workflow, quant engineering, and software quality articles.

---

## Target Style

The preferred style is an **academic graphite pencil technical illustration**.

It should feel like:

- a figure from a scientific journal or engineering textbook,
- a precise technical drawing rendered in graphite on white paper,
- an architectural or mechanical diagram with careful cross-hatching,
- a classroom blackboard diagram drawn with precision and intent.

It should not feel like:

- a colorful hand-drawn cartoon or doodle,
- a dark SaaS landing-page hero,
- a glossy product marketing banner,
- a neon cyberpunk graphic,
- a watercolor notebook sketch,
- a generic AI stock image.

---

## Visual Characteristics

### 1. Medium

Use a clean white or very light cream paper background.

Do **not** use graph paper grids, notebook rulings, or textured backgrounds.
The paper should be plain, like a blank technical drawing sheet.

The image should look like a precise pencil drawing made by an engineer or scientist, not a colorful doodle.

### 2. Linework

Prefer:

- precise, confident graphite pencil lines,
- careful cross-hatching and parallel hatching for shading and depth,
- thin technical diagram lines for structure,
- clean ruled arrows and callout lines,
- architectural or mechanical rendering conventions.

Avoid:

- imperfect wobbly cartoon sketch lines,
- watercolor washes or ink splashes,
- glossy 3D rendering,
- hard vector corporate icons,
- heavy color gradients,
- photorealism.

### 3. Color Palette

**Monochrome graphite only.** No color fills. No color accents.

Use only:

- white paper background,
- graphite gray for lines, hatching, and shading,
- black for emphasis lines and text.

If a subtle second tone is absolutely necessary (e.g., to separate two panels in a comparison), use a very light wash of warm gray — not green, not blue, not amber.

Avoid all color: no pale green, no light blue, no beige accents, no watercolor.

### 4. Composition

Use concrete engineering metaphors rendered as precise technical objects:

- balance scales,
- mechanical presses or stamps,
- architectural gateways or colonnades,
- pipeline cross-sections,
- drafting board diagrams,
- checklist clipboards,
- terminal screens under glass domes,
- wire bundles and conduits,
- flow diagrams with clear directional arrows.

The image should explain an idea through a precisely rendered metaphor, not through abstract decoration.

### 5. Text

All text must be in **English only**. No Chinese characters inside illustrations.

Text should be sparse and legible:

- one clear title,
- one short subtitle,
- 2–5 labels or callouts,
- short technical annotations.

Use clean technical serif or sans-serif lettering (like a drafting font or academic figure caption). Avoid decorative or handwritten fonts.

Avoid:

- long paragraphs inside the image,
- small dense text blocks,
- many overlapping labels,
- decorative text that does not explain the concept.

### 6. Layout

Preferred layouts:

- side-by-side comparison (two-panel),
- sequential pipeline left-to-right,
- single central object with callout lines,
- stacked timeline comparison,
- arcade / colonnade of objects in sequence.

Keep the structure readable at blog width (16:9 aspect ratio).

---

## Prompt Template

Use this template when asking for a generated illustration:

```text
Academic graphite pencil illustration on clean white paper. 16:9.
Style: precise technical linework, careful cross-hatching for shading
and depth, monochrome graphite only — no color, no watercolor,
no graph-paper grid. Scientific journal or engineering textbook style.
NOT cartoon. NOT colorful. Precise and academic.

Topic: <topic>

Main metaphor: <concrete object — e.g. classical arcade of four archways,
mechanical stamp cross-section, wire bundle passing through gates,
balance scale, two-panel dissection diagram>.

Layout: <side-by-side / stacked timelines / single object with callouts /
left-to-right pipeline>.

Objects and labels:
- <element 1>: labeled "<English label>"
- <element 2>: labeled "<English label>"
- ...

Title (at top, large): "<Title>"
Subtitle (below title): "<Subtitle in English>"
Footer caption: "<one-line annotation>"

All text in English. No Chinese characters.
No color fills. No gradients. Monochrome graphite only.
```

---

## Reference Images

The two canonical reference images for this style are:

**Reference A — Tangled rope vs. infinity loop:**
Precise pencil rendering on white background. Fine overlapping linework for the rope tangle; smooth controlled curves for the infinity loop. Clean printed annotations above and below each object. No color. Academic and composed.

**Reference B — Classical temple colonnade:**
Architectural pencil rendering with detailed cross-hatching on stonework. Three objects (clipboard, CPU under glass dome, compass) sit on plinths between columns. Text annotations below each column. Monochrome gray tones. Structured and precise.

New illustrations should match the visual weight and precision of these references.

---

## Quality Checklist

Before accepting an illustration, verify:

- [ ] Does it look like a technical journal figure or textbook plate?
- [ ] Is the background clean white or very light cream (no grid, no texture)?
- [ ] Is the image fully monochrome graphite — no color anywhere?
- [ ] Is the linework precise, not wobbly or cartoon-like?
- [ ] Is cross-hatching used for shading instead of color fills?
- [ ] Is all text in English, sparse, and legible?
- [ ] Is the metaphor concrete and rendered as a real object?
- [ ] Does the image explain the article section rather than merely decorate it?
- [ ] Does it avoid cartoon, watercolor, and SaaS aesthetics?