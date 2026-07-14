# BSL Look Spec — Ground Truth (extracted from BSL/ shaderpack defaults)

Companion to `BSL_TECHNIQUES.md`. That file catalogs techniques; THIS file pins the
concrete default values and pass structure of BSL Shaders v10 so SleakEngine's
re-implementation matches the real look. Extracted from `BSL/lib/settings.glsl`
(all defaults), `BSL/program/*.glsl` (pass bodies), `BSL/shaders.properties`
(profiles/enables). Values quoted are shipped defaults.

**Priority correction vs BSL_TECHNIQUES.md:** stock BSL ships with DOF, motion
blur, chromatic aberration, auto-exposure, color grading, POM/advanced
materials, refraction, water caustics, MCBL, and aurora ALL OFF. The default
BSL identity = filtered shadows + SSAO + light shafts + TAA + FXAA + bloom
(always on) + volumetric Perlin clouds + water waves/parallax/SSR + BSL
tonemap (exposure ×4) + vignette + warm blocklight + night desaturation +
waving plants + sun path rotation −40°. Implement the default-ON set first.

**Sky correction:** BSL's sky is NOT Rayleigh/Mie — it is a fitted analytic
exponential-gradient model with hand-tuned colors (see §3). Implement the
gradient model, not physical scattering.

---

## 1. Pass graph (execution order)

Render target map (from `program/final.glsl:22-35`):

| RT | Format | Contents |
|---|---|---|
| colortex0 | R11F_G11F_B10F | main HDR scene |
| colortex1 | RGB8 | translucent / VL / bloom atlas / final chain |
| colortex2 | RGBA16 | temporal: TAA history (.gba) + exposure/DOF/lens scalars (.r) |
| colortex3 | RGB8 | smoothness / sky-occlusion / entity mask |
| colortex4 | R8 | cloud distance / AO |
| colortex5 | RGB10_A2 | reflection image |
| colortex6 | RGBA16 | normals / refraction vector |
| colortex7 | RGBA16 | fresnel / dirty lens |
| colortex8/9 | RGB8/RGB16F | colored blocklight (MCBL, off by default) |

Order:
1. **shadow** — shadow map (2048 default)
2. **gbuffers_\*** opaque, forward-lit, TAA jitter injected in ALL gbuffer VS
3. **deferred** — SSAO → colortex4
4. **deferred1** — AO apply, fog, sky draw, volumetric clouds, sun/moon/stars, sun glare
5. **gbuffers_water/weather** — translucent forward
6. **composite** — refraction, underwater fog, light-shaft raymarch (→colortex1), MCBL
7. **composite1** — light-shaft 4-tap diagonal blur (`vl*=vl; vl*=vl; vl*=0.125`) + add
8. **composite2** — motion blur (OFF default)
9. **composite3** — DOF 60-tap disk (OFF default)
10. **composite4** — bloom downsample: 7-tile mip atlas
11. **composite5** — bloom apply → auto-exposure → grading → vignette → **BSL tonemap** → lens flare → gamma → sat/vibrance → film grain; writes TAA history
12. **composite6** — FXAA 3.11
13. **composite7** — TAA resolve
14. **final** — chromatic aberration + sharpen (0.125 auto when FXAA+TAA)

## 2. Headline defaults

- Shadows: res 2048, distance 256, `shadowMapBias = 1 - 25.6/dist`, 9-tap disk PCF,
  colored shadows ON, `sunPathRotation = -40`, sky-falloff fallback `smoothstep(0.7333, 1, skylight)`
- AO ON (strength 1.0, normal-reconstruction method, 4 dirs × 2 taps, radius 0.25)
- Light shafts ON (strength 1.0) — see §5
- Clouds: volumetric Perlin ON — see §6
- Bloom always on: radius 3, strength 1.0 (final mix 0.2), contrast 0
- TAA ON (8-frame jitter) + FXAA ON + auto sharpen 0.125
- Water: alpha 0.70, color (64,160,255)×0.35 sqrt-encoded, bump 1.0, speed 1.0 — §4
- Tonemap: BSL curve, exposure ×4 (`exp2(2.0 + EXPOSure=0)`), white curve 2.0, gamma 2.2
- Vignette ON 1.06; film grain ON (1/256)
- Blocklight (255,212,160)×0.85 sqrt → linear ≈ (0.72,0.50,0.28) warm amber
- Desaturation ON factor 1.5 (night/shadow chroma shift — defining night look)
- Waving: grass/crop/plant/leaf/vine/water/lava/fire/lantern all ON — §10
- OFF by default: DOF, motion blur, CA, auto-exposure, color grading, POM/adv.
  materials, refraction, caustics, MCBL, aurora, dirty lens, shader sun/moon discs

## 3. Sky model (fitted gradients — the BSL look's core)

`GetSkyColor()`: with `VoU = dot(dir, up)`, `VoL = dot(dir, sun)`:
- `gradientCurve = mix(1.5, 1.0, VoL)` (HORIZON_F=1.5, HORIZON_N=1.0)
- `baseGradient = exp(-(1 - pow(1 - max(VoU,0), gradientCurve)) / SKY_DENSITY_D)`, SKY_DENSITY_D = 0.35
- day exposure `exp2(timeBrightness*0.75 - 0.75)`; night `exp2(-3.5)`
- sun-horizon tint: `sunMix = pow((VoL*0.5+0.5) * clamp(1-VoU,0,1), 2 - sunSkyVisibility) * pow(1 - timeBrightness*0.6, 3)`
- `horizonMix = pow(1-abs(VoU), 2.5) * 0.125`
- night gradient `exp(-max(VoU,0)/0.65)`; weather `exp(-max(VoU,0)/1.50)`
- `sunSkyVisibility = clamp(dot(sunVec,upVec)*2 + 0.5, 0, 1)`

Color constants (sqrt-encoded as `(RGB/255)*I`, squared at use → linear):

| Name | RGB | Intensity |
|---|---|---|
| SKY / FOG base | (96,160,255) | 1.0 |
| lightDay | (196,220,255) | 1.40 |
| ambientDay | (120,172,255) | 0.60 |
| lightMorning = lightEvening | (255,160,80) | 1.20 |
| ambientMorning = ambientEvening | (255,204,144) | 0.35 |
| lightNight = ambientNight | (96,192,255) | 1.00, then ×0.3 × moonPhase[8]={1,.875,.75,.625,.5,.625,.75,.875} |
| weatherCol (rain) | (176,224,255) | 1.20 |

Time blending: `timeAngle = worldTime/24000`, `timeBrightness = max(sin(timeAngle·2π), 0)`,
`mefade = 1 - clamp(abs(timeAngle-0.5)*8 - 1.5, 0, 1)`, `dfade = 1 - pow(1-timeBrightness, 1.5)`;
`lightCol = mix(night, mix(mix(morning,evening,mefade), day, dfade), sunVisibility)`,
rain → luminance × weatherCol. Stars: 3-octave hash, threshold 0.8125.
Sun/moon intensity 1.50 (squared). SKY_GROUND 2 (below-horizon darkening).

## 4. Water

- Color sqrt = `(64,160,255)/255 * 0.35`, squared → linear; alpha 0.70; fog tint mult WATER_F = 1.20
- Height map: 2 value-noise octaves at scales /256 (slow) + /48 (fast), blend WATER_DETAIL 0.25, ×WATER_BUMP 1.0; `wind = time*0.5*WATER_SPEED`; slope skew `worldPos.xz += worldPos.y*0.2`
- Normals: 4-tap central difference, offset WATER_SHARPNESS 0.2; `normalStrength = 0.35*(1-fresnel)`, fresnel = `pow(clamp(1+dot(N,V),0,1), 8)`
- Parallax waves ON: 4 iterations, height `-1.25*h + 0.25`
- Reflection: SSR raytrace 30 steps + binary refine + sky fallback; translucent reflections ON
- Water fog: `range = 64/density`, `fog = 1 - exp(-2*len/range)`, color `waterColor.rgb*alpha*WATER_F²*(lightCol*eBS*shadowFade*0.9 + 0.1)`
- Refraction/caustics OFF by default

## 5. Volumetric light shafts (Robobo1221 style)

- 7 samples, exponential steps `dist = exp2(i + dither) - 0.95`, maxDist 128
- Time falloff: MORNING 0.25, DAY 0.10, NIGHT 0.50, WEATHER 8.0;
  `factor = mix(M, D, timeBrightness)`, `mix(N, factor, sunVisibility)`, `×mix(1, W, rain)*0.1`
- Per-sample shadow-map test (distorted coords), tinted by translucent/water
- Multiplier: `× lightCol * 0.25 × strength × (1 - rain*eBS*0.875) × shadowFade`
- Encode `pow(vl/32, 0.25)` → decode in composite1: `vl*=vl; vl*=vl; vl*=0.125`, 4 diagonal blur taps
- Sun glare adds `0.25 * lightCol * visibility` with same curve
- Dither: `frameCounter * 0.618` (golden ratio) under TAA

## 6. Volumetric clouds

- Perlin base (CLOUD_BASE 0), 32 raymarch samples (64 blocky), height 192 auto, thickness 5×scale 12
- Coverage: AMOUNT 10.0 (higher = fewer), DENSITY 4, DETAIL 1.0, STRETCH 1.0; density curve `n/sqrt(n²+0.5)`
- Wind: `vec2(t*0.0005, sin(t*0.001)*0.005)*0.667` × CLOUD_SPEED 1.0
- Lighting: `scattering = pow(halfVoL, 6)`; `mix(ambientCol*(0.3*skyVis+0.5), lightCol*(0.85+1.15*scattering), cloudLighting)` × brightness 1.0 × `(0.5 - 0.25*(1-skyVis)*(1-rain))`; opacity ×1.0, `cloud *= cloud`
- Distance fade 32/fogDensity → 240/fogDensity

## 7. Bloom

- Generate (composite4): 7 downsample tiles (lod 1–7) in one atlas; 6×6 separable
  Gaussian weights [0.03,0.15,0.32,0.32,0.15,0.03]; encode `pow(sum/32, 0.25)` + Bayer8 dither
- Apply (composite5): decode `b⁴·32` (soft HDR threshold);
  radius-3 combine `(b1·4.00 + b2·3.18 + b3·2.52 + b4·2.00 + b5·1.59 + b6·1.26 + b7)/15.55`;
  `color = mix(color, blur, 0.2*BLOOM_STRENGTH)`; `resScale = 1.25*min(720,H)/H`

## 8. TAA

- Jitter: 8-frame Chocapic13 sequence (.125,-.375)(-.125,.375)(.625,.125)(.375,-.625)(-.625,.625)(-.875,-.125)(.375,-.875)(.875,.875), `frameCounter % 8`, injected in gbuffer vertex shaders
- Reprojection: previous-frame camera offset; Catmull-Rom history sampling (c=0.7)
- Neighborhood: YCoCg AABB clip over 3×3
- Blend: `exp(-length(velocity))*0.2 + 0.7` → history weight 0.7–0.9 velocity-adaptive; off-screen reject → 4-tap blur fallback

## 9. Tonemap / grading / exposure

- BSL tonemap: `color *= exp2(2.0 + EXPOSURE)`; desat/resat pair (a=0.03, b=0.01 × WHITE_PATH 1.0);
  curve `c / pow(pow(c, WC)+1, 1/WC)` with WHITE_CURVE 2.0; `pow(c, mix(LOWER, UPPER, sqrt(c)))` LOWER=UPPER=1.0; clamp; gamma 1/2.2
- Auto-exposure (OFF): mip at `log2(H*0.7)`, `exposure = length(rgb)`, temporal `exp2(-dt*3.33)`, `color /= 2*e + 0.125`
- Saturation/vibrance 1.0/1.0 neutral: `c = c*SAT - grayLum*(SAT-1)` + midtone-protecting vibrance
- Vignette: `d = length(uv-0.5)`; `d *= d*0.3535 + 0.75`; `color *= 1 - d*1.06`
- Film grain: `+= (noise.b - 0.5)/256`
- Forward desaturation (ON, factor 1.5): shifts shadowed/low-skylight albedo toward night/weather chroma — defining BSL night look

## 10. Waving (vertex animation)

`time = frameTimeCounter * ANIMATION_SPEED(1.0)`; amplitudes × ANIMATION_STRENGTH(1.0);
`CalcMove(pos, density, speed, amp)` = 2D value-noise displacement:

| Type | density | speed | amp |
|---|---|---|---|
| grass | 0.35 | 1.0 | (0.25, 0.06) + proximity bend |
| crop | 0.35 | 1.0 | (0.15, 0.06) + bend |
| plant | 0.7 | 1.35 | (0.12, 0) + bend |
| tall plant | 0.35 | 1.15 | (0.15, 0.06) |
| leaves | 0.25 | 1.0 | (0.08, 0.08) |
| vine | 0.35 | 1.25 | (0.06, 0.06) |
| lilypad | dual-sine vertical ×0.0125 | | |
| lava | vertical height wave | | |
| fire | 2.0 | 3.0 | (0, 1) |
| lantern | pendulum rotation, rmult = π·0.016 | | |

## 11. Quality profiles (ladder)

| Profile | Changes |
|---|---|
| MINIMUM | no AO, no light shafts, no shadows/filter/color, shadow 512/128, no TAA |
| LOW | + shadows (1024/128) |
| MEDIUM | + AO + shadow filter (1536/192) |
| HIGH | + light shafts + colored shadows + TAA (2048/256) |
| ULTRA | 3072/512 |

Bloom / FXAA / clouds / water / tonemap are NOT profile-gated (always on).

## 12. Blocklight / lightmap

- Torch color sqrt = `(255,212,160)/255 * 0.85` → linear ≈ (0.72, 0.50, 0.28)
- Falloff: `newLightmap = pow(l,10)*1.6 + l*0.6`, `blockLighting = col * newLightmap²`
- Minlight: (128,128,128)×0.50 × `(1 - skylight²)`
- Emissive: intensity 4.0, curve 1.0
- Scene sum: `albedo * (mix(ambient*skylight, lightCol, shadow*NoL) * skylight² + blockLighting + emissive + minLight)`

## Supporting mechanics

- Shadow distortion: `distb*bias + (1-bias)`, z×0.2 (near-camera resolution boost)
- SSR: 30 steps, binary search refine, border fade `13.333*(1-cdist)`
- Motion blur (off): 5-tap along reproj velocity ×0.02
- DOF (off): 60-tap disk, CoC `abs(1/z - 1/focus)`, centerDepthSmooth default
- FXAA 3.11: 12-iteration edge search, subpixel 0.50
- Time uniforms: `timeAngle = worldTime/24000`, `timeBrightness = max(sin(timeAngle·2π),0)`, shadowFade ramps, framemod8 jitter index
