# -*- coding: utf-8 -*-
"""逐字段对比 Android 原生 mod 与 PC 移植 mod 的 Addressables catalog"""
import base64
import json
import zipfile

APK = r'E:\笔记\wulin_v15-综合护肝-2026-10-20.apk'
PC = r'F:\SteamLibrary\steamapps\common\WulinSH\Mods\NewMyMod001'


def load_cat(z, slot):
    p = 'assets/Mods/Android/%s/catalog_0.1.json' % slot
    h = 'assets/Mods/Android/%s/catalog_0.1.hash' % slot
    return json.loads(z.read(p).decode('utf-8-sig')), z.read(h)


def brief(c, h, tag):
    print('=' * 70)
    print(tag)
    print('  keys:', list(c.keys()))
    print('  hash file (%d B): %s' % (len(h), h.decode('latin1').strip()))
    print('  m_BuildResultHash:', c.get('m_BuildResultHash'))
    print('  m_LocatorId:', c.get('m_LocatorId'))
    rp = c.get('m_ResourceProviderData', [])
    print('  m_ResourceProviderData (%d):' % len(rp))
    for p in rp:
        print('     ', p['m_Id'].split('.')[-1], '| data=', repr(p.get('m_Data'))[:40])
    print('  m_ProviderIds:', [x.split('.')[-1] for x in c.get('m_ProviderIds', [])])
    print('  m_InternalIdPrefixes:', c.get('m_InternalIdPrefixes'))
    print('  m_resourceTypes:', c.get('m_resourceTypes'))
    print('  m_InstanceProviderData:', c.get('m_InstanceProviderData', {}).get('m_Id', '').split('.')[-1])
    print('  m_SceneProviderData:', c.get('m_SceneProviderData', {}).get('m_Id', '').split('.')[-1])
    print('  blob sizes:')
    for k in ('m_KeyDataString', 'm_BucketDataString', 'm_EntryDataString', 'm_ExtraDataString'):
        v = c.get(k)
        if v is None:
            print('     %-20s (missing)' % k)
            continue
        try:
            raw = base64.b64decode(v)
        except Exception:
            raw = b''
        print('     %-20s b64=%d  bin=%d  head=%s' % (k, len(v), len(raw), raw[:16].hex()))
    print()


def main():
    z = zipfile.ZipFile(APK)
    bj, bjh = load_cat(z, 'Bianjiecaozuo')
    nm, nmh = load_cat(z, 'NewMyMod001')
    brief(bj, bjh, 'Bianjiecaozuo（安卓原生，游戏内正常生效）')
    brief(nm, nmh, 'NewMyMod001（从 PC 移植，Reported 不生效）')

    # 旧的原生 NewMyMod001 catalog（v14 里的）作为第三份参照
    z14 = zipfile.ZipFile(r'E:\笔记\wulin_v14.apk')
    j14 = json.loads(z14.read('assets/Mods/Android/NewMyMod001/catalog_0.1.json').decode('utf-8-sig'))
    h14 = z14.read('assets/Mods/Android/NewMyMod001/catalog_0.1.hash')
    brief(j14, h14, 'NewMyMod001（v14 原生「体验调整护肝」，可正常加载）')

    print('=' * 70)
    print('PC 原始 catalog（源文件）')
    pcj = json.load(open(PC + r'\catalog_0.1.0.json', 'r', encoding='utf-8-sig'))
    pch = open(PC + r'\catalog_0.1.0.hash', 'rb').read()
    brief(pcj, pch, 'PC catalog_0.1.0.json')


if __name__ == '__main__':
    main()
