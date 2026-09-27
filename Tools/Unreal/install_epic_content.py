"""
Copies Epic's free mannequin content (skeleton, physics asset, control rigs and about a hundred animations) out of
the engine's own templates into Content/Characters/Mannequins, where FTO's characters use it. Plain Python, no
editor needed; run it once after cloning (and after switching engine versions):

  python Tools/Unreal/install_epic_content.py [--engine "C:/Program Files/Epic Games/UE_5.8"]

The content ships with every Unreal Engine install and may be used in Unreal projects under the engine's licence,
but it isn't FTO's to publish, so the folder is git-ignored rather than committed. Packaged builds include it.
"""
import argparse
import os
import shutil
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEST = os.path.join(REPO, "Content", "Characters", "Mannequins")
TEMPLATE = os.path.join("Templates", "TemplateResources", "High", "Characters", "Content", "Mannequins")


def default_engine():
    for root in (os.environ.get("UE_ROOT", ""), r"C:\Program Files\Epic Games\UE_5.8"):
        if root and os.path.isdir(os.path.join(root, TEMPLATE)):
            return root
    return ""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--engine", default=default_engine(), help="Unreal Engine 5.8 install folder")
    args = parser.parse_args()
    source = os.path.join(args.engine, TEMPLATE)
    if not os.path.isdir(source):
        sys.exit(f"Can't find the mannequin content at {source}: pass --engine <UE 5.8 folder>")
    # Same path as in Epic's templates (/Game/Characters/Mannequins), so the assets' references to each other hold.
    shutil.copytree(source, DEST, dirs_exist_ok=True)
    count = sum(len(files) for _, _, files in os.walk(DEST))
    print(f"FTO: copied Epic's mannequin content ({count} files) to {DEST}")


if __name__ == "__main__":
    main()
