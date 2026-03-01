#!/usr/bin/env python3
"""Generate a 2048x512 block texture atlas (4 tiles, 512px each)."""

from PIL import Image, ImageFilter
import numpy as np
import os

TILE = 512
SEED = 42
rng = np.random.default_rng(SEED)


# ── Noise helpers ──────────────────────────────────────────────

def _fade(t):
    return t * t * t * (t * (t * 6 - 15) + 10)


def value_noise(w, h, scale, seed=0):
    """Tileable value noise via bilinear interpolation of a random grid."""
    gs = max(2, int(np.ceil(w / scale)) + 1)
    local_rng = np.random.default_rng(seed)
    grid = local_rng.random((gs + 1, gs + 1)).astype(np.float32)
    # Make tileable
    grid[-1, :] = grid[0, :]
    grid[:, -1] = grid[:, 0]

    xs = np.linspace(0, gs - 1, w, endpoint=False).astype(np.float32)
    ys = np.linspace(0, gs - 1, h, endpoint=False).astype(np.float32)
    xi = np.floor(xs).astype(int)
    yi = np.floor(ys).astype(int)
    xf = _fade(xs - xi)
    yf = _fade(ys - yi)

    xi1 = xi + 1
    yi1 = yi + 1

    # 2D bilinear
    top = grid[yi[:, None], xi[None, :]] * (1 - xf[None, :]) + \
          grid[yi[:, None], xi1[None, :]] * xf[None, :]
    bot = grid[yi1[:, None], xi[None, :]] * (1 - xf[None, :]) + \
          grid[yi1[:, None], xi1[None, :]] * xf[None, :]
    return top * (1 - yf[:, None]) + bot * yf[:, None]


def fbm(w, h, octaves=5, lacunarity=2.0, gain=0.5, base_scale=64, seed=0):
    """Fractal Brownian Motion – layered value noise."""
    result = np.zeros((h, w), dtype=np.float32)
    amp = 1.0
    scale = base_scale
    total_amp = 0.0
    for i in range(octaves):
        result += amp * value_noise(w, h, scale, seed=seed + i * 137)
        total_amp += amp
        amp *= gain
        scale /= lacunarity
    return result / total_amp


# ── Tile generators ────────────────────────────────────────────

def create_grass_top():
    """Lush grass top – greens with subtle color variation."""
    n1 = fbm(TILE, TILE, octaves=5, base_scale=80, seed=10)
    n2 = fbm(TILE, TILE, octaves=3, base_scale=40, seed=20)
    n3 = fbm(TILE, TILE, octaves=6, base_scale=16, seed=30)  # fine detail

    r = (75 + n1 * 30 + n3 * 12).clip(0, 255).astype(np.uint8)
    g = (140 + n1 * 40 + n2 * 20 - n3 * 8).clip(0, 255).astype(np.uint8)
    b = (35 + n2 * 15 + n3 * 8).clip(0, 255).astype(np.uint8)
    a = np.full((TILE, TILE), 255, dtype=np.uint8)

    img = Image.fromarray(np.stack([r, g, b, a], axis=-1), 'RGBA')
    # Slight blur to soften pixel noise
    img = img.filter(ImageFilter.GaussianBlur(radius=0.5))
    return img


def create_dirt():
    """Dirt – warm browns with scattered darker spots."""
    n1 = fbm(TILE, TILE, octaves=5, base_scale=64, seed=50)
    n2 = fbm(TILE, TILE, octaves=4, base_scale=24, seed=60)
    n3 = fbm(TILE, TILE, octaves=6, base_scale=10, seed=70)  # granular

    r = (130 + n1 * 28 + n3 * 14 - n2 * 10).clip(0, 255).astype(np.uint8)
    g = (92 + n1 * 20 + n3 * 10 - n2 * 8).clip(0, 255).astype(np.uint8)
    b = (62 + n1 * 14 + n3 * 8).clip(0, 255).astype(np.uint8)
    a = np.full((TILE, TILE), 255, dtype=np.uint8)

    img = Image.fromarray(np.stack([r, g, b, a], axis=-1), 'RGBA')
    img = img.filter(ImageFilter.GaussianBlur(radius=0.4))
    return img


def create_grass_side():
    """Grass side – jagged grass-to-dirt transition."""
    dirt = np.array(create_dirt().convert('RGB')).astype(np.float32)
    grass = np.array(create_grass_top().convert('RGB')).astype(np.float32)

    # Transition boundary: noisy horizontal line near top ~20-30% of tile
    boundary_noise = fbm(TILE, 1, octaves=4, base_scale=64, seed=80)[0]
    # Boundary row per column: roughly 20-35% from top
    boundary_row = (TILE * 0.20 + boundary_noise * TILE * 0.15).astype(int)
    boundary_row = boundary_row.clip(int(TILE * 0.10), int(TILE * 0.40))

    ys = np.arange(TILE)[:, None]
    xs = np.arange(TILE)[None, :]
    border = boundary_row[None, :]

    # Smooth blend over ~6 pixels
    blend_width = max(4, TILE // 80)
    alpha = ((ys - border).astype(np.float32) / blend_width).clip(0, 1)

    result = grass * (1 - alpha[:, :, None]) + dirt * alpha[:, :, None]
    result = result.clip(0, 255).astype(np.uint8)
    a = np.full((TILE, TILE), 255, dtype=np.uint8)
    img = Image.fromarray(np.concatenate(
        [result, a[:, :, None]], axis=-1), 'RGBA')
    return img


def create_stone():
    """Stone – grays with subtle cracks and color variation."""
    n1 = fbm(TILE, TILE, octaves=5, base_scale=80, seed=90)
    n2 = fbm(TILE, TILE, octaves=4, base_scale=32, seed=100)
    n3 = fbm(TILE, TILE, octaves=6, base_scale=12, seed=110)

    # Base gray
    base = 125 + n1 * 35 - n2 * 15 + n3 * 12

    # Simulate subtle cracks: sharp lines from high-frequency noise
    crack_noise = fbm(TILE, TILE, octaves=3, base_scale=8, seed=120)
    cracks = (crack_noise < 0.30).astype(np.float32) * 18  # darken at "cracks"

    val = (base - cracks).clip(0, 255).astype(np.uint8)
    # Slight warm/cool variation
    r = np.clip(val.astype(np.int16) + 2, 0, 255).astype(np.uint8)
    g = val
    b = np.clip(val.astype(np.int16) - 1, 0, 255).astype(np.uint8)
    a = np.full((TILE, TILE), 255, dtype=np.uint8)

    img = Image.fromarray(np.stack([r, g, b, a], axis=-1), 'RGBA')
    img = img.filter(ImageFilter.GaussianBlur(radius=0.3))
    return img


# ── Assemble atlas ─────────────────────────────────────────────

tiles = [create_grass_top(), create_grass_side(), create_dirt(), create_stone()]
atlas = Image.new('RGBA', (TILE * len(tiles), TILE))
for i, tile in enumerate(tiles):
    atlas.paste(tile, (i * TILE, 0))

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'block_atlas.png')
atlas.save(out, optimize=True)
print(f"Generated {out} ({atlas.width}x{atlas.height})")
