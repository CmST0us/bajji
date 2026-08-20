# Bajji StopWatch Random Image Figma Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a complete, reviewable Figma file for the Bajji StopWatch pairing, random-image settings, image display, physical-button interactions, and error states.

**Architecture:** Build one Figma Design file with seven pages. Establish local foundations and reusable components first, compose all 466×466 circular screens from them, then add prototype links and validate every screen with structural metadata and screenshots.

**Tech Stack:** Figma Design, Figma Plugin API, Auto Layout, local variables, components/variants, Prototype interactions.

**Spec:** `docs/superpowers/specs/2026-08-20-stopwatch-random-image-ui-ux-design.md`

## Global Constraints

- Screen frame is exactly 466×466 px with a circular clipping mask.
- Critical text and touch targets stay inside the centered 328×328 px safe area.
- Touch targets are at least 48×48 px; standard interactive rows are 56 px high.
- Chinese text uses Source Han Sans SC; numerals and English API values use Montserrat.
- Default request is `category=bq&type=eciyuan`.
- `type` is visible only for `bq`, `acg`, and `furry`.
- KEY A cycles display modes; KEY B refreshes; A+B held together for 1000 ms returns to settings and cancels on early release.
- Normal image viewing has no permanent toolbar.
- Repeated elements are components or instances; related children use Auto Layout.
- Each write returns every created or mutated node ID and is followed by metadata or screenshot validation.

---

### Task 1: Create and inspect the target Figma file

**Files:**
- Create: Figma file `Bajji StopWatch · Random Image UX`
- Read: `docs/superpowers/specs/2026-08-20-stopwatch-random-image-ui-ux-design.md`

**Interfaces:**
- Consumes: confirmed UI/UX specification.
- Produces: `fileKey`, page IDs, available font names, and a map of existing local variables/components.

- [ ] **Step 1: Create the file**

Create a Figma Design file named `Bajji StopWatch · Random Image UX`; store the returned `fileKey` for every later call.

- [ ] **Step 2: Inspect the empty file before writing**

Return editor type, pages, top-level nodes, local variable collections, local styles, and local components. Log Code Connect discovery as not applicable because the repository contains no Figma Code Connect files for this embedded UI.

- [ ] **Step 3: Verify fonts**

List available fonts and resolve exact style strings for:

```text
Source Han Sans SC: Regular, Medium, Bold or the nearest available equivalents
Montserrat: Regular, Medium, SemiBold
```

If Source Han Sans SC is unavailable, use Noto Sans CJK SC and record the substitution on `06 Motion & Handoff`; do not silently use Inter.

- [ ] **Step 4: Create and name seven pages**

```text
00 Cover
01 User Flow
02 Foundations
03 Components
04 Screens
05 Display Modes
06 Motion & Handoff
```

- [ ] **Step 5: Validate file structure**

Return the seven page names and IDs. Expected: exactly seven named delivery pages, with no duplicated page names.

### Task 2: Build foundations and reusable components

**Files:**
- Modify: Figma page `02 Foundations`
- Modify: Figma page `03 Components`

**Interfaces:**
- Consumes: font map and page IDs from Task 1.
- Produces: local variables, text/effect styles, and reusable component IDs used by Tasks 3–6.

- [ ] **Step 1: Create scoped local variables**

Create `Bajji / Color` variables for `#05070C`, `#0B1522`, `#101A27`, `#54D7FF`, `#F4F8FF`, `#A8B4C7`, `#74E6A5`, `#FFC857`, and `#FF6B76`; create spacing variables `8`, `12`, `16`, `24`, `32`, `48`, `56`; create radius variables `12`, `20`, `24`, `28`, `32`, and `233`. Assign explicit fill, text, gap, dimension, or radius scopes.

- [ ] **Step 2: Create type and effect styles**

Create named text styles:

```text
Bajji/Title/24 Semibold
Bajji/Body/18 Regular
Bajji/Caption/14 Medium
Bajji/Code/56 Semibold
Bajji/Toast/16 Semibold
```

Create `Bajji/Overlay Shadow` with `0 12 32 #00000052` and `Bajji/Backdrop Blur` with 20 px background blur.

- [ ] **Step 3: Draw the safe-area specimen**

On `02 Foundations`, create a labeled 466×466 circular device frame with a centered 328×328 dashed safe-area square, 48×48 minimum target specimen, palette chips, type specimens, spacing scale, and the rule “圆周外缘仅放图片和非关键信息”.

- [ ] **Step 4: Create reusable components**

Create components and variants with these names:

```text
Bajji / Status Pill: neutral, success, warning, error
Bajji / Setting Row: default, selected, disabled
Bajji / Primary Button: default, pressed, loading, disabled
Bajji / List Item: default, selected
Bajji / Toast: mode, loading, offline, error
Bajji / Key Hint: A, B, A+B
Bajji / Hold Progress: 0, 50, 100
Bajji / Pairing Modal: waiting, success, failure
```

Expose text properties for every label and API value. Use Auto Layout and keep touchable component heights at 48 px or more.

- [ ] **Step 5: Validate foundations and components**

Use metadata to assert variable/style/component names and count. Screenshot `02 Foundations` and `03 Components`; verify no clipped Chinese text, no overlapping variants, and correct product fonts.

### Task 3: Compose startup and pairing screens

**Files:**
- Modify: Figma page `04 Screens`

**Interfaces:**
- Consumes: Task 2 component IDs and local variables/styles.
- Produces: screen IDs for startup, unpaired, pairing waiting, and pairing success states.

- [ ] **Step 1: Create the screen row wrapper**

Create a horizontal Auto Layout section named `Flow / Pairing`, with 96 px gaps and captions outside each 466×466 frame.

- [ ] **Step 2: Build startup check**

Create `StopWatch / Startup / Checking` with a dark background, centered Bajji mark, circular scan indicator, and “正在检查配对”. Keep all text in the safe area.

- [ ] **Step 3: Build unpaired guidance**

Create `StopWatch / Pairing / Unpaired` with the “未配对” status pill, phone/watch connection illustration, title “请先连接手机”, two concise steps, and “正在等待配对请求…” BLE wave state.

- [ ] **Step 4: Build the pairing-code modal**

Create `StopWatch / Pairing / Code` from the waiting modal variant over a 64% dark backdrop. Show title “在手机输入配对码”, code `123 456`, trust reminder, and “等待手机确认…”.

- [ ] **Step 5: Build pairing success**

Create `StopWatch / Pairing / Success` from the success modal variant with a green check and “配对成功”. Annotate the 800 ms transition to settings.

- [ ] **Step 6: Validate pairing screens**

Screenshot each of the four screens at original scale. Verify 466×466 dimensions, readable six-digit code, no controls outside the safe area, and visual continuity between modal states.

### Task 4: Compose settings and picker screens

**Files:**
- Modify: Figma page `04 Screens`

**Interfaces:**
- Consumes: Task 2 setting-row, list-item, button, and status components.
- Produces: settings, category-picker, subtype-picker, and initial-loading screen IDs.

- [ ] **Step 1: Build settings home**

Create `StopWatch / Settings / Default` with title “图片设置”, connection status, selected rows `表情包 / bq` and `二次元 / eciyuan`, button “保存并获取图片”, and local-persistence caption.

- [ ] **Step 2: Build the main-category picker**

Create `StopWatch / Settings / Category Picker` as a vertically scrollable full-screen list. Include all ten values from the spec, selected state for `bq`, a fixed title, and a 48×48 back target.

- [ ] **Step 3: Build the subtype picker**

Create `StopWatch / Settings / Type Picker · bq` containing `xiongmao`, `waiguoren`, `maomao`, `ikun`, and selected `eciyuan`. Add an annotation listing the corresponding `acg` and `furry` values and noting that other categories hide the row.

- [ ] **Step 4: Build first-load state**

Create `StopWatch / Image / First Load` with dark background, indeterminate ring, and “正在获取图片”; do not show a percentage.

- [ ] **Step 5: Validate settings flow**

Screenshot the four screens. Confirm selected values match `category=bq&type=eciyuan`, all rows are at least 56 px high, scrolling is enabled on the category list, and no unsupported type is shown.

### Task 5: Compose image modes, button feedback, and error states

**Files:**
- Modify: Figma page `04 Screens`
- Modify: Figma page `05 Display Modes`

**Interfaces:**
- Consumes: Task 2 toast, key-hint, and hold-progress components.
- Produces: image-viewing, mode-comparison, refresh, hold, cache, error, and animation screen IDs.

- [ ] **Step 1: Place one reusable sample image**

Use one royalty-safe or generated anime-style sample image across all mode comparisons. Store it once in the Figma file and reuse its image hash; do not introduce per-screen image downloads.

- [ ] **Step 2: Build three image modes**

Create:

```text
StopWatch / Image / Cover
StopWatch / Image / Contain Blur
StopWatch / Image / Tile
```

Cover uses proportional center crop. Contain Blur uses a blurred cover background and sharp contained foreground. Tile repeats the same image at its natural aspect ratio. Show the key hint only in an annotated variant, not permanently on the clean image frames.

- [ ] **Step 3: Build the display-mode comparison page**

On `05 Display Modes`, show the three modes side by side for a landscape, portrait, and square crop of the same source. Label scaling behavior and clipping rules beneath each row.

- [ ] **Step 4: Build transient button-feedback states**

Create `StopWatch / Image / Mode Toast` with “完整显示”, and `StopWatch / Image / Refreshing` with the old image retained behind “正在换一张…”. Annotate 1.2 second toast dismissal and prevention of concurrent refreshes.

- [ ] **Step 5: Build A+B hold states**

Create `StopWatch / Image / Hold 50` and `StopWatch / Image / Hold Complete` with centered progress ring and “继续按住以打开设置”. Annotate early-release cancellation and completion at exactly 1000 ms.

- [ ] **Step 6: Build cache and error states**

Create `StopWatch / Image / Offline Cache` retaining the current image with “离线 · 显示缓存”, plus `StopWatch / Image / No Cache Error` with “图片暂时不可用” and “按 B 重试”. Add annotations for no-result and unsupported-format copy.

- [ ] **Step 7: Build animated-image storyboard**

Create three `StopWatch / Image / Animated 01–03` frames using related image states and After Delay links. Show “动态图” only on the first frame, then hide it.

- [ ] **Step 8: Validate image states**

Screenshot each clean display mode, the 3×3 comparison, hold states, cache/error states, and animation storyboard. Verify the blur background fills the full circle, contained foreground is complete, clean views have no permanent toolbar, and text remains inside the safe area.

### Task 6: Add cover, user flow, motion, handoff, and prototype links

**Files:**
- Modify: Figma page `00 Cover`
- Modify: Figma page `01 User Flow`
- Modify: Figma page `06 Motion & Handoff`
- Modify: Figma page `04 Screens`

**Interfaces:**
- Consumes: all screen and component IDs from Tasks 2–5.
- Produces: finished navigation map, prototype start point, and developer handoff notes.

- [ ] **Step 1: Build the cover page**

Create title, one hero device mockup, project scope, 466/328 sizing summary, page index, and status “UI/UX ready for review”.

- [ ] **Step 2: Build the user-flow page**

Draw the startup → unpaired → pairing → settings → loading → image flow, with branches for cache and errors. Add a compact physical-key table for A, B, and A+B.

- [ ] **Step 3: Build motion and handoff notes**

Document 180 ms image fade, 800 ms pairing-success hold, 1.2 second toast, 1000 ms A+B threshold, early-release cancellation, static/animated formats, local persistence, and `category/type` constraints.

- [ ] **Step 4: Add prototype interactions**

Set `StopWatch / Startup / Checking` as the start point. Link pairing simulation, settings rows, save/load, KEY A mode cycle, KEY B refresh, and A+B hold simulation. Use clickable annotations outside the round frame to represent hardware keys without polluting the product UI.

- [ ] **Step 5: Validate navigation and handoff**

Return the prototype start node and every destination ID. Screenshot the three documentation pages and verify that all 18 required screen states are represented and named.

### Task 7: Final QA and delivery

**Files:**
- Inspect: all seven Figma pages
- Modify: only nodes that fail QA

**Interfaces:**
- Consumes: complete Figma file.
- Produces: final Figma URL/file key, screen inventory, and verified screenshots.

- [ ] **Step 1: Run structural QA**

Return all screen frames with name, width, height, font families, child count, and safe-area violations. Expected: every `StopWatch /` screen is 466×466 and uses only the approved product fonts.

- [ ] **Step 2: Run visual QA**

Capture original-scale screenshots for pairing code, settings, the three image modes, hold progress, offline cache, no-cache error, and the animated storyboard. Check text clipping, overlap, contrast, circular clipping, and image coverage.

- [ ] **Step 3: Fix only failed checks**

Apply targeted edits to failing nodes and repeat their metadata/screenshot checks; do not rebuild passing pages.

- [ ] **Step 4: Deliver**

Provide the Figma file link, list the seven pages and 18 core screens, and state any explicit font substitution. Confirm that the existing Xcode scheme modification was untouched.
