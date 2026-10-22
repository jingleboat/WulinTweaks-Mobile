"""Rewrite the ModManager.TestLoadMods string literal inside global-metadata.dat.

The game stores the enabled-mod list as a global string: "0|ModA|0|ModB|..." where the
leading digit selects the source path (0 = streamingAssets/LocalModPath, 1 = SteamModPath).
TestLoadMods hardcodes one such string; we point that literal at a longer string of our own
that is appended past the end of the stringLiteralData section (the runtime does not bounds
check dataIndex).

usage: python meta_mods.py --meta in.dat --out out.dat --mods A,B,C
"""
import argparse
import struct

TBL_OFF = 0x100
TBL_SIZE = 0x33E28
DATA_OFF = 0x33F28
ENTRY = 1901  # the 71-byte "0|BalanceKongFu|..." literal


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--meta", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--mods", required=True, help="comma separated dir names, or 'all'")
    ap.add_argument("--source", default="0")
    a = ap.parse_args()

    d = bytearray(open(a.meta, "rb").read())
    n_entries = TBL_SIZE // 8

    if a.mods == "all":
        raise SystemExit("pass explicit names")

    names = [m.strip() for m in a.mods.split(",") if m.strip()]
    s = "|".join("%s|%s" % (a.source, m) for m in names)

    off, ln, di = struct.unpack_from("<II", d, TBL_OFF + ENTRY * 8), None, None
    old_len, old_idx = struct.unpack_from("<II", d, TBL_OFF + ENTRY * 8)
    print("old entry %d: len=%d idx=%#x -> %r" % (
        ENTRY, old_len, old_idx, bytes(d[DATA_OFF + old_idx:DATA_OFF + old_idx + old_len]).decode("utf8", "replace")))

    new_idx = len(d) - DATA_OFF
    blob = s.encode("utf8")
    d += blob
    struct.pack_into("<II", d, TBL_OFF + ENTRY * 8, len(blob), new_idx)
    print("new string (%d bytes) at dataIndex %#x: %s" % (len(blob), new_idx, s))
    open(a.out, "wb").write(d)
    print("wrote", a.out, len(d))


if __name__ == "__main__":
    main()
