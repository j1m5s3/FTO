"""
Tiles PNGs into one contact sheet (handy for reviewing turntables/clip frames at a glance).

  blender -b --factory-startup -P Tools/Blender/contact_sheet.py -- --dir <folder> --out sheet.png [--cols 4] [--match clip_]
"""
import os
import sys

import bpy
import numpy as np

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import fto_blender as fb  # noqa: E402


def main():
    args = fb.script_args()
    folder = args["dir"]
    cols = int(args.get("cols", 4))
    match = args.get("match", "")
    files = sorted(f for f in os.listdir(folder) if f.lower().endswith(".png") and match in f)
    if not files:
        print("FTO: no images")
        return

    images = [bpy.data.images.load(os.path.join(folder, f)) for f in files]
    w, h = images[0].size
    rows = (len(images) + cols - 1) // cols
    sheet = np.ones((rows * h, cols * w, 4), dtype=np.float32)

    for i, img in enumerate(images):
        px = np.array(img.pixels[:], dtype=np.float32).reshape(img.size[1], img.size[0], 4)
        px = px[:h, :w]
        r, c = divmod(i, cols)
        # Blender pixel rows start at the bottom; place row 0 at the top of the sheet.
        y0 = (rows - 1 - r) * h
        sheet[y0:y0 + px.shape[0], c * w:c * w + px.shape[1]] = px

    out = bpy.data.images.new("sheet", cols * w, rows * h, alpha=True)
    out.pixels[:] = sheet.ravel()
    out.filepath_raw = os.path.abspath(args["out"])
    out.file_format = 'PNG'
    out.save()
    print(f"FTO: wrote {out.filepath_raw} ({len(images)} images: {', '.join(files)})")


main()
