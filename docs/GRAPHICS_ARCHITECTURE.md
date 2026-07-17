# SleakEngine BSL Graphics Overhaul — Architecture

Design for implementing the BSL look (see `docs/BSL_LOOK_SPEC.md` for ground-truth
values, `BSL_TECHNIQUES.md` for the technique catalog) across SleakEngine and
SleakCraft. Engine stays game-independent: the engine exposes generic,
data-driven graphics features; SleakCraft supplies the BSL parameter values,
the time-of-day driver, and block-type hooks.

## Current state (audited 2026-07-15)

| Feature | Vulkan | OpenGL | DX11 | DX12 |
|---|---|---|---|---|
| Deferred (GBuffer+lighting) | ✅ | ✅ | ✗ | ✗ |
| SSAO | ✅ | ✅ | ✗ (dead shader) | ✗ (dead shader) |
| SSR | ✅ | ✗ | ✗ | ✗ |
| TAA | ⚠️ camera-only reprojection | ✗ | ✗ | ✗ |
| Bloom | ✅ | ✗ | ✗ | ✗ |
| Auto-exposure | ✗ | ✗ | ✗ | ✗ |
| Post chain / HDR target | ✅ hardcoded seq | ✗ (LDR, ACES inline) | tonemap pass only | ✗ |
| IBL | ✅ | ✅ | ✗ (dead shader) | ✗ |
| Shadows | ✅ 2048 | ✅ 4096(*) | ✅ 4096(*) | ✅ 4096(*) |
| Velocity buffer | ✗ | ✗ | ✗ | ✗ |
| Sky | static cubemap/panorama + fog-gradient hack | same | same | same |

(*) BUG: `LightManager` hardcodes 2048-texel snap/size math — GL/DX 4096 maps
get wrong texel math today. Fixed by config-driven `shadowMapResolution`.

Key architectural gaps: no RT pool / render-graph, pass order hardcoded in
`VulkanRenderer::EndRender`, feature bools spread across 3 layers with silent
no-ops on unimplemented backends, ImGui coupled into VK bloom-composite,
GraphicsConfig write-once + bypassed by UI, no time-of-day anywhere.

## Target architecture

### Layer 1 — Settings: `GraphicsConfig` v2 (engine, public)

`Sleak::GraphicsConfig` becomes the complete, sectioned, data-driven surface:

```
GraphicsConfig
├── shadows:    enabled, mapResolution, distance, frustumSize, casterDistance,
│               bias, strength, filterTaps, coloredShadows, distortion
├── ao:         enabled, radius, bias, power, strength
├── reflections: ssrEnabled, steps, refineSteps, roughFallback, skyFalloff
├── bloom:      enabled, strength, radius, contrast
├── aa:         taaEnabled, taaBlendMin/Max, fxaaEnabled, fxaaSubpixel, sharpen
├── sky:        proceduralEnabled, densityDay/Night/Weather, horizonNear/Far,
│               groundMode, colors{sky, lightDay/Morning/Evening/Night,
│               ambient*, weather}, sunDiscMode, starsEnabled
├── timeOfDay:  READ-ONLY inputs set per frame by the game:
│               timeAngle, timeBrightness, sunVisibility, shadowFade,
│               rainStrength, moonPhase   (engine consumes, never advances)
├── volumetrics: lightShaftEnabled, strength, samples, maxDist,
│               falloffMorning/Day/Night/Weather
├── clouds:     mode(off/2D/volumetric), base(perlin/worley/blocky), samples,
│               height, thickness, amount, density, speed, brightness, opacity
├── water:      color, alpha, bump, detail, sharpness, speed, parallax,
│               reflectionMode, fogDensity, fogTintMult, causticsEnabled
├── fog:        (existing distance/height fields fold in here)
├── tonemap:    operator(BSL/ACES), exposure, whiteCurve, lowerCurve, upperCurve,
│               autoExposure{enabled, radius, speed}
├── grading:    saturation, vibrance, colorGrading{...}, vignette{enabled,strength},
│               filmGrain, chromaticAberration
├── lighting:   blocklightColor, blocklightIntensity, minLight, emissiveIntensity,
│               desaturation{enabled, factor}, sssEnabled
├── waving:     enabled, speed, strength   (per-type amplitudes are GAME data)
├── dofMotion:  dof{enabled, mode, ...}, motionBlur{enabled, strength}
└── (existing:  culling, msaa)
```

Rules:
- **Single source of truth.** The settings UI reads/writes the config and calls
  `Application::ApplyGraphicsConfig()`; no more direct `app->SetX` scattering.
- **Dirty-section apply.** `ApplyGraphicsConfig` diffs sections; cheap uniform
  changes apply immediately, RT-affecting changes (shadow res, render scale)
  queue a `WaitGPUIdle` + resource rebuild.
- **Capability mask.** `Renderer::GetFeatureCaps()` bitmask replaces silent
  no-ops: UI greys out what the active backend can't do.
- **Persistence** (game side): `bin/graphics.json`, loaded before renderer init,
  saved on change. NOT WorldMeta (no save-format churn).
- **Presets:** engine keeps generic `Preset(quality)`; SleakCraft owns
  `BSLPreset(tier)` filling every field with BSL_LOOK_SPEC values using the
  BSL profile ladder (MINIMUM/LOW/MEDIUM/HIGH/ULTRA — shadow res
  512/1024/1536/2048/3072, dist 128/128/192/256/512, AO from MEDIUM,
  shafts+TAA from HIGH).

### Layer 2 — Frame infrastructure (engine)

1. **`PostProcessChain`** — generic module (per-backend impl, shared pass
   list): transient RT pool (HDR ping-pong at render scale), fullscreen-triangle
   helper, passes registered in fixed order mirroring BSL:
   `[VL-blur] → [MotionBlur] → [DOF] → [BloomGen] → [Composite: bloomApply +
   autoExposure + grading + vignette + tonemap + grain] → [FXAA] → [TAA] →
   [Final: CA + sharpen + UI]`.
   Each pass = shader + enable predicate reading GraphicsConfig. VK's existing
   bespoke TAA/SSR/bloom re-plumb into this; GL gets the chain net-new;
   DX11/DX12 adopt it when their deferred foundation lands (Phase 4).
2. **GL HDR path** — offscreen R11G11B10F/RGBA16F scene target (currently LDR
   default-FBO), extract ACES out of `lighting_pass_gl.frag` into the chain.
3. **Sky/ToD uniform block** — `SkyUBO`: sunVec, timeAngle, timeBrightness,
   sunVisibility, shadowFade, rainStrength, moonPhase, resolved sky colors
   (CPU-blended per BSL §3 time-mix by `LightManager` from config). Bound to
   sky pass, lighting pass, water, volumetrics.
4. **Procedural sky pass** — implement `SkyboxMode::Gradient` for real using
   BSL's gradient model (spec §3) + sun/moon discs + stars; replaces the static
   panorama when enabled. Fog colors derive from the SAME model so horizon
   blends seamlessly (kills the fog-as-sky hack drift).
5. **Velocity buffer** — R16G16 motion vectors from GBuffer pass (camera +
   per-object), consumed by TAA (fixes ghosting) and later motion blur.
6. **Shadow foundation fixes** — config-driven `shadowMapResolution` in all 4
   backends + `LightManager` (removes 2048/4096 mismatch); shadow distortion
   (spec: `distb*bias+(1-bias)`, z×0.2) as cheap alternative to cascades first;
   optional 9-tap disk PCF parity with BSL.
7. **Decouple ImGui** from VK bloom-composite → moves to Final pass.

### Layer 3 — Game side (SleakCraft)

- **`TimeOfDayController`**: advances world time (day length setting, pause),
  computes `timeAngle/timeBrightness/sunVisibility/shadowFade/moonPhase`,
  applies sun path rotation (−40° BSL), drives `DirectionalLight` direction +
  sun/moon intensity + feeds `GraphicsConfig.timeOfDay` per frame. Debug UI:
  time slider + speed.
- **`BSLPreset(tier)`**: all BSL_LOOK_SPEC defaults → GraphicsConfig.
- **Waving hooks**: game marks foliage block types (grass/plant/leaf/…) with a
  wave-type vertex attribute during chunk meshing; engine gbuffer/voxel vertex
  shaders apply `CalcMove`-style value-noise displacement with per-type
  (density, speed, amplitude) from a small game-supplied table (spec §10).
- **Settings UI v2**: quality-tier dropdown (BSL ladder) + per-section
  overrides, all through GraphicsConfig; persistence to `bin/graphics.json`.
- **Water**: keep water material/shader in Game assets, upgrade to BSL water
  model (spec §4): 2-octave noise height (/256, /48), central-diff normals,
  4-iter parallax, SSR reflections with sky fallback, water fog.
- **Blocklight**: warm (0.72, 0.50, 0.28) torch light + BSL falloff curve +
  night desaturation (factor 1.5) in lighting pass — values from game config.

## Phasing (tracked as tasks #3–#6)

- **Phase 1 (infra)**: GraphicsConfig v2 + apply pipeline + persistence;
  shadow-resolution unification (bug fix); SkyUBO/ToD plumbing + game
  TimeOfDayController; GL HDR target + PostProcessChain skeleton (GL, VK
  re-plumb); velocity buffer. *No new look yet — everything depends on this.*
  ⚠️ 2026-07-17: first GL HDR attempt (RGBA16F scene FBO + tonemap pass)
  REVERTED — nondeterministic ~1.4s/frame NVIDIA driver stall once the world
  loads (water-heavy forward blending into the FP16 target; glTextureBarrier
  only intermittently curative). Retry requires Nsight/apitrace attribution,
  single-variable experiments (RGBA8 vs 16F, blend off, barrier placement),
  and fixing the shadow-sampler/unit-0 UB warning first. VK path unaffected.
- **Phase 2 (BSL identity, GL+VK)**: procedural gradient sky + sun/moon/stars;
  bloom parity on GL + BSL strength/radius semantics; volumetric light shafts;
  TAA w/ velocity + FXAA + sharpen; BSL tonemap/exposure/vignette/grain;
  warm blocklight + desaturation. → *This is ~80% of the BSL look.*
- **Phase 3 (P1)**: volumetric Perlin clouds; BSL water upgrade; waving
  plants; leaf SSS; shadow filter/colored shadows/distortion; water fog;
  auto-exposure.
- **Phase 4 (polish + parity)**: off-by-default BSL extras (DOF, motion blur,
  CA, color grading, MCBL, aurora, lens flare, dirty lens…); quality-tier UI
  polish; **DX11/DX12: build deferred + post foundation, then port the full
  shader set** (largest single item — those backends currently have neither
  GBuffer nor HDR; runtime verification requires a Windows machine, Linux can
  only compile-validate HLSL via dxc/glslangValidator).

## Shader variant policy

Per engine convention every shader ships 4 variants (`.vert/.frag(+.spv)`,
`_gl`, `.hlsl`, `_dx12.hlsl`). Authoring order: **OpenGL reference → Vulkan in
the same PR (both runtime-verified on Linux) → DX11/DX12 in the Phase-4 parity
pass** (compile-validated immediately, runtime-verified on Windows). Never land
a feature without at least stubbing the DX variants so file inventory stays in
sync with `grep`-based tooling.

## Verification protocol

Every visual feature: build → launch `-r opengl` and `-r vulkan` on a fixed
world/seed → screenshot both at matched camera → compare against each other
and against BSL reference screenshots in `BSL/ExamplePicturesFromMinecraft/`.
Perf gate: track FPS via existing benchmark metrics; each phase must keep
Vulkan ≥60 FPS at rd=12 on the dev GPU at HIGH tier.
