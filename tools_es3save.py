"""大侠立志传（手机/PC 通用）存档解密与查询工具。

存档是 Easy Save 3 的加密 JSON：
  AES-128-CBC，IV = 文件前 16 字节，密钥 = PBKDF2-HMAC-SHA1("Meow", salt=IV, 100 轮, 16 字节)，
  解出后是 GZip，再解压得到 ES3 JSON（字典键有时是裸整数，需要补引号）。

口令来源：游戏 Resources 里的 `ES3/ES3Defaults` 资产（路径 `SaveFile.es3`、口令 `Meow`），
不是猜的 —— 从 PC 版 `Wulin_Data/resources.assets` 里能直接读到这两个字符串，
手机版同一套配置，所以两端存档通用。

用法：
  python tools_es3save.py dump   <Save目录> [输出目录]   # 解密目录下所有 *.save
  python tools_es3save.py find   <Save目录> <npcID>      # 查某个角色/实例在哪张图、什么坐标
  python tools_es3save.py player <Save目录>              # 队伍与主角所在位置
  python tools_es3save.py raw    <单个.save 文件>        # 打印单个文件 JSON
"""
import sys, os, re, gzip, json, math

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.padding import PKCS7

PASSWORD = b"Meow"
ITER = 100
KEYLEN = 16

# ES3 JSON 里的裸整数键，如 {"historyDlcs":{1:true}}
_BARE_KEY = re.compile(r'(?<=[{,])(-?\d+):')


def decrypt_bytes(raw: bytes) -> bytes:
    import hashlib
    iv, ct = raw[:16], raw[16:]
    key = hashlib.pbkdf2_hmac("sha1", PASSWORD, iv, ITER, KEYLEN)
    dec = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    data = dec.update(ct) + dec.finalize()
    unpad = PKCS7(128).unpadder()
    data = unpad.update(data) + unpad.finalize()
    if data[:2] == b"\x1f\x8b":
        data = gzip.decompress(data)
    return data


def load_text(path: str) -> str:
    return _BARE_KEY.sub(r'"\1":', decrypt_bytes(open(path, "rb").read()).decode("utf-8"))


def load(path: str):
    return json.loads(load_text(path))


def encrypt_bytes(payload: bytes) -> bytes:
    """把 gzip 后的明文按 ES3 规则加密：随机 IV + AES-128-CBC(PKCS7)。"""
    import hashlib, os as _os
    from cryptography.hazmat.primitives.padding import PKCS7 as _PK
    iv = _os.urandom(16)
    key = hashlib.pbkdf2_hmac("sha1", PASSWORD, iv, ITER, KEYLEN)
    padder = _PK(128).padder()
    body = padder.update(payload) + padder.finalize()
    enc = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    return iv + enc.update(body) + enc.finalize()


def save_json_obj(obj, path: str):
    """把 dict 写回 .save（紧凑 JSON -> gzip -> 加密）。"""
    import gzip as _gz
    txt = json.dumps(obj, ensure_ascii=False, separators=(",", ":"))
    open(path, "wb").write(encrypt_bytes(_gz.compress(txt.encode("utf-8"), 9)))


def _f(d: str, name: str):
    return os.path.join(d, "SaveObject" + name + ".save")


def cmd_dump(d: str, out: str = None):
    if out:
        os.makedirs(out, exist_ok=True)
    for name in sorted(os.listdir(d)):
        if not name.endswith(".save"):
            continue
        p = os.path.join(d, name)
        try:
            txt = json.dumps(load(p), ensure_ascii=False, indent=1)
        except Exception as e:
            print(f"!! {name}: {e}")
            continue
        if out:
            dst = os.path.join(out, name[:-5] + ".json")
            open(dst, "w", encoding="utf-8").write(txt)
            print(f"{name} -> {dst} ({len(txt)} bytes)")
        else:
            print(f"{name}: {len(txt)} bytes")


def cmd_player(d: str):
    pt = load(_f(d, "PlayerTeam"))['Data']['value']
    gm = load(_f(d, "Game"))['Data']['value']
    print(f"槽位名        : {gm['miscStringDict'].get('OverrideSaveFileName')}")
    print(f"最后保存时间  : {gm['miscStringDict'].get('PlayerLastSaveTime')}")
    print(f"当前地图      : {pt['mapId']}（PlayerMapID={gm['miscIntDict'].get('PlayerMapID')}）")
    print(f"队伍坐标      : {pt['teamPos']}  朝向={pt.get('teamDir')}")
    print(f"队伍成员      : {pt['playerTeam']}  宠物={pt.get('playerPet')}")


def cmd_find(d: str, target: str):
    cc = load(_f(d, "GameCharacter"))['Data']['value']['createdCharacter']
    mm = load(_f(d, "MapManager"))['Data']['value']
    pt = load(_f(d, "PlayerTeam"))['Data']['value']

    inst = cc.get(target)
    print(f"=== 实例 {target} ===")
    if inst:
        nm = (inst.get('m_surName') or '') + (inst.get('m_givenName') or '')
        for k in ('m_instanceId', 'm_templeteUid', 'm_surName', 'm_givenName', 'm_curMapId',
                  'm_curPos', 'm_targetMapId', 'm_targetPos', 'm_RespawnMap', 'm_RespawnPos',
                  'm_factionJob', 'm_isAlive'):
            print(f"  {k} = {json.dumps(inst.get(k), ensure_ascii=False)}")
        print(f"  显示名 = {inst.get('m_surName') or ''}{inst.get('m_givenName') or ''}"
              f"{'  (名字前带 @ 表示玩家尚未认识)' if (inst.get('m_givenName') or '').startswith('@') else ''}")
    else:
        print("  createdCharacter 里没有这个 id")

    print(f"=== 名册（MapNpcDataList）里的条目 ===")
    for mapid, npcs in mm['MapNpcDataList'].items():
        if target in npcs:
            dd = npcs[target]
            pos = dd.get('pos') or []
            q = pos[min(dd.get('curListIndex', 0), len(pos) - 1)] if pos else None
            print(f"  map={mapid} pos={json.dumps(q, ensure_ascii=False)} "
                  f"dir={dd.get('direction')} faction={dd.get('factionID')} job={dd.get('jobID')} "
                  f"stayTime={dd.get('stayTime')}")

    lead = pt['teamPos']
    print(f"=== 与队伍的相对位置（队伍在 {pt['mapId']} ({lead['x']},{lead['y']})）===")
    if inst:
        cp = inst.get('m_curPos') or {}
        if 'x' in cp:
            print(f"  实例坐标 ({cp['x']},{cp['y']})，直线距离 {math.hypot(cp['x']-lead['x'], cp['y']-lead['y']):.1f} 格"
                  f"（前提：同在当前地图）")


def main(argv):
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd, d = argv[1], argv[2]
    if cmd == "dump":
        cmd_dump(d, argv[3] if len(argv) > 3 else None)
    elif cmd == "find":
        cmd_find(d, argv[3])
    elif cmd == "player":
        cmd_player(d)
    elif cmd == "raw":
        print(json.dumps(load(d), ensure_ascii=False, indent=1))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
