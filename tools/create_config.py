#!/usr/bin/env python3
import duckduckgo_search
from argparse import ArgumentParser, RawDescriptionHelpFormatter
from io import BytesIO
import re
import textwrap
import struct
from PIL import Image, ImagePalette
import PIL
import os
import requests

if __name__ == "__main__":
    parser = ArgumentParser(
        formatter_class=RawDescriptionHelpFormatter,
        epilog=textwrap.dedent(f"""
    Example usage:

    If you're using this on KS1.3 with e.g. JST to launch WHDLoad slaves:
        {__file__} --folder ~/Download/Games --output ALauncher --command-template "cd {{reldir}}; c:jst {{basename}}"

    Or with WHDLoad on KS 2+:
        {__file__} --folder ~/Download/Games --output ALauncher --command-template "cd {{reldir}}; c:whdload {{basename}}"
    """))
    parser.add_argument("--folder", type=str, required=True)
    parser.add_argument("--command-template", type=str, required=True)
    parser.add_argument("--output", type=str, required=True)
    args = parser.parse_args()

    # recursively find all .slave files under the folder
    slave_files = []
    for root, dirs, files in os.walk(args.folder):
        for file in files:
            if file.lower().endswith(".slave"):
                slave_files.append(os.path.join(root, file))

    pics = os.path.join(args.output, "acelauncher.pics")
    os.makedirs(pics, exist_ok=True)

    def ppname(name):
        return re.sub("(NTSC|PAL|CDTV|1MB|2MB| De)", "", re.sub(r"([a-z0-9&\+])([A-Z0-9&\+])", r"\1 \2", name)).strip()

    with open(os.path.join(args.output, "acelauncher.config"), "w") as f:
        for slave in slave_files:
            basename = os.path.basename(slave)
            name, _ = os.path.splitext(basename)
            relpath = os.path.relpath(slave, args.folder)
            name = ppname(name)
            f.write(f"{name}\n")
            f.write(args.command_template.format(relslave=relpath, reldir=os.path.dirname(relpath), basename=basename) + "\n")
            f.write(f"sys:acelauncher.pics/{name}.iff\n")

    for slave in slave_files:
        basename = os.path.basename(slave)
        name, _ = os.path.splitext(basename)
        name = ppname(name)
        print(name)
        ddgs = duckduckgo_search.DDGS(proxy=os.environ.get("https_proxy"))

        if not os.path.exists(os.path.join(pics, f"{name}.png")):
            imgs = ddgs.images(keywords=f"{name} Amiga Gameplay Screenshot Ingame lemonamiga Hall of Light", size="Medium", layout="Wide")
            for img in imgs:
                if img["image"].endswith((".jpg", ".jpeg", ".png", ".gif")):
                    try:
                        response = requests.get(img["image"], timeout=5)
                        response.raise_for_status()
                        image = Image.open(BytesIO(response.content))
                        image.show()
                        print("Is this image good? (y/n)")
                        if input().lower() == "y":
                            image.save(os.path.join(pics, f"{name}.png"))
                        else:
                            continue
                    except Exception:
                        print(".. failed to download image", img["image"])
                        continue
                    else:
                        break
            else:
                continue
        image = Image.open(os.path.join(pics, f"{name}.png"))
        if image.width > 320 or image.height > 140:
            # resize to max 140 pixels of height, keeping aspect ratio
            image = image.resize((int(140 * image.width / image.height), 140), PIL.Image.Resampling.NEAREST)
        try:
            image = image.quantize(32, method=Image.Quantize.MAXCOVERAGE, dither=Image.Dither.FLOYDSTEINBERG)
        except ValueError:
            image = image.quantize(32, dither=Image.Dither.FLOYDSTEINBERG)
        image.putpalette(ImagePalette.ImagePalette('RGB', [(b >> 4) | (b >> 4 << 4) for b in image.palette.getdata()[1]]))
        image.convert("RGB")
        image.save(os.path.join(pics, f"{name}.png"))

        # save the image as IFF ILBM image
        with open(os.path.join(pics, f"{name}.iff"), "wb") as f:
            f.write(b"FORM")
            f.write(struct.pack(">I", 0))
            start = f.tell()
            f.write(b"ILBM")
            f.write(b"BMHD")
            f.write(struct.pack(">I", 20))
            f.write(struct.pack(">H", image.width))     # w
            f.write(struct.pack(">H", image.height))    # h
            f.write(struct.pack(">H", 0))               # x
            f.write(struct.pack(">H", 0))               # y
            f.write(struct.pack(">B", 5))               # nPlanes
            f.write(struct.pack(">B", 0))               # masking
            f.write(struct.pack(">B", 0))               # compression
            f.write(struct.pack(">B", 0))               # pad1
            f.write(struct.pack(">H", 0))               # transparentColor
            f.write(struct.pack(">B", 0))               # xAspect
            f.write(struct.pack(">B", 0))               # yAspect
            f.write(struct.pack(">H", image.width))     # pageWidth
            f.write(struct.pack(">H", image.height))    # pageHeight
            f.write(b"CMAP")
            f.write(struct.pack(">I", 3 * 32))
            colors = [c for c in image.palette.colors.keys()]
            while len(colors) < 32:
                colors.append((0, 0, 0))
            for i in range(32):
                r, g, b = colors[i]
                f.write(bytes([r, g, b]))
            f.write(b"BODY")
            rawdata = []
            for y in range(image.height):
                planes = [[], [], [], [], []]
                for x in range(image.width):
                    pixel = image.getpixel((x, y))
                    for i in range(5):
                        planes[i].append(pixel & 1)
                        pixel >>= 1    
                # compact the planes into bytes
                for plane in planes:
                    bytelen = 0
                    byte = 0
                    for bit in plane:
                        byte <<= 1
                        byte |= bit
                        bytelen += 1
                        if bytelen == 8:
                            rawdata.append(byte)
                            byte = 0
                            bytelen = 0
                    if bytelen != 0:
                        while bytelen < 8:
                            byte <<= 1
                            bytelen += 1
                        rawdata.append(byte)
                    # fill up to (w+15)/16 words
                    if len(rawdata) % ((image.width + 15) // 16) != 0:
                        rawdata.append(0)
            f.write(struct.pack(">I", len(rawdata)))
            f.write(bytes(rawdata))
            end = f.tell()
            f.seek(len(b"FORM"), os.SEEK_SET)
            f.write(struct.pack(">I", end - start))
