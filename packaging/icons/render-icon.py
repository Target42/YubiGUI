# Zeichnet das YubiGUI-Icon. Quelle für PNG, ICO und SVG.
# Aufruf: python packaging/icons/render-icon.py
from __future__ import annotations

import struct
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parent
BG = (28, 40, 50, 255)        # #1C2832, dieselbe Farbe wie die Navigation
KEY = (61, 154, 102, 255)     # #3D9A66
METAL = (228, 235, 240, 255)  # #E4EBF0
HOLE = (244, 247, 245, 255)   # #F4F7F5
SLOT = (28, 40, 50, 255)

# Geometrie im 256er-Raster. Der Schlüssel füllt das Zeichen, damit er bei 16 px noch lesbar ist.
USB = (34, 104, 108, 152, 8)
SLOTS = ((46, 114, 74, 122), (46, 134, 74, 142))
BODY = (86, 68, 226, 188, 60)
HOLE_C = (166, 128)
HOLE_R = 30
HOLE_INNER = 15
HIGHLIGHT = (108, 80, 210, 108)


def draw(size: int) -> Image.Image:
    scale = size / 256
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    pen = ImageDraw.Draw(img)

    def box(spec):
        x0, y0, x1, y1 = spec[:4]
        return [x0 * scale, y0 * scale, x1 * scale - 1, y1 * scale - 1]

    pen.rounded_rectangle([0, 0, size - 1, size - 1], radius=56 * scale, fill=BG)
    pen.rounded_rectangle(box(USB), radius=USB[4] * scale, fill=METAL)
    if size >= 48:
        for slot in SLOTS:
            pen.rounded_rectangle(box(slot), radius=2 * scale, fill=SLOT)
    pen.rounded_rectangle(box(BODY), radius=BODY[4] * scale, fill=KEY)

    body_mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(body_mask).rounded_rectangle(box(BODY), radius=BODY[4] * scale, fill=255)
    shine = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(shine).rounded_rectangle(
        box(HIGHLIGHT), radius=14 * scale, fill=(255, 255, 255, 42)
    )
    shine.putalpha(ImageChops.multiply(shine.getchannel("A"), body_mask))
    img = Image.alpha_composite(img, shine)
    pen = ImageDraw.Draw(img)

    cx, cy = HOLE_C[0] * scale, HOLE_C[1] * scale
    outer, inner = HOLE_R * scale, HOLE_INNER * scale
    pen.ellipse([cx - outer, cy - outer, cx + outer, cy + outer], fill=HOLE)
    pen.ellipse([cx - inner, cy - inner, cx + inner, cy + inner], fill=BG)
    return img


def write_svg(path: Path) -> None:
    path.write_text(
        """<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <defs>
    <clipPath id="key">
      <rect x="86" y="68" width="140" height="120" rx="60"/>
    </clipPath>
  </defs>
  <rect width="256" height="256" rx="56" fill="#1C2832"/>
  <rect x="34" y="104" width="74" height="48" rx="8" fill="#E4EBF0"/>
  <rect x="46" y="114" width="28" height="8" rx="2" fill="#1C2832"/>
  <rect x="46" y="134" width="28" height="8" rx="2" fill="#1C2832"/>
  <g clip-path="url(#key)">
    <rect x="86" y="68" width="140" height="120" rx="60" fill="#3D9A66"/>
    <rect x="108" y="80" width="102" height="28" rx="14" fill="#ffffff" opacity="0.16"/>
  </g>
  <circle cx="166" cy="128" r="30" fill="#F4F7F5"/>
  <circle cx="166" cy="128" r="15" fill="#1C2832"/>
</svg>
""",
        encoding="utf-8",
        newline="\n",
    )


def _dib(image: Image.Image) -> bytes:
    image = image.convert("RGBA")
    width, height = image.size
    raw = image.tobytes("raw", "BGRA")
    stride = width * 4
    xor = b"".join(raw[y * stride:(y + 1) * stride] for y in range(height - 1, -1, -1))
    mask_stride = ((width + 31) // 32) * 4
    mask = bytearray(mask_stride * height)
    pixels = image.load()
    for y in range(height):
        src_y = height - 1 - y
        for x in range(width):
            if pixels[x, src_y][3] < 128:
                mask[y * mask_stride + (x // 8)] |= 0x80 >> (x % 8)
    header = struct.pack(
        "<IiiHHIIiiII",
        40,
        width,
        height * 2,
        1,
        32,
        0,
        len(xor),
        0,
        0,
        0,
        0,
    )
    return header + xor + bytes(mask)


def write_ico(path: Path, images: list[Image.Image]) -> None:
    blobs = [_dib(image) for image in images]
    offset = 6 + 16 * len(blobs)
    entries = b""
    for image, blob in zip(images, blobs):
        width, height = image.size
        entries += struct.pack(
            "<BBBBHHII",
            0 if width >= 256 else width,
            0 if height >= 256 else height,
            0,
            0,
            1,
            32,
            len(blob),
            offset,
        )
        offset += len(blob)
    with path.open("wb") as handle:
        handle.write(struct.pack("<HHH", 0, 1, len(blobs)))
        handle.write(entries)
        for blob in blobs:
            handle.write(blob)


def main() -> None:
    master = draw(1024)
    png_sizes = (16, 22, 24, 32, 48, 64, 128, 256, 512)
    sized = {size: master.resize((size, size), Image.Resampling.LANCZOS) for size in png_sizes}
    for size, image in sized.items():
        folder = ROOT / "hicolor" / f"{size}x{size}" / "apps"
        folder.mkdir(parents=True, exist_ok=True)
        image.save(folder / "yubigui.png", "PNG")
    sized[256].save(ROOT / "yubigui.png", "PNG")
    write_svg(ROOT / "yubigui.svg")
    scalable = ROOT / "hicolor" / "scalable" / "apps"
    scalable.mkdir(parents=True, exist_ok=True)
    (scalable / "yubigui.svg").write_bytes((ROOT / "yubigui.svg").read_bytes())
    write_ico(ROOT / "yubigui.ico", [sized[size] for size in (16, 24, 32, 48, 64, 128, 256)])
    print("icon-ok")


if __name__ == "__main__":
    main()
