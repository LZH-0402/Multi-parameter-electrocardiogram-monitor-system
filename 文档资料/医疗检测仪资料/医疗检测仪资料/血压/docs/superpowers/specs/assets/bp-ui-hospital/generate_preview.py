"""Generate editable SVG design previews using the SPO2_Hospital palette.

This builds design artifacts only. Device projects and protocols are untouched.
Run with Python 3; render the SVGs to PNG with render_svg.cjs afterwards.
"""
from pathlib import Path
from html import escape
import json

OUT = Path(__file__).resolve().parent
C = {
    'background': '#0F1B2D', 'header': '#16263A',
    'surface': '#18293D', 'inset': '#111F31',
    'data_surface': '#132D40', 'pulse_surface': '#193333',
    'border': '#30465E', 'text': '#F4F7FB', 'muted': '#ADBDCE',
    'cyan': '#42CDE0', 'green': '#68D99F', 'amber': '#F2CB67',
    'red': '#FF7C82', 'button': '#22364D',
    'button_pressed': '#304D63', 'button_border': '#607B95',
}

def rgb(h):
    return tuple(int(h[i:i+2], 16) for i in (1, 3, 5))

def luminance(h):
    ch = [v / 255 for v in rgb(h)]
    linear = [v / 12.92 if v <= 0.04045 else ((v+.055)/1.055)**2.4 for v in ch]
    return sum(v*w for v,w in zip(linear,(.2126,.7152,.0722)))

def contrast(a,b):
    hi,lo=sorted((luminance(a),luminance(b)),reverse=True)
    return (hi+.05)/(lo+.05)

TEXT_ROLES = [
    ('主文字 / 通用卡片','text','surface'),
    ('次文字 / 数据卡片','muted','data_surface'),
    ('次文字 / 顶栏','muted','header'),
    ('次文字 / 页面','muted','background'),
    ('压力与血压 / 数据卡片','cyan','data_surface'),
    ('脉率 / 脉率区','green','pulse_surface'),
    ('按钮文字 / 常态','text','button'),
    ('按钮文字 / 按下','text','button_pressed'),
    ('停止文字 / 常态','red','button'),
    ('停止文字 / 按下','text','button_pressed'),
    ('注意文字 / 提示','amber','surface'),
    ('琥珀标识 / 徽标','amber','button'),
    ('提示正文 / 深色提示','text','inset'),
    ('完成提示正文 / 绿色提示','text','pulse_surface'),
    ('次文字 / 禁用按钮','muted','inset'),
]

class Canvas:
    def __init__(self,w=800,h=480,title='血压界面设计稿'):
        self.w,self.h=w,h
        self.items=[]
        self.title=title

    def rect(self,x,y,w,h,fill,r=0,stroke=None,sw=1):
        self.items.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}"'+
                          (f' stroke="{stroke}" stroke-width="{sw}"' if stroke else '')+'/>' )

    def text(self,x,y,t,size=24,color=None,bold=False,number=False,anchor='start'):
        family = "Consolas, monospace" if number else "Microsoft YaHei, Noto Sans CJK SC, sans-serif"
        # y is the upper edge of the existing layout's text box.
        baseline=round(y+size*.88,2)
        self.items.append(f'<text x="{x}" y="{baseline}" fill="{color or C["text"]}" '
                          f'font-family="{family}" font-size="{size}" font-weight="{700 if bold or number else 400}" '
                          f'text-anchor="{anchor}">{escape(t)}</text>')

    def center(self,x,y,w,h,t,size=26,color=None,bold=True):
        self.text(x+w/2,y+(h-size)/2-1,t,size,color,bold,anchor='middle')

    def line(self,x1,y1,x2,y2,color=None,sw=1):
        self.items.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color or C["border"]}" stroke-width="{sw}"/>')

    def contents(self):
        return ''.join(self.items)

    def svg(self):
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
                f'viewBox="0 0 {self.w} {self.h}" role="img" aria-label="{escape(self.title)}">'
                f'<title>{escape(self.title)}</title>{self.contents()}</svg>')

class Page(Canvas):
    def __init__(self,title,card='data_surface'):
        super().__init__(title=title)
        self.rect(0,0,800,480,C['background'])
        self.rect(0,0,800,56,C['header'])
        self.rect(24,18,4,23,C['cyan'])
        self.text(40,15,'无创血压',28,bold=True)
        self.text(175,23,'NIBP',16,color=C['muted'])
        self.text(568,21,'2026-09-20  09:41',20,color=C['muted'])
        self.line(0,56,800,56)
        self.text(24,67,'会话 001 · 未关联患者',20,color=C['muted'])
        self.rect(24,96,420,224,C[card],12,C['border'])
        self.line(24,424,776,424)
        self.text(642,444,'UI · 演示数据',18,color=C['muted'])

    def button(self,x,y,w,h,label,kind='primary',state='up'):
        bg=C['button_pressed'] if state=='pressed' else C['button']
        edge=C['red'] if kind=='stop' else (C['cyan'] if kind=='primary' else C['button_border'])
        fg=C['red'] if kind=='stop' else C['text']
        if kind=='stop' and state=='pressed': fg=C['text']
        if state=='disabled': bg,edge,fg=C['inset'],C['border'],C['muted']
        self.rect(x,y,w,h,bg,12,edge,2 if kind in ('primary','stop') else 1.5)
        self.center(x,y,w,h,label,28 if w>200 else 24,fg)

    def tip(self,title,detail,kind='cyan'):
        bg={'cyan':C['inset'],'green':C['pulse_surface'],'amber':C['surface']}[kind]
        self.rect(24,336,752,64,bg,10)
        self.text(40,345,title,22,color=C[kind],bold=True)
        self.text(40,374,detail,20)

    def footer(self,label,color='muted'):
        self.items.append(f'<circle cx="28" cy="450" r="4" fill="{C[color]}"/>')
        self.text(44,441,label,20,color=C[color])


pages=[]
p=Page('待机页 · 血氧工程同色系')
p.text(40,114,'本次会话 · 上次结果',22,color=C['muted'])
p.text(62,176,'—  /  —',58,number=True,color=C['muted'])
p.text(40,270,'尚无测量结果',24)
p.button(492,112,280,112,'开始测量')
p.button(492,248,132,72,'历史','secondary')
p.button(640,248,132,72,'设置','secondary')
p.tip('准备测量','请按设备说明佩戴袖带，测量时保持静止')
p.footer('可开始 · 请核对测量对象','green')
pages.append(('idle','01  待机 · 单击开始',p))

p=Page('测量页 · 血氧工程同色系')
p.text(40,113,'测量中',24,color=C['cyan'],bold=True)
p.text(40,155,'156',88,number=True,color=C['cyan'])
p.text(300,201,'mmHg',24,color=C['cyan'])
p.line(40,254,428,254)
p.text(40,275,'袖带压力',22)
p.text(239,275,'已用时 00:28',22,color=C['muted'])
p.button(492,112,280,112,'停止测量','stop')
p.text(492,257,'需要中止时，点击停止',22,color=C['muted'])
p.text(492,288,'本次结果尚未生成',22,color=C['muted'])
p.tip('测量进行中','请保持静止，测量时避免说话')
p.footer('正在接收压力数据','cyan')
pages.append(('measuring','02  测量 · 随时停止',p))

p=Page('结果页 · 血氧工程同色系')
# Recolour the existing PR row without moving its bounds or adding a new panel.
p.items.append(f'<path d="M25 255H443V307Q443 319 431 319H37Q25 319 25 307Z" fill="{C["pulse_surface"]}"/>')
p.text(40,112,'收缩压 SYS',22,color=C['cyan'])
p.text(248,112,'舒张压 DIA',22,color=C['cyan'])
p.text(40,147,'120',76,number=True,color=C['cyan'])
p.text(248,147,'80',76,number=True,color=C['cyan'])
p.text(40,224,'mmHg',20,color=C['cyan'])
p.text(248,224,'mmHg',20,color=C['cyan'])
p.line(232,112,232,245)
p.line(40,255,428,255)
p.text(40,278,'脉率 PR',22,color=C['green'])
p.text(205,259,'72',52,number=True,color=C['green'])
p.text(292,282,'次/分',22,color=C['green'])
p.button(492,112,280,112,'再次测量')
p.button(492,248,132,72,'历史','secondary')
p.button(640,248,132,72,'换患者','secondary')
p.tip('测量完成','测量时间 09:41 · 本次会话第 1 次测量','green')
p.footer('结果来自本次测量','green')
pages.append(('result','03  结果 · 大数字读数',p))

p=Page('异常页 · 血氧工程同色系',card='surface')
p.rect(40,112,70,30,C['button'],8)
p.center(40,112,70,30,'E01',20,C['amber'])
p.text(40,162,'压力未建立',32,bold=True)
p.text(40,218,'请检查袖带佩戴',24)
p.text(40,253,'和气管连接',24)
p.text(40,291,'本次未获得有效结果',20,color=C['muted'])
p.button(492,112,280,112,'重新测量')
p.button(492,248,280,72,'返回','secondary')
p.tip('检查完成后再重试','若问题持续，请联系维护人员','amber')
p.footer('设备允许重试 · 请先检查','amber')
pages.append(('error','04  异常 · 原因与下一步',p))

for name,title,p in pages:
    (OUT/f'{name}.svg').write_text(p.svg(),encoding='utf-8')

b=Canvas(1728,1372,'血压界面 · SPO2 Hospital 同色系 · 四页总览')
b.rect(0,0,b.w,b.h,C['background'])
b.text(40,28,'NIBP / 医院系列配色',38,bold=True)
b.text(40,82,'沿用血氧工程的深蓝底色、青色读数与绿色脉率 · 800 × 480',22,C['muted'])
for i,(name,title,p) in enumerate(pages):
    x=40+(i%2)*848
    y=148+(i//2)*562
    b.text(x,y,title,24,bold=True)
    b.items.append(f'<g transform="translate({x} {y+42})">{p.contents()}</g>')
b.text(40,1252,'色彩来自 SPO2_Hospital 工程；页面位置、按钮尺寸与测量流程沿用已确认方案。',20,C['muted'])
for i,(key,label) in enumerate([('background','深蓝背景'),('cyan','血压读数'),('green','脉率 / 完成'),('amber','注意提示'),('red','停止 / 故障')]):
    x=40+i*332
    b.rect(x,1303,28,28,C[key],7,C['button_border'])
    b.text(x+40,1304,label,20)
    b.text(x+175,1306,C[key],18,C['muted'])
(OUT/'overview.svg').write_text(b.svg(),encoding='utf-8')

state=Canvas(1016,390,'血压界面按钮配色：常态、按下、禁用')
state.rect(0,0,1016,390,C['background'])
state.text(24,24,'按钮配色状态',28,bold=True)
for col,(value,label) in enumerate([('up','常态'),('pressed','按下'),('disabled','禁用')]):
    x=24+col*332
    state.text(x,80,label,22,C['muted'])
    # Use the same geometry method with a temporary page instance.
    page=Page('state')
    page.items=[]
    page.button(x,124,280,92,'开始测量',state=value)
    page.button(x,244,280,92,'停止测量',kind='stop',state=value)
    state.items.extend(page.items)
state.text(24,359,'禁用示例仅表示非测量状态；测量中不禁用停止入口。',18,C['muted'])
(OUT/'button-states.svg').write_text(state.svg(),encoding='utf-8')

ratios=[{'role':name,'foreground':C[f],'background':C[bg],'ratio':round(contrast(C[f],C[bg]),2)} for name,f,bg in TEXT_ROLES]
palette={
    'reference_project':r'F:\VisualTFT\SPO2_Hospital',
    'sources':['tools/build_pages.py lines 42-48','主界面.tft text foreground and background colors','images/bg_0.png','images/bg_3.png','images/btn_confirm_up.png','images/btn_confirm_down.png','images/btn_back_up.png'],
    'colors':C,
    'visualtft_rgb':{k:';'.join(map(str,rgb(v))) for k,v in C.items()},
    'text_contrast_checks':ratios,
    'scope':'Colour adaptation of approved 800x480 layout; SVG/HTML review artifacts only.',
}
(OUT/'palette.json').write_text(json.dumps(palette,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

cards=''.join(f'<figure><figcaption>{escape(title)}</figcaption>{p.svg()}<a href="{name}.svg">打开原尺寸 SVG</a></figure>' for name,title,p in pages)
swatches=''.join(f'<li><i style="background:{val}"></i><span>{key}<small>{val}</small></span></li>' for key,val in C.items())
doc=f'''<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>NIBP · 医院系列配色</title><style>
:root{{color-scheme:dark;--bg:{C['background']};--text:{C['text']};--muted:{C['muted']};--line:{C['border']};--cyan:{C['cyan']}}}
*{{box-sizing:border-box}}body{{margin:0;background:var(--bg);color:var(--text);font-family:'Microsoft YaHei',sans-serif;padding:32px}}
main{{max-width:1640px;margin:auto}}h1{{font-size:30px;margin:0 0 12px}}p{{color:var(--muted);line-height:1.7}}.screens{{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:32px 24px;margin:30px 0}}
figure{{margin:0}}figcaption{{font-size:20px;margin:0 0 12px}}figure svg{{display:block;width:100%;height:auto;border:1px solid var(--line)}}a{{color:var(--cyan);display:inline-block;padding:10px 0}}a:focus-visible{{outline:3px solid var(--cyan);outline-offset:4px}}
ul{{padding:0;display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:16px;list-style:none}}li{{display:flex;gap:12px;align-items:center}}i{{width:32px;height:32px;border:1px solid {C['button_border']};border-radius:7px}}small{{display:block;color:var(--muted);margin-top:4px}}
.states{{max-width:1016px}}.states svg{{width:100%;height:auto}}footer{{margin:28px 0;color:var(--muted)}}@media(max-width:1100px){{.screens{{grid-template-columns:1fr}}body{{padding:20px}}}}
</style></head><body><main><h1>NIBP / 医院系列配色</h1><p>布局沿用已确认方案；配色参考 SPO2_Hospital。下方为四张静态设计稿，数值均为演示数据。</p>
<section class="screens" aria-label="核心页面">{cards}</section><h2>按钮状态</h2><div class="states">{state.svg()}</div>
<h2>工程色表</h2><ul>{swatches}</ul><footer>设计画布 800 × 480；查看原尺寸 SVG 可按像素核对。页面图用于配色评审，尚未生成或验证可烧录工程。</footer></main></body></html>'''
(OUT/'preview.html').write_text(doc,encoding='utf-8')
print('Generated 6 SVGs, preview.html and palette.json.')
print('Text contrast minimum:',min(r['ratio'] for r in ratios))
assert all(r['ratio'] >= 4.5 for r in ratios), 'A text colour pair needs adjustment.'
