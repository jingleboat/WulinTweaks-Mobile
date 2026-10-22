# -*- coding: utf-8 -*-
"""最终口径：
  跑过   = GraphDataList 里 State != NotOpen(0)
  因入队 = 运行时 [compare] 中 放宽=通过 且 原版=不通过
  注意：扫描是对内存里所有剧情图做假设性计算，NotOpen 的图虽然能算出结果，但从未打开，不算触发。
"""
import hashlib, gzip, json, re, sys, io
from collections import defaultdict
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
ROOT = r"C:\Users\User\AppData\LocalLow\DefaultCompany\Wulin\76561198807754750"
LOG = r"D:\steamgame\steamapps\common\WulinSH\BepInEx\LogOutput.log"
LOC = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor\Assets\GameRes\json\3\LocData.txt"
ST = {0:"NotOpen",1:"Close",2:"Opened",3:"Started",4:"已完成",5:"Failed",6:"TimeOut",7:"CloseAfterOpen"}

def decrypt(p):
    d=open(p,"rb").read(); iv,ct=d[:16],d[16:]
    k=hashlib.pbkdf2_hmac("sha1", b"Meow", iv, 100, 16)
    c=Cipher(algorithms.AES(k), modes.CBC(iv)).decryptor()
    pt=c.update(ct)+c.finalize(); pt=pt[:-pt[-1]]
    return gzip.decompress(pt) if pt[:2]==b"\x1f\x8b" else pt
def load(s):
    raw=decrypt(ROOT+"\\"+s+"\\SaveObjectStory.save").decode("utf-8")
    raw=re.sub(r'(?<=[{,])\s*(-?\d+)\s*:', lambda m:'"%s":'%m.group(1), raw)
    return json.loads(raw)["Data"]["value"]

loc={}
for r in json.loads(open(LOC,"rb").read().decode("utf-8-sig"))["LocData"]:
    un=r.get("uName") or ""
    if un: loc[un]=r.get("SChinese") or ""
def nm(g): return loc.get(g+"_name","") or loc.get(g+"_overview1","")

team=defaultdict(int); both=defaultdict(int)
pat=re.compile(r"\[compare\] graph=(\S+) 放宽=(\S+) 原版=(\S+)")
for line in open(LOG, encoding="utf-8", errors="replace"):
    m=pat.search(line)
    if not m: continue
    g,rel,orig=m.group(1),m.group(2),m.group(3)
    if rel=="通过" and orig=="不通过": team[g]+=1
    elif rel=="通过" and orig=="通过": both[g]+=1

data={}
for slot in ("Save0","Save2"):
    v=load(slot); gdl=v.get("GraphDataList",{})
    data[slot]={g:d for g,d in gdl.items() if isinstance(d,dict)}

for slot in ("Save0","Save2"):
    gdl=data[slot]
    ran={g for g,d in gdl.items() if d.get("State",0)!=0}
    star=sorted(g for g in ran if g in team)
    print("="*92)
    print("%s：GraphDataList %d 项，其中 State!=NotOpen（真正打开过）%d 项" % (slot,len(gdl),len(ran)))
    print("="*92)
    print("\n★ 打开过 + 会被「角色在队」放宽影响：%d 个" % len(star))
    print("%-22s %-10s %-5s %-6s %s" % ("剧情图","State","note","入队次数","名称"))
    print("-"*92)
    for g in star:
        st=gdl[g].get("State"); nn=len(gdl[g].get("note") or [])
        print("%-22s %-10s %-5d %-6d %s" % (g,ST.get(st,st),nn,team[g],nm(g)))
    print()

# 差异：Save0 打开过、Save2 没有
a={g for g,d in data["Save0"].items() if d.get("State",0)!=0}
b={g for g,d in data["Save2"].items() if d.get("State",0)!=0}
print("="*92)
print("Save0 打开过但 Save2 没有的：%d 个（含进度差异，需人工判断）" % len(a-b))
print("="*92)
for g in sorted(a-b):
    st=data["Save0"][g].get("State")
    tag="★因队友条件" if g in team else ""
    print("  %-24s %-10s %-14s %s" % (g,ST.get(st,st),tag,nm(g)))
