# -*- coding: utf-8 -*-
"""查存档里指定 uid 的角色实例状态"""
import json, re, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

p = r"device-saves\Save1-20261022\Save1\SaveObjectGameCharacter.json"
raw = open(p, encoding="utf-8").read()
raw = re.sub(r'(?<=[{,])\s*(-?\d+)\s*:', lambda m: '"%s":' % m.group(1), raw)
v = json.loads(raw)["Data"]["value"]
print("顶层键:", list(v.keys()))
cc = v.get("createdCharacter", {})
print("createdCharacter 实例数:", len(cc))

FIELDS = ["m_templeteUid", "m_isAlive", "m_name", "m_surName", "m_givenName", "m_teamMemberState",
          "m_hp", "m_mapID", "m_relationValue", "m_isDead", "m_joinTeam", "m_factionID"]

for uid in sys.argv[1:]:
    print("\n" + "=" * 60)
    print("uid =", uid)
    d = cc.get(uid)
    if d is None:
        # 找模板 uid 相同的
        hits = [(k, x) for k, x in cc.items() if isinstance(x, dict) and str(x.get("m_templeteUid")) == uid]
        print("  实例不存在；模板 uid 命中:", [h[0] for h in hits][:10])
        for k, x in hits[:3]:
            print("   ", k, {f: x.get(f) for f in FIELDS if f in x})
        continue
    print(json.dumps({k: d[k] for k in d if k in FIELDS}, ensure_ascii=False))
    # 其它有用的
    for k in ("m_teamMemberState", "m_relationValue", "m_isAlive", "m_isDead"):
        if k in d:
            print(f"   {k} = {d[k]!r}")
