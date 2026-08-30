# BSL Shader Pack — Rendering Techniques Catalog

Technique inventory extracted from the BSL shader pack (located in `BSL/`) for the
purpose of porting equivalents into SleakEngine's deferred pipeline. This is a
**description of techniques**, not a copy of BSL's implementation. Every feature
listed will be authored from scratch against the engine's own shading model.

## Engine baseline (what we already have)

- Deferred renderer, PBR Cook-Torrance (GGX + Smith + Schlick)
- GBuffer: albedo+AO (RGBA8), normal+roughness (RGBA16F), metallic+emissive (RGBA8), worldPos (RGBA32F)
- SSAO, IBL (irradiance + prefiltered specular + BRDF LUT), PCSS soft shadows
- Distance fog + height fog
- HDR scene color (R16G16B16A16_FLOAT on DX), ACES tonemap
- Sky: static cubemap / panorama / gradient (no dynamic time-of-day)

## Priority legend

| Tag | Meaning |
|---|---|
| **P0** | Core BSL identity — essential for the look |
| **P1** | High visual impact, large payoff |
| **P2** | Polish; visible on close inspection |
| **P3** | Niche or stylistic; optional |
| ✅ | Already implemented in SleakEngine |

---

## 1. Sky & atmosphere

| # | Technique | Pri | Description |
|---|---|---|---|
| 1 | Atmospheric sky (Rayleigh + Mie) | P0 | Physically-motivated scattering driving sky gradient from sun position. Day/night exposure curves. |
| 2 | Time-of-day system | P0 | Sun/moon angular path from a time value; drives lighting direction, sky colors, fog tint. |
| 3 | Sun and moon discs | P1 | Procedural or SDF-based disk with fresnel-tinted edge. Optional square sun disk. |
| 4 | Horizon 4-parameter gradient falloff | P1 | Separate day/night horizon gradients with exponential falloff. |
| 5 | Underground sky fade | P2 | Sky dims/fades when camera is below terrain. |
| 6 | Biome-aware fog density | P1 | Separate densities per dimension (overworld/nether/end); weather modulation. |
| 7 | Distance fog | ✅ | Already implemented. |
| 8 | Height fog | ✅ | Already implemented. |
| 9 | Water fog / underwater scattering | P1 | Density-based absorption when camera is underwater; color tint. |

## 2. Volumetrics

| # | Technique | Pri | Description |
|---|---|---|---|
| 10 | Volumetric light shafts (god rays) | P0 | Screen-space ray-march through shadow map; phase-dependent brightness (morning/day/night/weather). |
| 11 | Volumetric clouds (Perlin / Worley / Blocky noise) | P1 | 3D noise sampling; coverage function; vertical detail layering; animated scrolling. |
| 12 | Cloud rain overlay | P2 | Rain-dependent cloud density and coverage modifier. |

## 3. Lighting

| # | Technique | Pri | Description |
|---|---|---|---|
| 13 | Cook-Torrance GGX BRDF | ✅ | Already in `lighting_pass`. |
| 14 | Image-based lighting (diffuse + specular) | ✅ | Already implemented. |
| 15 | Multi-colored blocklight (MCBL) | P2 | 3D voxel texture propagating colored light from emissive blocks (torches, glowstone, ore glow). Striped temporal reads. |
| 16 | Dynamic hand light | P3 | Point light attached to held item with distance falloff. |
| 17 | Directional lightmap | P1 | Per-fragment directional integration blended with vanilla lightmap to break flat lighting. |
| 18 | Hardcoded emission detection | P2 | HSV-threshold auto-detection of emissive pixels in albedo (ores, torches without separate emissive). |
| 19 | Forward lighting for transparent geometry | P1 | Per-light accumulation for forward pass (deferred-forward hybrid). |
| 20 | Subsurface scattering (basic) | P1 | Thickness-weighted light transmission through leaves/plants; wrap lighting term. |

## 4. Shadows

| # | Technique | Pri | Description |
|---|---|---|---|
| 21 | Cascaded shadow maps | P1 | Multiple cascades for near/far shadow resolution (engine currently single-cascade). |
| 22 | PCSS (percentage-closer soft shadows) | ✅ | Already implemented. |
| 23 | Shadow distortion | P1 | Non-linear shadow map projection to increase near-camera resolution. |
| 24 | Colored shadows (stained glass) | P2 | Sample albedo from shadow map to tint shadow where light passes through colored transparent geometry. |
| 25 | LOD shadows | P2 | Low-resolution shadow cascade for very distant terrain. |
| 26 | Poisson-disk PCF | ✅ | Part of PCSS kernel. |

## 5. Water

| # | Technique | Pri | Description |
|---|---|---|---|
| 27 | Gerstner waves | ✅ | Already in `water_shader`. |
| 28 | Water normal maps with parallax | P1 | Scrolling animated normal maps; sharpness control; bump strength. |
| 29 | Water caustics | P1 | Animated texture sampling projected onto underwater surfaces; strength-controllable. |
| 30 | Water reflections (SSR on water plane) | P1 | Screen-space ray-traced reflections for the water surface. |
| 31 | Water refraction | P1 | Distort underwater scene using surface normal; Fresnel-blended with reflection. |
| 32 | Water shadow coloring | P2 | Dim and tint shadows passing through water volume. |
| 33 | Rain puddles | P2 | Fresnel-biased wet surface overlay near y-top-facing normals during rain. |

## 6. Reflections

| # | Technique | Pri | Description |
|---|---|---|---|
| 34 | Screen-space ray-traced reflections (SSR) | P0 | Ray-march depth buffer to find reflected color; error-multiplier LOD fallback. |
| 35 | Complex Fresnel (spherical Gaussian) | P1 | More accurate than Schlick for rough surfaces. |
| 36 | Simple Fresnel (Schlick) | ✅ | Already in `lighting_pass`. |
| 37 | Rough reflections | P1 | Perturbed normal sampling weighted by roughness. |

## 7. Surface & materials

| # | Technique | Pri | Description |
|---|---|---|---|
| 38 | Parallax occlusion mapping (POM) | P1 | Height-based ray-marching for depth illusion on flat geometry. Requires height maps in atlas. |
| 39 | Parallax self-shadowing | P2 | Shadow sampling along height map in light direction. Requires POM. |
| 40 | Normal map processing | P1 | Tangent-space decoding with dampening for plants. Requires normal maps in atlas. |
| 41 | GGX area-light approximation (Decima) | P1 | Larger-than-point sun highlights; more realistic specular shape. |
| 42 | Directional lightmap | P1 | See #17. |

## 8. Ambient occlusion

| # | Technique | Pri | Description |
|---|---|---|---|
| 43 | Screen-space AO (basic) | ✅ | Already implemented. |
| 44 | HBAO (horizon-based AO) | P2 | Higher-quality AO using ray-horizon approximation. Upgrade from SSAO. |
| 45 | Depth-derivative normal reconstruction | P2 | Reconstruct surface normal from depth gradient when GBuffer normal absent. |

## 9. Anti-aliasing

| # | Technique | Pri | Description |
|---|---|---|---|
| 46 | FXAA | P2 | Fast approximate AA; luma-edge detection; cheap. |
| 47 | TAA (temporal anti-aliasing) | P0 | Jittered projection + history reprojection + neighborhood clamp. Central to BSL's "smooth" look. |

## 10. Depth of field

| # | Technique | Pri | Description |
|---|---|---|---|
| 48 | DOF — circular aperture (60 samples) | P2 | Bokeh DOF with multiple focus modes (auto-track / fixed / crosshair). |

## 11. Motion blur

| # | Technique | Pri | Description |
|---|---|---|---|
| 49 | Per-pixel camera motion blur | P2 | Velocity reconstruction from depth + camera matrix; per-pixel sampling along the vector. |

## 12. Bloom

| # | Technique | Pri | Description |
|---|---|---|---|
| 50 | HDR bloom | P0 | Threshold + progressive downsample/upsample chain with tent filter. |
| 51 | Bloom strength / radius / contrast controls | P1 | Artist-tunable. |

## 13. Lens effects

| # | Technique | Pri | Description |
|---|---|---|---|
| 52 | Chromatic aberration | P2 | Per-channel wavelength distortion, edge-weighted. |
| 53 | Lens flare (multi-element) | P3 | Base circles, anamorphic streaks, rainbow bands, ring lens, point overlaps. |
| 54 | Dirty lens LUT overlay | P3 | Textured overlay mixed with bloom intensity. |

## 14. Tone-mapping & color

| # | Technique | Pri | Description |
|---|---|---|---|
| 55 | ACES tonemap | ✅ | Already implemented. BSL uses Reinhardt-variant — can switch or support both. |
| 56 | Auto-exposure | P1 | Mipmap-based scene luminance sampling with adaptation speed. |
| 57 | Per-channel color curves | P1 | R/G/B master and lower/upper/white-point curves for cinematic grade. |
| 58 | Per-time-of-day color grading | P1 | Separate morning/day/evening/night LUTs or grading curves. |
| 59 | White-balance curve | P2 | Temperature/tint adjustment path. |
| 60 | Saturation & vibrance | P1 | Full-spectrum and selective channel control. |
| 61 | Biome-based color grading | P2 | Desert/mesa/swamp/jungle/nether/end grade transitions. |

## 15. Vertex animation (waving)

| # | Technique | Pri | Description |
|---|---|---|---|
| 62 | Waving plants (grass/leaves/crops) | P1 | Perlin-noise-driven vertex displacement; per-type speed/strength. |
| 63 | Waving water surface | ✅ | Already in `water_shader`. |
| 64 | Lava surface waves | P2 | Height-gated sine waves. |
| 65 | Lily pad oscillation | P3 | Dual-frequency sine. |
| 66 | Lantern rotation | P3 | Time-seeded rotation matrix stack. |
| 67 | Grass bending near player | P2 | Camera-proximity displacement. |

## 16. Post-process filters

| # | Technique | Pri | Description |
|---|---|---|---|
| 68 | Underwater distortion | P2 | Wave-based coordinate warp when camera submerged. |
| 69 | Outline edge detection | P3 | Depth-discontinuity edge highlight. |
| 70 | Toon lightmap mode | P3 | Quantized light response (stylized). |
| 71 | Retro pixel filter | P3 | Depth-quantized color posterization. |
| 72 | World curvature | P3 | Horizon pull-down for long-view aesthetics. |

## 17. Entity / extras

| # | Technique | Pri | Description |
|---|---|---|---|
| 73 | Entity flash (damage / crit) | P3 | Tint/flash on hit — needs a game-side hook. |
| 74 | Sun disk square-SDF shape option | P3 | Stylistic square-sun variant. |

---

## 18. Quality tiers

BSL exposes 5 profiles — MINIMUM / LOW / MEDIUM / HIGH / ULTRA — with cascading
feature toggles. Recommend matching this pattern in SleakCraft: expose an enum
in the MainScene UI panel and gate each effect by tier.

---

## 19. Proposed implementation phases

**Phase 1 — Pipeline infrastructure (no visible change yet, but everything depends on it)**
- Post-process chain refactor with ping-pong HDR buffers
- Time-of-day uniform / sun-direction system
- Velocity buffer for TAA/motion blur

**Phase 2 — BSL identity features (P0, delivers ~80% of the look)**
- Atmospheric sky (#1, #2, #3, #4)
- Bloom (#50, #51)
- Volumetric light shafts (#10)
- TAA (#47)
- SSR (#34)
- Auto-exposure + color grading (#56, #57, #58, #60)

**Phase 3 — High-impact additions (P1)**
- Volumetric clouds (#11)
- Water caustics + SSR integration (#29, #30, #31)
- Waving plants (#62)
- Subsurface scattering for leaves (#20)
- Directional lightmap (#17)
- Cascaded shadow maps + shadow distortion (#21, #23)
- Parallax occlusion mapping (#38, #40) — requires height/normal maps in atlas
- Water fog (#9)

**Phase 4 — Polish (P2/P3)**
- FXAA, HBAO, DOF, motion blur
- Chromatic aberration, lens flare, dirty lens
- Colored shadows, rain puddles, underwater distortion
- Lava/lantern/lily-pad waving
- Outline, toon, retro pixel, world curvature
- Biome-based grading

Each phase depends structurally on the previous one — Phase 1 must land before
anything else because the engine currently has no post-process chain beyond a
single tonemap pass.

---

## 20. Multi-backend cost reminder

Every shader written needs four variants per SleakEngine's convention:

- Vulkan: `name.vert` + `name.frag` + compiled `.spv`
- OpenGL: `name_gl.vert` + `name_gl.frag`
- DirectX 11: `name.hlsl`
- DirectX 12: `name_dx12.hlsl`

Implementation approach: author OpenGL first (reference backend), then port to
the other three in a follow-up. This matches how SSAO and IBL shipped.
