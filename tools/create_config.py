from argparse import ArgumentParser
from io import BytesIO
import re
import struct
from PIL import Image, ImagePalette
import PIL
import os
import requests
from bs4 import BeautifulSoup

if __name__ == "__main__":
    parser = ArgumentParser()
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

    pics = os.path.join(args.output, "cdtvlauncher.pics")
    os.makedirs(pics, exist_ok=True)

    def ppname(name):
        return re.sub("(NTSC|PAL|CDTV|1MB|2MB| De)", "", re.sub(r"([a-z0-9&\+])([A-Z0-9&\+])", r"\1 \2", name)).strip()

    with open(os.path.join(args.output, "cdtvlauncher.config"), "w") as f:
        for slave in slave_files:
            basename = os.path.basename(slave)
            name, _ = os.path.splitext(basename)
            relpath = os.path.relpath(slave, args.folder)
            name = ppname(name)
            f.write(f"{name}\n")
            f.write(args.command_template.format(relslave=relpath, reldir=os.path.dirname(relpath), basename=basename) + "\n")
            f.write(f"sys:cdtvlauncher.pics/{name}.iff\n")

    for slave in slave_files:
        basename = os.path.basename(slave)
        name, _ = os.path.splitext(basename)
        name = ppname(name)
        print(name)

        searchname = re.sub(r" ", r"%20", name)
        if not os.path.exists(os.path.join(pics, f"{name}.png")):
            # convert camel case to spaces
            # search google for an image for this game
            url = f"https://www.google.com/search?q={searchname}+Amiga+Gameplay&tbm=isch"
            headers = {
                "User-Agent": "Dillo/3.0.5"
            }
            response = requests.get(url, headers=headers)
            response.raise_for_status()
            # find the first image
            soup = BeautifulSoup(response.text, "html.parser")
            try:
                img = soup.find_all("img")[1]
            except IndexError:
                continue
            src = img["src"]
            # download the image, joining the img src with the base url if necessary
            if src.startswith("/"):
                src = f"https://www.google.com{src}"
            response = requests.get(src)
            response.raise_for_status()
            # ensure the image is indexed to 32 amiga colors
            image = Image.open(BytesIO(response.content))
            image.save(os.path.join(pics, f"{name}.png"))
        image = Image.open(os.path.join(pics, f"{name}.png"))
        if image.width > 320 or image.height > 256:
            # resize keeping aspect ratio
            image = image.resize((320, int(320 * image.height / image.width)), PIL.Image.Resampling.NEAREST)
        image = image.quantize(32, method=Image.Quantize.MAXCOVERAGE, dither=Image.Dither.NONE)
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
