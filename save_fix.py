import json, re, difflib

SRC = r"C:\Users\User\Documents\deepseek-harness\default-workspace\in\SaveObjectDynamicDataGlobal.txt"
DST = r"C:\Users\User\Documents\deepseek-harness\default-workspace\out\SaveObjectDynamicDataGlobal.txt"

raw = open(SRC, 'rb').read()
bom = raw.startswith(b'\xef\xbb\xbf')
text = raw.decode('utf-8-sig')

pat = re.compile(r'(?m)^([ \t]*)"TeammateBuff"\s*:\s*"((?:[^"\\]|\\.)*)"')

def repl(m):
    ind = m.group(1)
    names = [s.strip() for s in json.loads('"' + m.group(2) + '"').split(',') if s.strip()]
    body = ',\n'.join(f'{ind}    {json.dumps(n, ensure_ascii=False)}' for n in names)
    return (f'{ind}"TeammateBuff": {{\n'
            f'{ind}  "refNames": [\n'
            f'{body}\n'
            f'{ind}  ]\n'
            f'{ind}}}')

new_text, n = pat.subn(repl, text)
d = json.loads(new_text)          # 语法校验
node = d['Data']['value']['storedDatasMainGame'][0]
assert node['TeammateBuff'] == {"refNames": ["雨露均沾检查器"]}, node['TeammateBuff']

out = new_text.encode('utf-8')
if bom:
    out = b'\xef\xbb\xbf' + out
open(DST, 'wb').write(out)
print('replaced:', n, '| bytes:', len(out))

print('--- diff ---')
for line in difflib.unified_diff(text.splitlines(), new_text.splitlines(),
                                 fromfile='in/SaveObjectDynamicDataGlobal.txt',
                                 tofile='out/SaveObjectDynamicDataGlobal.txt', n=2, lineterm=''):
    print(line)
