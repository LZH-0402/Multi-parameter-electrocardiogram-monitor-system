# 血氧监护 VisualTFT 页面与素材 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 生成 800×480 血氧监护多页面预览、按钮按下/弹起素材、VisualTFT 布局规格与中文操作文档。

**Architecture:** 使用一个可点击的纯 HTML 预览作为唯一坐标事实；使用 SVG 生成可编辑素材，再用本地 PNG 转换工具导出 VisualTFT 可导入图片；使用 Markdown 记录页面、控件、ID、坐标、按钮属性、切换测试和人工核对项。

**Tech Stack:** HTML/CSS/JavaScript、SVG、PowerShell/Python（本地素材导出）、Markdown。

**Spec:** `docs/superpowers/specs/2026-09-21-spo2-hmi-design.md`

## Global Constraints

- 画布必须为 800×480，页面 ID 从 0 开始，控件 ID 在页面内从 1 开始。
- 保持深蓝背景、青色 SpO₂、绿色 PR、橙色提示、红色报警的视觉语义。
- 所有触摸热区至少 48×48px，四周安全边距至少 16px，正文至少 16px。
- PNG 是 VisualTFT 直接导入格式，SVG 作为可编辑源文件。
- 不伪造或声称生成 VisualTFT 私有 `.vptf`/`.tft` 工程文件。
- 页面切换优先用 VisualTFT 对内指令；暂停/恢复、清空报警等需要 MCU 确认的动作通过对外通知。

### Task 1: Create deterministic SVG/PNG UI assets

**Files:**
- Create: `visualtft_spo2/assets/svg/*.svg`
- Create: `visualtft_spo2/assets/png/*.png`
- Create: `visualtft_spo2/assets/README.md`

**Interfaces:**
- Produces button assets named `btn_<name>_up/down.svg/png` and shared icons/background pieces referenced by the HTML preview.

- [ ] Step 1: Create a PowerShell generator that writes SVG assets with exact 800×480-aligned button sizes and text.
- [ ] Step 2: Generate four bottom navigation pairs plus back/confirm/cancel pairs using the palette from the spec.
- [ ] Step 3: Convert each SVG to PNG using an installed local converter; if unavailable, use a deterministic Python rasterizer and record the method.
- [ ] Step 4: Check every output image exists, has matching up/down dimensions, and has no zero-byte files.

### Task 2: Build clickable 800×480 HTML preview

**Files:**
- Create: `visualtft_spo2/preview/monitor-ui.html`
- Create: `visualtft_spo2/preview/README.md`

**Interfaces:**
- Produces clickable `showScreen(screenId)` behavior for screens 0–5, `buttonState` pressed feedback, and a visible screen ID label for verification.

- [ ] Step 1: Implement a screen renderer with fixed 800×480 CSS pixels and a palette matching the spec.
- [ ] Step 2: Add clickable navigation from screen 0 to screens 1–5 and return paths to screen 0.
- [ ] Step 3: Add representative trend chart, alarm list, settings rows, pause confirmation, and probe/alarm detail content.
- [ ] Step 4: Add a coordinate/debug toggle that shows screen ID and button names without changing layout geometry.
- [ ] Step 5: Validate that all navigation buttons work by inspecting DOM event targets and running a headless smoke script if a browser runtime is available.

### Task 3: Write VisualTFT layout and operation guide

**Files:**
- Create: `visualtft_spo2/VisualTFT_按钮与页面切换指南.md`
- Create: `visualtft_spo2/layout/页面与控件规格表.md`
- Create: `visualtft_spo2/layout/页面切换图.md`

**Interfaces:**
- Consumes screen geometry from `monitor-ui.html` and asset names from Task 1.
- Produces tables that match the preview exactly, VisualTFT step sequence, test checklist, and manual verification list.

- [ ] Step 1: Record screen IDs, control IDs, names, types, coordinates, sizes, images, and actions for all six screens.
- [ ] Step 2: Document VisualTFT workflow: create screens, import PNGs, place controls, set up/up-down images, configure internal page switch, configure external notification, download and virtual-screen testing.
- [ ] Step 3: Include MCU notification payload placeholders as symbolic events only; mark device-specific bytes/LUA signatures for manual verification.
- [ ] Step 4: Add conflict checks for overlap, bounds, touch sizes, and spacing.
- [ ] Step 5: Add troubleshooting and acceptance checklist covering invalid values, stale values, back navigation, and alarm states.

### Task 4: Validate and package deliverables

**Files:**
- Modify: `visualtft_spo2/preview/README.md`
- Modify: `visualtft_spo2/assets/README.md`

**Interfaces:**
- Produces a final file index with absolute paths and validation results.

- [ ] Step 1: Use a script to parse preview screen IDs and compare them with layout tables.
- [ ] Step 2: Verify SVG XML well-formedness and PNG dimensions.
- [ ] Step 3: Verify all referenced asset filenames exist.
- [ ] Step 4: Render the HTML preview to a screenshot if a local browser is available and inspect the result.
- [ ] Step 5: Write final file index and unresolved manual verification items.
