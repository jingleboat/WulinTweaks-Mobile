#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从地图 prefab 的 `_SwimGrid` 图层生成手机端用的水域格烤表 `water_mask.h`。

数据来源（二选一，两者内容一致）：
    PC  : Wulin_Data/StreamingAssets/aa/StandaloneWindows64/map_assets_all.bundle（gzip 外套）
    APK : assets/aa/StandaloneWindows64/map_assets_all.bundle（同上）

做法：遍历 prefab 层级，对每个根对象 `Map_<id>` 找它下面名为 `_SwimGrid` 的 Tilemap，
把每个格子的 (x, y) 记下来，按 `y<<32 | x` 排序后写成 C 数组。

用法：
    python gen_water_mask.py                     # 生成并覆盖 native/src/water_mask.h
    python gen_water_mask.py --check             # 只比对，不写文件（生成器自检）
    python gen_water_mask.py --out some.h        # 写到别处
"""
import gzip
import os
import re
import sys

import UnityPy

DEFAULT_BUNDLE = (r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets"
                  r"\aa\StandaloneWindows64\map_assets_all.bundle")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "water_mask.h")

# 只有「有图层但没格子」的地图需要保留占位（原表里就是 2169）
ALWAYS_EMIT_EMPTY = [2169]

# ---- 手工覆盖（用户实测给的「手机端和 PC 不一致」的部分，2026-10-26）----
# 语义不同，别混：
#   ADD    ：这些格**额外算水域**（与 prefab 的 _SwimGrid 取并集）
#   REPLACE：这张图的水域**以文件为准**（整表替换，prefab 的格子不再自动并入）
WATER_DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "native", "water_data")
ADD_FILES = {504: "504.add.txt"}
REPLACE_FILES = {2160: "2160.replace.txt"}


def _parse_pairs(path):
    """文件格式：一行逗号/空白分隔的坐标流，如 `3,2,1,4` = (3,2) 与 (1,4)。"""
    txt = open(path, encoding="utf-8").read()
    nums = [int(x) for x in re.findall(r"-?\d+", txt)]
    if len(nums) % 2:
        nums = nums[:-1]
    return set((nums[i], nums[i + 1]) for i in range(0, len(nums), 2))


def apply_overrides(data):
    log = []
    for mid, fn in REPLACE_FILES.items():
        p = os.path.join(WATER_DATA_DIR, fn)
        if not os.path.exists(p):
            continue
        cells = _parse_pairs(p)
        old = len(set(data.get(mid, [])))
        data[mid] = list(cells)
        log.append("map %d 整表替换：%d -> %d 格（%s）" % (mid, old, len(cells), fn))
    for mid, fn in ADD_FILES.items():
        p = os.path.join(WATER_DATA_DIR, fn)
        if not os.path.exists(p):
            continue
        add = _parse_pairs(p)
        before = set(data.get(mid, []))
        after = before | add
        data[mid] = list(after)
        log.append("map %d 并入：%d + %d（新增 %d）-> %d 格（%s）"
                   % (mid, len(before), len(add), len(after - before), len(after), fn))
    return log


def load_bundle(path):
    raw = open(path, "rb").read()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    return UnityPy.load(raw)


def build_hierarchy(env):
    """返回 {transform_path_id: (go_path_id, father_path, [children])} 与 {go_path_id: name}"""
    tr = {}
    for obj in env.objects:
        if obj.type.name != "Transform":
            continue
        try:
            d = obj.read()
        except Exception:
            continue
        go = getattr(d, "m_GameObject", None)
        fa = getattr(d, "m_Father", None)
        kids = [getattr(k, "path_id", k) for k in getattr(d, "m_Children", [])]
        tr[obj.path_id] = (getattr(go, "path_id", None), getattr(fa, "path_id", None), kids)
    names = {}
    for obj in env.objects:
        if obj.type.name == "GameObject":
            try:
                names[obj.path_id] = obj.read().m_Name
            except Exception:
                pass
    return tr, names


def swim_tiles_by_map(env, tr, names):
    """{map_id: [(x,y), ...]}，以及「有 _SwimGrid 对象但没格子」的地图也会出现在结果里（空列表）。"""
    # GameObject(path) -> Tilemap 组件
    tilemap_of_go = {}
    for obj in env.objects:
        if obj.type.name != "Tilemap":
            continue
        try:
            tm = obj.read()
        except Exception:
            continue
        go = getattr(tm, "m_GameObject", None)
        pid = getattr(go, "path_id", None)
        if pid is None:
            continue
        coords = []
        for t in tm.m_Tiles or []:
            pos = t[0] if isinstance(t, tuple) else t.m_Position
            coords.append((pos.x, pos.y))
        tilemap_of_go[pid] = coords

    go2tr = {g: t for t, (g, f, k) in tr.items() if g}
    out = {}

    def walk(tid, map_id, root_depth):
        g = tr.get(tid, (None, None, []))[0]
        nm = names.get(g, "") or ""
        if nm == "_SwimGrid" and map_id is not None and g in tilemap_of_go:
            out.setdefault(map_id, []).extend(tilemap_of_go[g])
            return
        for k in tr[tid][2]:
            if k in tr:
                walk(k, map_id, root_depth + 1)

    roots = [t for t, (g, f, k) in tr.items() if f is None or f not in tr]
    for r in roots:
        g = tr[r][0]
        nm = names.get(g, "") or ""
        m = re.match(r"^Map_(\d+)$", nm)
        if not m:
            continue
        map_id = int(m.group(1))
        if go2tr.get(g) != r:
            pass
        walk(r, map_id, 0)
        out.setdefault(map_id, out.get(map_id, []))
    return out


def render(data):
    maps = sorted(k for k, v in data.items() if v)          # 有格子的图
    # 空表：原文件里只保留了 2169（有图层但一格都没画），保持这个约定
    empties = [m for m in ALWAYS_EMIT_EMPTY if m in data and not data[m]]
    lines = []
    lines.append("/* 自动生成：gen_water_mask.py —— 来自地图 prefab 的 _SwimGrid 图层，不要手改 */")
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("typedef struct { int mapid; int count; const int64_t* keys; } WaterMaskEntry;")
    lines.append("")
    for mid in maps:
        cells = sorted(set(data[mid]), key=lambda c: ((c[1] & 0xFFFFFFFF) << 32) | (c[0] & 0xFFFFFFFF))
        keys = [((c[1] & 0xFFFFFFFF) << 32) | (c[0] & 0xFFFFFFFF) for c in cells]
        lines.append("static const int64_t WM_%d[%d] = {" % (mid, len(keys)))
        for i in range(0, len(keys), 6):
            chunk = ", ".join("0x%016xLL" % k for k in keys[i:i + 6])
            lines.append("    " + chunk + ",")   # 末行也带逗号：与历史文件格式保持一致
        lines.append("};")
        lines.append("")
    # 空表（保持原文件里的占位写法）
    for mid in empties:
        lines.append("static const int64_t WM_%d[1] = {" % mid)
        lines.append("    0")
        lines.append("};")
        lines.append("")
    lines.append("static const WaterMaskEntry WATER_MASKS[] = {")
    for mid in maps:
        lines.append("    { %d, %d, WM_%d }," % (mid, len(set(data[mid])), mid))
    for mid in empties:
        lines.append("    { %d, 0, WM_%d }," % (mid, mid))
    lines.append("};")
    lines.append("#define WATER_MASK_COUNT %d" % (len(maps) + len(empties)))
    return "\n".join(lines) + "\n"


def main():
    path = DEFAULT_BUNDLE
    if "--bundle" in sys.argv:
        path = sys.argv[sys.argv.index("--bundle") + 1]
    out = OUT
    if "--out" in sys.argv:
        out = sys.argv[sys.argv.index("--out") + 1]

    env = load_bundle(path)
    tr, names = build_hierarchy(env)
    data = swim_tiles_by_map(env, tr, names)
    for line in apply_overrides(data):
        print("覆盖:", line)
    text = render(data)
    got = {k: len(set(v)) for k, v in sorted(data.items()) if v}
    print("扫描到有水格的地图:", got, "；有 _SwimGrid 图层但一格没画的地图数:", sum(1 for v in data.values() if not v))

    if "--check" in sys.argv:
        old = open(out, encoding="utf-8").read()
        same = old.strip() == text.strip()
        print("与现有 %s 一致: %s" % (os.path.basename(out), "是" if same else "否"))
        if not same:
            for i, (a, b) in enumerate(zip(old.splitlines(), text.splitlines())):
                if a != b:
                    print("  首个差异 行%d:\n    旧: %s\n    新: %s" % (i + 1, a, b))
                    break
        return
    open(out, "w", encoding="utf-8").write(text)
    print("已写出", out)


if __name__ == "__main__":
    main()
