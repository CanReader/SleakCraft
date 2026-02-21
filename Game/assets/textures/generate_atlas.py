#!/usr/bin/env python3
from PIL import Image
import random
import os

TILE = 16
random.seed(42)

def noise(base, variance):
    return max(0, min(255, base + random.randint(-variance, variance)))

def create_grass_top():
    img = Image.new('RGBA', (TILE, TILE))
    for y in range(TILE):
        for x in range(TILE):
            r = noise(89, 15)
            g = noise(150, 25)
            b = noise(40, 10)
            img.putpixel((x, y), (r, g, b, 255))
    return img

def create_grass_side():
    img = Image.new('RGBA', (TILE, TILE))
    for y in range(TILE):
        for x in range(TILE):
            if y < 4:
                r = noise(89, 15)
                g = noise(150, 25)
                b = noise(40, 10)
            else:
                r = noise(134, 12)
                g = noise(96, 10)
                b = noise(67, 8)
            img.putpixel((x, y), (r, g, b, 255))
    return img

def create_dirt():
    img = Image.new('RGBA', (TILE, TILE))
    for y in range(TILE):
        for x in range(TILE):
            r = noise(134, 15)
            g = noise(96, 12)
            b = noise(67, 10)
            img.putpixel((x, y), (r, g, b, 255))
    return img

def create_stone():
    img = Image.new('RGBA', (TILE, TILE))
    for y in range(TILE):
        for x in range(TILE):
            base = noise(128, 20)
            img.putpixel((x, y), (base, base, base, 255))
    return img

atlas = Image.new('RGBA', (TILE * 4, TILE))
tiles = [create_grass_top(), create_grass_side(), create_dirt(), create_stone()]
for i, tile in enumerate(tiles):
    atlas.paste(tile, (i * TILE, 0))

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'block_atlas.png')
atlas.save(out)
print(f"Generated {out} ({atlas.width}x{atlas.height})")
