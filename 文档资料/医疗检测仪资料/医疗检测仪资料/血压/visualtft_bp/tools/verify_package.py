"""Check exported artifact integrity and UI resource consistency, then package it."""
from pathlib import Path
import json
import re
import zipfile
import xml.etree.ElementTree as ET
from PIL import Image, ImageFont, ImageChops

ROOT=Path(__file__).resolve().parents[1]
m=json.loads((ROOT/'manifest.json').read_text(encoding='utf-8'))
issues=[]
font_warnings=[]
check_count=0
def check(ok, message):
    global check_count
    check_count+=1
    if not ok: issues.append(message)

def inside(a):
    return a['x']>=0 and a['y']>=0 and a['x']+a['w']<=800 and a['y']+a['h']<=480

def overlaps(a,b):
    return a['x']<b['x']+b['w'] and a['x']+a['w']>b['x'] and a['y']<b['y']+b['h'] and a['y']+a['h']>b['y']

pages={p['key']:p for p in m['pages']}
buttons={b['key']:b for b in m['buttons']}
check(len({p['id'] for p in m['pages']})==len(m['pages']),'Duplicate page IDs')
for p in m['pages']:
    ids=[o['id'] for o in p['fields']+p['buttons']]
    check(len(ids)==len(set(ids)),f'Duplicate control IDs in {p["key"]}')
    for o in p['fields']+p['buttons']:check(inside(o),f'Out of bounds {p["key"]}/{o["id"]}')
    for i,a in enumerate(p['buttons']):
        check(any(x['source']==p['key'] and x['control_id']==a['id'] for x in m['actions']),f'Missing destination for {p["key"]}/{a["id"]}')
        for b in p['buttons'][i+1:]:check(not overlaps(a,b),f'Overlapping buttons {p["key"]}/{a["id"]}/{b["id"]}')
    for i,a in enumerate(p['fields']):
        for b in p['fields'][i+1:]:check(not overlaps(a,b),f'Overlapping text fields {p["key"]}/{a["id"]}/{b["id"]}')
    bg_source=next(x for x in m['renders'] if x['png']==p['background'])['svg']
    svg=ET.parse(ROOT/bg_source).getroot()
    labels=[''.join(el.itertext()) for el in svg.iter() if el.tag.endswith('}text')]
    check(not any(re.search(r'\d{2,}',t) for t in labels),f'Example numeric data in background {p["key"]}')
    for v in p['fields']:
        variants=[v['sample'],v['default']]+[s.get('values',{}).get(v['name'],'') for s in m['variants'].values() if s['page']==p['key']]
        font_path='C:/Windows/Fonts/consolab.ttf' if v['number'] else 'C:/Windows/Fonts/msyh.ttc'
        font=ImageFont.truetype(font_path,v['size'])
        for text in set(variants):
            width=font.getlength(text)
            if width>v['w']+1: font_warnings.append({'page':p['key'],'control':v['id'],'text':text,'width':round(width,1),'available':v['w']})

for a in m['actions']:
    check(a['target'] in pages or a['target'] in m['variants'],f'Missing target: {a}')
    check(a['button'] in buttons,f'Missing button: {a}')
    check(any(b['id']==a['control_id'] for b in pages[a['source']]['buttons']),f'Missing source control: {a}')
    with Image.open(ROOT/a['pair_image']) as im:
        pair=im.convert('RGB')
    for side,box,image_path in [('pressed',(16,60,816,540),a['pressed_image']),('destination',(832,60,1632,540),a['destination_image'])]:
        with Image.open(ROOT/image_path) as im:
            diff=ImageChops.difference(pair.crop(box),im.convert('RGB'))
        check(diff.getbbox() is None,f'Composite mismatch {a["sequence"]}/{side}')

for render in m['renders']:
    target=ROOT/render['png'];check(target.is_file(),f'Missing PNG {target}')
    if target.exists():
        with Image.open(target) as im:
            check(im.size==(render['width'],render['height']),f'Image dimensions {target}')
            if render['png'].startswith('assets/'):
                check(im.mode=='RGB',f'Import asset must be opaque RGB {target}')

guide=(ROOT/'VisualTFT_医院版血压界面绘制指南.md').read_text(encoding='utf-8')
links=re.findall(r'!?\[[^\]]*\]\(([^)]+)\)',guide)
for target in links:
    if not re.match(r'https?://|#',target):check((ROOT/target).is_file(),f'Broken MD link {target}')
html=(ROOT/'阅读版.html').read_text(encoding='utf-8')
for target in re.findall(r'(?:src|href)="([^"]+)"',html):
    if target.startswith('#'):check(f'id="{target[1:]}"' in html,f'Broken HTML anchor {target}')
    elif not target.startswith('http'):check((ROOT/target).is_file(),f'Broken HTML resource {target}')

# Every import asset must appear as an image in the guide.
for render in m['renders']:
    if render['png'].startswith('assets/'):
        check(f']({render["png"]})' in guide,f'Import image missing from guide: {render["png"]}')

report={'checks':check_count,'pages':len(pages),'button_types':len(buttons),'button_actions':len(m['actions']),
        'png_count':len(m['renders']),'errors':issues,'font_width_warnings':font_warnings,
        'hardware_tested':False,'visualtft_project_compiled':False}
(ROOT/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
if issues or font_warnings:raise SystemExit(1)

archive=ROOT.parent/'VisualTFT_血压界面绘制资料包.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for path in sorted(ROOT.rglob('*')):
        if path.is_file() and '__pycache__' not in path.parts:z.write(path,Path('visualtft_bp')/path.relative_to(ROOT))
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
print(json.dumps({'zip':str(archive),'bytes':archive.stat().st_size},ensure_ascii=False))
