"""List mod directories packaged in a Hero's Adventure APK (one per line, sorted)."""
import sys
import zipfile

apk = sys.argv[1] if len(sys.argv) > 1 else r"E:\笔记\base_sign.apk"
z = zipfile.ZipFile(apk)
names = sorted({n.split("/")[3] for n in z.namelist()
                if n.startswith("assets/Mods/Android/") and n.count("/") > 3})
print("\n".join(names))
