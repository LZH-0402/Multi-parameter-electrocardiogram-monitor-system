"""Build the approved BP design's VisualTFT guide and code-native SVG assets.

This is an asset/document generator, not a screen project or device firmware.
Run with Python, then render_assets.cjs, then verify_package.py.
"""
from pathlib import Path
from html import escape
import csv
import json
import re

ROOT = Path(__file__).resolve().parents[1]
for directory in ('assets/backgrounds', 'assets/buttons', 'previews', 'transitions', 'sources', 'tools'):
    (ROOT / directory).mkdir(parents=True, exist_ok=True)

C = {
    'background':'#0F1B2D', 'header':'#16263A', 'surface':'#18293D',
    'inset':'#111F31', 'data':'#132D40', 'pulse':'#193333', 'border':'#30465E',
    'text':'#F4F7FB', 'muted':'#ADBDCE', 'cyan':'#42CDE0', 'green':'#68D99F',
    'amber':'#F2CB67', 'red':'#FF7C82', 'button':'#22364D',
    'pressed':'#304D63', 'button_border':'#607B95'
}
PAGES = {}
BUTTONS = {}
ACTIONS = []
RENDERS = []


class SVG:
    def __init__(self, w=800, h=480):
        self.w, self.h, self.items = w, h, []

    def rect(self, x, y, w, h, fill, r=0, stroke=None, sw=1):
        self.items.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}"' +
                          (f' stroke="{stroke}" stroke-width="{sw}"' if stroke else '') + '/>')

    def text(self, x, y, value, size=24, color=None, bold=False, align='start', number=False):
        font = 'Consolas, monospace' if number else 'Microsoft YaHei, sans-serif'
        self.items.append(f'<text x="{x}" y="{round(y + size * .88, 2)}" font-family="{font}" '
                          f'font-size="{size}" font-weight="{700 if bold or number else 400}" '
                          f'fill="{color or C["text"]}" text-anchor="{align}">{escape(str(value))}</text>')

    def line(self, x, y, xx, yy, color=None):
        self.items.append(f'<path d="M{x} {y}H{xx}" stroke="{color or C["border"]}"/>' if y == yy else
                          f'<path d="M{x} {y}L{xx} {yy}" stroke="{color or C["border"]}"/>')

    def group(self, other, x=0, y=0, scale=1):
        self.items.append(f'<g transform="translate({x} {y}) scale({scale})">{"".join(other.items)}</g>')

    def svg(self):
        return f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" viewBox="0 0 {self.w} {self.h}">' + ''.join(self.items) + '</svg>'


def export(canvas, relative):
    source = 'sources/' + relative.replace('/', '__').replace('.png', '.svg')
    (ROOT / source).write_text(canvas.svg(), encoding='utf-8')
    RENDERS.append({'svg':source, 'png':relative, 'width':canvas.w, 'height':canvas.h})


def field(p, cid, name, x, y, w, h, sample, default='--', size=24, color='text', bg='background', align='left', number=False):
    p['fields'].append(dict(id=cid, name=name, x=x, y=y, w=w, h=h, sample=sample, default=default,
                            size=size, color=color, bg=bg, align=align, number=number))


def page(pid, key, title, subtitle='会话 001 · 未关联患者', card=True, card_color='data'):
    s = SVG()
    s.rect(0,0,800,480,C['background'])
    s.rect(0,0,800,56,C['header'])
    s.rect(24,18,4,23,C['cyan'])
    s.text(40,15,title,28,bold=True)
    if title == '无创血压': s.text(175,23,'NIBP',16,C['muted'])
    s.line(0,56,800,56)
    s.line(24,424,776,424)
    if card: s.rect(24,96,420,224,C[card_color],12,C['border'])
    p = dict(id=pid,key=key,title=title,base=s,fields=[],buttons=[],selected=None,
             note='',background=f'assets/backgrounds/p{pid:02d}_{key}.png')
    field(p,100,'CLOCK',548,17,228,28,'2026-09-21 09:41','时间未校准',20,'muted','header',align='right')
    field(p,101,'CONTEXT',24,65,752,27,subtitle,'未关联患者',20,'muted')
    field(p,102,'FOOTER',24,436,584,30,'','等待设备状态',20,'muted')
    PAGES[key] = p
    return p


def foot(p, text, color='muted'):
    f = next(v for v in p['fields'] if v['name'] == 'FOOTER')
    f['sample'], f['color'] = text, color


def tip(p, title, detail, kind='cyan'):
    bg = {'cyan':'inset','green':'pulse','amber':'surface','red':'surface'}[kind]
    p['base'].rect(24,336,752,64,C[bg],10)
    field(p,103,'TIP_TITLE',40,342,714,28,title,'等待设备状态',22,kind,bg)
    field(p,104,'TIP_DETAIL',40,372,714,26,detail,'',20,'text',bg)


def btn(p, cid, key, label, x, y, w, h, target, effect, kind='secondary', enabled=True, preview_to=None):
    spec = dict(key=key,label=label,w=w,h=h,kind=kind)
    if key in BUTTONS:
        assert BUTTONS[key] == spec, key
    BUTTONS[key] = spec
    b = dict(id=cid,key=key,label=label,x=x,y=y,w=w,h=h,kind=kind,enabled=enabled)
    p['buttons'].append(b)
    ACTIONS.append(dict(source=p['key'],source_id=p['id'],control_id=cid,button=key,label=label,
                        target=target,preview_to=preview_to or target,effect=effect))
    return b


# 0: startup, no automatic timer-based navigation.
p=page(0,'boot','无创血压',card=False)
p['base'].rect(24,96,752,304,C['surface'],12,C['border'])
p['base'].text(48,118,'设备启动',24,C['cyan'])
field(p,110,'BOOT_STATUS',48,169,690,48,'正在初始化','正在初始化',32,bg='surface')
field(p,111,'BOOT_DETAIL',48,237,690,34,'正在检查设备连接','正在检查设备连接',24,'muted','surface')
field(p,112,'VERSION',48,306,690,30,'屏幕版本：--','屏幕版本：--',20,'muted','surface')
foot(p,'尚未允许测量','amber')
p['note']='上电初始页；不放“跳过”按钮。不使用延时自动进入可测量状态。主控完成连接检查后进入 P01，异常进入 P04。'

# 1: idle.
p=page(1,'idle','无创血压')
p['base'].text(40,114,'本次会话 · 上次结果',22,C['muted'])
field(p,110,'LAST_RESULT',40,170,388,80,'-- / --','-- / --',58,'muted','data',number=True)
field(p,111,'LAST_INFO',40,269,388,34,'尚无测量结果','尚无测量结果',24,bg='data')
btn(p,10,'start','开始测量',492,112,280,112,'starting','发送 START；主控接受后进入 P02 启动中；拒绝时保持 P01 并更新提示。','primary')
btn(p,11,'history','历史',492,248,132,72,'history','发送 OPEN_HISTORY，记录返回页 P01，主控加载本会话历史后进入 P05。')
btn(p,12,'settings','设置',640,248,132,72,'settings','仅安全空闲时进入 P06；从主控读回当前亮度与时间。')
tip(p,'准备测量','请按设备说明佩戴袖带，测量时保持静止')
foot(p,'可开始 · 请核对测量对象','green')
p['note']='初始开始按钮禁用，主控明确 start_allowed 后才启用。同一会话存在上次结果时可更新 LAST_RESULT 和 LAST_INFO，必须同时带历史时间说明。'

# 2: measurement; values are native text.
p=page(2,'measuring','无创血压')
field(p,110,'MEASURE_STATUS',40,108,388,36,'测量中','启动中',24,'cyan','data')
field(p,111,'PRESSURE',40,148,250,100,'156','--',88,'cyan','data',number=True)
p['base'].text(300,201,'mmHg',24,C['cyan'])
p['base'].line(40,254,428,254)
p['base'].text(40,275,'袖带压力',22)
field(p,112,'ELAPSED',239,273,189,32,'已用时 00:28','已用时 --:--',22,'muted','data')
btn(p,10,'stop','停止测量',492,112,280,112,'stopping','发送 STOP；主控接受停止请求后进入 P08。停止不弹确认框；允许重复请求，由主控去重。','stop')
p['base'].text(492,257,'需要中止时，点击停止',22,C['muted'])
p['base'].text(492,288,'本次结果尚未生成',22,C['muted'])
tip(p,'测量进行中','请保持静止，测量时避免说话')
foot(p,'正在接收压力数据','cyan')
p['note']='P02 包含启动中、测量中、压力断流三种文案。启动时压力为 --；没有有效压力时清空显示，不能保留过时数字。'

# 3: final result.
p=page(3,'result','无创血压')
p['base'].items.append(f'<path d="M25 255H443V307Q443 319 431 319H37Q25 319 25 307Z" fill="{C["pulse"]}"/>')
p['base'].text(40,112,'收缩压 SYS',22,C['cyan'])
p['base'].text(248,112,'舒张压 DIA',22,C['cyan'])
field(p,110,'SYS',40,145,176,80,'120','--',76,'cyan','data',number=True)
field(p,111,'DIA',248,145,180,80,'80','--',76,'cyan','data',number=True)
p['base'].text(40,224,'mmHg',20,C['cyan'])
p['base'].text(248,224,'mmHg',20,C['cyan'])
p['base'].line(232,112,232,245)
p['base'].line(40,255,428,255)
p['base'].text(40,278,'脉率 PR',22,C['green'])
field(p,112,'PR',174,258,112,58,'72','--',52,'green','pulse',number=True)
p['base'].text(296,282,'次/分',22,C['green'])
btn(p,10,'remeasure','再次测量',492,112,280,112,'starting','同一会话发送 START；仅 safe_to_restart 且 start_allowed 时允许。主控接受后进入 P02。','primary')
btn(p,11,'history','历史',492,248,132,72,'history','发送 OPEN_HISTORY，记录返回页 P03；主控加载本会话记录后进入 P05。')
btn(p,12,'new_patient','换患者',640,248,132,72,'confirm','仅安全空闲时显示 P09 确认页面；此时尚不更换会话。')
tip(p,'测量完成','测量时间 09:41 · 本次会话第 5 次测量','green')
foot(p,'结果来自本次测量','green')
p['note']='SYS/DIA/PR、会话、时间、有效标识一起更新后才显示“测量完成”。绿色表示流程完成，不代表血压正常。字体若不支持目标尺寸，用本机可用的 64 px / 32 px 字库完成首轮联调。'

# 4: notice, retry only if enabled by MCU; locked variant reuses same controls.
p=page(4,'notice','无创血压',card_color='surface')
field(p,110,'ERROR_CODE',40,109,388,32,'E01','--',22,'amber','surface')
field(p,111,'ERROR_TITLE',40,158,388,48,'压力未建立','等待状态确认',32,bg='surface')
field(p,112,'ERROR_LINE1',40,216,388,34,'请检查袖带佩戴','',24,bg='surface')
field(p,113,'ERROR_LINE2',40,251,388,32,'和气管连接','',24,bg='surface')
field(p,114,'RESULT_VALIDITY',40,289,388,27,'本次未获得有效结果','本次未获得有效结果',20,'muted','surface')
btn(p,10,'retry','重新测量',492,112,280,112,'starting','仅 retry_allowed 且 safe_to_restart 时发送 START；主控接受后进入 P02。','primary')
btn(p,11,'back_wide','返回',492,248,280,72,'idle','仅安全空闲时进入 P01；故障未解除时保持禁止开始，不通过返回按钮绕过保护。')
tip(p,'检查完成后再重试','若问题持续，请联系维护人员','amber')
foot(p,'设备允许重试 · 请先检查','amber')
p['note']='该图是允许重试的 E01 示例。通信故障或停止状态不明时，ID10 更换为 stop 图片并解释为 STOP，ID11 隐藏且禁用；见锁定变体。错误信息由主控填入，不把 E01 烘焙到背景。'

# 5: history. Same page is used for all pages of records.
p=page(5,'history','测量历史',card=False)
p['base'].rect(24,96,752,216,C['surface'],12,C['border'])
cols=[(40,52,'序号'),(104,132,'时间'),(252,96,'SYS'),(362,88,'DIA'),(468,96,'PR'),(588,172,'状态')]
for x,w,title in cols: p['base'].text(x,106,title,20,C['muted'])
p['base'].line(40,137,760,137)
samples=[['05','09:41','120','80','72','有效'],['04','09:37','--','--','--','已取消'],['03','09:33','--','--','--','E01'],['02','09:28','122','82','75','有效']]
for row,values in enumerate(samples):
    y=145+row*41
    if row: p['base'].line(40,y-5,760,y-5)
    for col,((x,w,label),value) in enumerate(zip(cols,values)):
        color='cyan' if col in (2,3) else ('green' if col==4 else ('muted' if col<2 else 'text'))
        field(p,110+row*6+col,f'ROW{row+1}_{["NO","TIME","SYS","DIA","PR","STATE"][col]}',x,y,w,32,value,'',22,color,'surface',number=col in (2,3,4))
btn(p,10,'previous','上一页',24,330,160,64,'history_first','请求前一组 4 条；页号及四行一次更新。已是第 1 页时禁用该按钮。',enabled=False)
btn(p,11,'next','下一页',200,330,160,64,'history_second','请求后一组 4 条；到末页后禁用下一页。')
btn(p,12,'back_160','返回',612,330,160,64,'result','返回 OPEN_HISTORY 保存的来源页 P01 或 P03；不得固定返回 P03 显示旧结果。')
field(p,140,'PAGE_INDEX',384,346,200,30,'第 1 / 2 页','暂无记录',22,'muted',align='center')
field(p,141,'CACHE_NOTE',24,399,752,24,'本次会话记录 · 断电不保留','本次会话记录 · 断电不保留',18,'muted')
foot(p,'单位：SYS / DIA 为 mmHg；PR 为次/分')
p['note']='示例包含有效结果、取消和错误事件。只展示当前会话，最多缓存 100 条事件、每页 4 条。缓存由主控实现，屏幕本身不会因为有历史页就自动保存记录。'

# 6: settings.
p=page(6,'settings','设备设置',card=False)
p['base'].rect(24,96,752,224,C['surface'],12,C['border'])
p['base'].text(40,124,'屏幕亮度',24)
field(p,110,'BRIGHTNESS',324,118,140,44,'70%','--',32,'cyan','surface',align='center',number=True)
btn(p,10,'brightness_minus','降低',492,110,124,64,'brightness_lower','请求亮度减 10 个百分点；建议交互范围 30%～100%，最终范围由实屏确认。主控应用成功后回写读数。')
btn(p,11,'brightness_plus','提高',648,110,124,64,'brightness_higher','请求亮度增 10 个百分点；达到上限后禁用提高。百分比到屏幕指令值的转换由主控实现。')
p['base'].line(40,190,760,190)
p['base'].text(40,211,'设备时间',24)
field(p,111,'DEVICE_TIME',40,254,430,32,'2026-09-21 09:41','时间未校准',22,'muted','surface')
btn(p,12,'set_time','时间设置',492,220,280,64,'time','仅空闲时进入 P10；复制当前设备时间作为编辑草稿，不立即改系统时间。')
btn(p,13,'maintenance','维护',24,336,200,64,'maintenance','进入 P07 的访问检查页；没有主控授权时只显示未授权说明，不提供维护命令。')
btn(p,14,'back_200','返回',572,336,200,64,'idle','返回 P01，主控回写当前会话和 start_allowed。')
p['base'].text(250,355,'仅空闲时可调整',22,C['muted'])
foot(p,'设置仅在空闲时可用')
p['note']='本包包含亮度调整和时间编辑的外观与控件表；实际亮度/RTC 指令须按实物型号验证。亮度按成功读回的值显示，时间未经校准仍写“时间未校准”。'

# 7: maintenance access, one safe entry and one return.
p=page(7,'maintenance','维护信息',card=False)
p['base'].rect(24,96,752,224,C['surface'],12,C['border'])
p['base'].text(40,112,'维护访问',22,C['amber'])
field(p,110,'ACCESS_STATUS',40,153,712,46,'权限未授予','权限未授予',32,bg='surface')
field(p,111,'MAINT_LINE1',40,213,712,34,'请由授权维护端启用访问权限','请由授权维护端启用访问权限',24,'muted','surface')
field(p,112,'MAINT_LINE2',40,261,712,34,'当前仅显示访问说明','当前仅显示访问说明',22,'muted','surface')
btn(p,10,'back_280','返回',492,336,280,64,'settings','返回 P06；退出后是否撤销维护授权由主控权限策略决定。')
p['base'].text(24,348,'维护请联系设备管理员',22,C['muted'])
foot(p,'需要授权才能查看维护信息','amber')
p['note']='本包只提供维护入口和只读信息版式，不虚构默认密码。真正权限在主控校验；不要用长按标题、隐藏按钮或固定密码代替权限。授权后的版本/通信信息见同页变体。'

# 8: stopping (retains working stop request).
p=page(8,'stopping','无创血压')
field(p,110,'STOP_STATUS',40,113,388,44,'正在停止','正在停止',32,'amber','data')
field(p,111,'PRESSURE',40,163,250,88,'--','--',76,'cyan','data',number=True)
p['base'].text(300,207,'mmHg',24,C['cyan'])
field(p,112,'STOP_DETAIL',40,267,388,34,'正在确认停止状态','正在确认停止状态',24,'muted','data')
btn(p,10,'stop','停止测量',492,112,280,112,'stopping','再次发送 STOP。保持 P08，直到主控确认允许恢复；不得仅凭按钮弹起或停止 ACK 进入待机。','stop')
p['base'].text(492,257,'停止请求已提交',22,C['muted'])
p['base'].text(492,288,'暂不可开始新测量',22,C['muted'])
tip(p,'正在结束本次测量','停止状态未确认前，请勿开始下一次测量','amber')
foot(p,'结果已取消 · 等待状态确认','amber')
p['note']='主控确认取消且允许重新测量后回 P01；无法确认时进入 P04 锁定变体。停止 ACK 不等于已泄压。此页仍能发送停止请求。'

# 9: patient/session confirmation.
p=page(9,'confirm','切换测量会话',card=False)
p['base'].rect(24,96,752,224,C['surface'],12,C['border'])
p['base'].text(40,114,'开始新的测量会话？',30,bold=True)
p['base'].text(40,179,'首页将不再显示上一会话结果。',24)
p['base'].text(40,220,'请核对下一位测量对象后继续。',24,C['muted'])
field(p,110,'CURRENT_SESSION',40,268,712,30,'当前：会话 001 · 未关联患者','当前会话：待主控提供',22,'muted','surface')
btn(p,10,'cancel_280','取消',24,336,280,64,'result','不创建新会话，返回来源结果页 P03；原结果继续显示。')
btn(p,11,'confirm_session','确认换患者',492,336,280,64,'idle_new','发送 NEW_SESSION；主控校验安全空闲、结束旧事务、建立新会话并清空当前显示后进入 P01。','primary')
foot(p,'不会立即删除旧会话缓存')
p['note']='采用完整页面实现确认框，不依赖子画面弹窗或透明层。未接入患者系统时“换患者”表示新建测量会话，不代表完成真实患者身份绑定。'

# 10: time editing without system keyboard (DN050 metadata says no keyboard).
p=page(10,'time','时间设置',card=False)
p['base'].rect(24,96,752,160,C['surface'],12,C['border'])
for i,(name,label,x,value) in enumerate([('YEAR','年',40,'2026'),('MONTH','月',184,'09'),('DAY','日',328,'21'),('HOUR','时',496,'09'),('MINUTE','分',640,'41')]):
    p['base'].text(x,110,label,22,C['muted'])
    # All five selectors share a blank tile; their values are native text.
    b=btn(p,10+i,'time_field','',x,151,120,72,'time_'+name.lower(),f'选择“{label}”字段，保持 P10；主控/已验证屏端逻辑回写高亮和编辑字段名。')
    ACTIONS[-1]['label']='选择'+label
    field(p,110+i,name,x+4,169,112,40,value,'--',32,'text','button',align='center',number=True)
p['selected']=10
btn(p,15,'time_add','＋',24,272,160,64,'time_increment','将当前选中字段加 1，示例为年份 2026→2027；只改草稿。调整月份/年份时重新校验日期。')
btn(p,16,'time_subtract','－',200,272,160,64,'time_decrement','将当前选中字段减 1，示例为年份 2026→2025；只改草稿。边界由实际 RTC 能力限定。')
btn(p,17,'cancel_small','取消',492,272,132,64,'settings','丢弃编辑草稿，返回 P06，设备时间不变。')
btn(p,18,'save_time','保存',640,272,132,64,'time_saved','发送 SET_TIME 草稿；主控校验日期且 RTC 写入并读回成功后返回 P06；失败留在 P10 显示原因。','primary')
field(p,120,'EDIT_FIELD',24,353,752,30,'正在编辑：年','正在编辑：年',22,'cyan')
field(p,121,'EDIT_HELP',24,389,752,26,'先选择字段，再按 ＋ / －；保存后才更新设备时间','选择字段，再调整数值',20,'muted')
foot(p,'编辑中的时间尚未保存')
p['note']='5 个选择框实际是按钮＋原生文本叠放，按钮图片无数字。文本用“用户主机输入”，不启用键盘。本机 DN050 配置不支持系统键盘，因此此页采用选择字段和加减按钮。RTC 范围、闰年和整机时间源由主控处理。'


# Runtime visual variants; these are NOT additional VisualTFT pages.
VARIANTS = {
    'starting':dict(page='measuring',values={'MEASURE_STATUS':'启动中','PRESSURE':'--','ELAPSED':'已用时 00:00','TIP_TITLE':'正在启动测量','FOOTER':'等待有效压力数据'}),
    'pressure_stale':dict(page='measuring',values={'MEASURE_STATUS':'压力数据中断','PRESSURE':'--','TIP_TITLE':'过程数据未更新','TIP_DETAIL':'请按设备说明处理；仍可点击停止','FOOTER':'正在确认设备状态'}),
    'idle_new':dict(page='idle',values={'CONTEXT':'会话 002 · 未关联患者','LAST_RESULT':'-- / --','LAST_INFO':'尚无测量结果'}),
    'idle_cancelled':dict(page='idle',values={'TIP_TITLE':'本次测量已取消','TIP_DETAIL':'再次测量前，请核对测量对象','FOOTER':'可开始 · 无本次测量结果'}),
    'idle_blocked':dict(page='idle',values={'FOOTER':'暂不可测量 · 等待设备就绪','TIP_TITLE':'正在连接','TIP_DETAIL':'连接完成后才能开始测量'},disabled=[10]),
    'notice_locked':dict(page='notice',values={'ERROR_CODE':'通信故障','ERROR_TITLE':'停止状态未确认','ERROR_LINE1':'请按设备说明处理','ERROR_LINE2':'联系维护人员','TIP_TITLE':'暂不允许重新测量','TIP_DETAIL':'请按设备说明处理，必要时联系维护人员','FOOTER':'状态未确认 · 开始与重试已关闭'},button_overrides={10:'stop'},hidden=[11]),
    'history_first':dict(page='history',values={},disabled=[10]),
    'history_second':dict(page='history',values={'PAGE_INDEX':'第 2 / 2 页',**{f'ROW{r}_{n}':('01' if n=='NO' else '09:24' if n=='TIME' else '118' if n=='SYS' else '78' if n=='DIA' else '71' if n=='PR' else '有效') if r==1 else '' for r in range(1,5) for n in ('NO','TIME','SYS','DIA','PR','STATE')}},disabled=[11],enabled=[10]),
    'history_empty':dict(page='history',values={'PAGE_INDEX':'暂无记录','CONTEXT':'会话 002 · 未关联患者',**{f'ROW{r}_{n}':'' for r in range(1,5) for n in ('NO','TIME','SYS','DIA','PR','STATE')}},disabled=[10,11]),
    'brightness_lower':dict(page='settings',values={'BRIGHTNESS':'60%','FOOTER':'亮度已应用 · 60%'}),
    'brightness_higher':dict(page='settings',values={'BRIGHTNESS':'80%','FOOTER':'亮度已应用 · 80%'}),
    'maintenance_granted':dict(page='maintenance',values={'ACCESS_STATUS':'授权访问 · 只读信息','MAINT_LINE1':'主控版本：Vx.x  ·  屏幕版本：Vx.x','MAINT_LINE2':'模块通信：已连接','FOOTER':'维护访问已授权'}),
    'time_increment':dict(page='time',values={'YEAR':'2027','FOOTER':'编辑中的时间尚未保存'}),
    'time_decrement':dict(page='time',values={'YEAR':'2025','FOOTER':'编辑中的时间尚未保存'}),
    'time_saved':dict(page='settings',values={'CLOCK':'2027-09-21 09:41','DEVICE_TIME':'2027-09-21 09:41','FOOTER':'设备时间已校准'}),
    'time_failed':dict(page='time',values={'FOOTER':'时间保存失败 · 设备时间未更新','EDIT_HELP':'请检查日期或时间服务连接，再重试保存'}),
}
for i,(name,label) in enumerate([('year','年'),('month','月'),('day','日'),('hour','时'),('minute','分')]):
    VARIANTS['time_'+name] = dict(page='time',values={'EDIT_FIELD':'正在编辑：'+label},selected=10+i)

# History previous button must show an enabled source on page 2.
for a in ACTIONS:
    if a['source']=='history' and a['control_id']==10: a['source_variant']='history_second'
    if a['source']=='time' and a['control_id']==18: a['source_variant']='time_increment'
ACTIONS.append(dict(source='notice',source_id=4,source_variant='notice_locked',control_id=10,button='stop',label='停止测量（故障锁定时）',target='stopping',preview_to='stopping',effect='P04 状态不明时 ID10 绑定 STOP；不得继续解释为重试。主控接受后显示 P08；无应答时保持锁定。'))
ACTIONS.append(dict(source='history',source_id=5,control_id=12,button='back_160',label='返回（从待机进入历史）',target='idle',preview_to='idle',effect='当 history_origin=P01 时返回 P01；这里补充另一条返回路径。'))


def button_canvas(spec, state='up', selected=False):
    s=SVG(spec['w'],spec['h'])
    s.rect(0,0,s.w,s.h,C['surface'] if spec['key'] in ('brightness_minus','brightness_plus','set_time','time_field') else C['background'])
    bg=C['pressed'] if state=='down' else C['button']
    edge=C['red'] if spec['kind']=='stop' else C['cyan'] if spec['kind']=='primary' or selected else C['button_border']
    fg=C['red'] if spec['kind']=='stop' and state!='down' else C['text']
    if state=='disabled': bg,edge,fg=C['inset'],C['border'],C['muted']
    if spec['key']=='time_field': s.rect(0,0,s.w,s.h,C['surface'])
    s.rect(1,1,s.w-2,s.h-2,bg,12,edge,2)
    size=28 if s.w>=240 else 24
    s.text(s.w/2,(s.h-size)/2-1,spec['label'],size,fg,True,'middle')
    return s


def resolve(key):
    if key in PAGES: return PAGES[key],{}
    variant=VARIANTS[key]
    return PAGES[variant['page']],variant


def preview(key, pressed=None, annotate=False):
    p,v=resolve(key)
    s=SVG(); s.group(p['base'])
    selected=v.get('selected',p['selected'])
    hidden=v.get('hidden',[])
    for b in p['buttons']:
        if b['id'] in hidden: continue
        bk=v.get('button_overrides',{}).get(b['id'],b['key'])
        enabled=(b['enabled'] or b['id'] in v.get('enabled',[])) and b['id'] not in v.get('disabled',[])
        state='down' if b['id']==pressed else 'up' if enabled else 'disabled'
        s.group(button_canvas(BUTTONS[bk],state,b['id']==selected),b['x'],b['y'])
    for f in p['fields']:
        val=v.get('values',{}).get(f['name'],f['sample'])
        # Native numeric overlays on selectors follow the button fill during press.
        if f['name'] in ('YEAR','MONTH','DAY','HOUR','MINUTE'):
            bid=f['id']-100
            if bid==pressed:
                s.rect(f['x'],f['y'],f['w'],f['h'],C['pressed'])
        anchor={'left':'start','center':'middle','right':'end'}[f['align']]
        x=f['x']+(f['w']/2 if anchor=='middle' else f['w'] if anchor=='end' else 0)
        s.text(x,f['y'],val,f['size'],C[f['color']],number=f['number'],align=anchor)
    s.text(774,451,'示例数据 · UI 预览',14,C['muted'],align='end')
    if annotate:
        for item,kind in [(f,'T') for f in p['fields']]+[(b,'B') for b in p['buttons']]:
            s.rect(item['x'],item['y'],item['w'],item['h'],'none',0,C['amber'],1)
            text=f'{kind}{item["id"]}'
            s.rect(item['x'],item['y'],len(text)*9+6,18,C['inset'])
            s.text(item['x']+3,item['y']+1,text,14,C['amber'])
    return s


for p in PAGES.values():
    export(p['base'],p['background'])
    export(preview(p['key']),f'previews/p{p["id"]:02d}_{p["key"]}.png')
    export(preview(p['key'],annotate=True),f'previews/p{p["id"]:02d}_{p["key"]}_controls.png')
for key in VARIANTS: export(preview(key),'previews/'+key+'.png')
for spec in BUTTONS.values():
    for state in ('up','down','disabled'):
        export(button_canvas(spec,state),f'assets/buttons/{spec["key"]}_{state}.png')
export(button_canvas(BUTTONS['time_field'],selected=True),'assets/buttons/time_field_selected.png')


def preview_path(key):
    p,_=resolve(key)
    return f'previews/p{p["id"]:02d}_{key}.png' if key in PAGES else f'previews/{key}.png'


for i,a in enumerate(ACTIONS,1):
    source=a.get('source_variant',a['source'])
    dst=a['preview_to']
    a['sequence']=i
    a['pressed_image']=f'transitions/a{i:02d}_pressed.png'
    a['destination_image']=preview_path(dst)
    a['pair_image']=f'transitions/a{i:02d}_pair.png'
    export(preview(source,pressed=a['control_id']),a['pressed_image'])
    pair=SVG(1648,556); pair.rect(0,0,1648,556,C['background'])
    pair.text(16,15,f'{i:02d} · P{a["source_id"]:02d} / ID {a["control_id"]}  按下「{a["label"]}」',22)
    pair.text(832,15,'处理完成后的目标画面（示例）',22,C['cyan'])
    pair.group(preview(source,pressed=a['control_id']),16,60)
    pair.group(preview(dst),832,60)
    export(pair,a['pair_image'])


overview=SVG(1648,6*548+88)
overview.rect(0,0,overview.w,overview.h,C['background'])
overview.text(24,20,'VisualTFT 血压界面 · 页面索引 / 800 × 480',30,bold=True)
for i,p in enumerate(PAGES.values()):
    x=16+(i%2)*816; y=82+(i//2)*548
    overview.text(x,y,f'P{p["id"]:02d}  {p["title"]} / {p["key"]}',22,C['muted'])
    overview.group(preview(p['key']),x,y+42)
export(overview,'previews/all_pages.png')

# Compact front-page overview of the four already approved screens.
core=SVG(1648,1100);core.rect(0,0,1648,1100,C['background'])
for i,key in enumerate(('idle','measuring','result','notice')):
    x=16+(i%2)*816;y=16+(i//2)*548
    p=PAGES[key];core.text(x,y,f'P{p["id"]:02d} · {p["title"]} / {key}',22,C['muted'])
    core.group(preview(key),x,y+42)
export(core,'previews/core_pages.png')

manifest={
    'design':'N050 capacitive 800x480 BP / SPO2_Hospital palette',
    'generated_at':'2026-09-21','hardware_model_verified':False,
    'colors':C,'pages':[{k:v for k,v in p.items() if k!='base'} for p in PAGES.values()],
    'variants':VARIANTS,'buttons':list(BUTTONS.values()),'actions':ACTIONS,'renders':RENDERS,
}
(ROOT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')

with (ROOT/'控件配置表.csv').open('w',encoding='utf-8-sig',newline='') as f:
    writer=csv.writer(f);writer.writerow(['页面ID','页面','控件ID','类型','名称','X','Y','宽','高','目标字号px','文字颜色','纯色背景','初始值/图片'])
    for p in PAGES.values():
        for b in p['buttons']:
            writer.writerow([p['id'],p['key'],b['id'],'按钮',b['label'] or '时间字段选择',b['x'],b['y'],b['w'],b['h'],'','','',f'assets/buttons/{b["key"]}_up.png'])
        for v in p['fields']:
            writer.writerow([p['id'],p['key'],v['id'],'文本',v['name'],v['x'],v['y'],v['w'],v['h'],v['size'],C[v['color']],C[v['bg']],v['default']])


md=[]
def line(s=''): md.append(s)
def image(alt,path): line(f'![{alt}]({path})\n')
def table(headers,rows):
    line('| '+' | '.join(headers)+' |');line('| '+' | '.join('---' for _ in headers)+' |')
    for row in rows: line('| '+' | '.join(str(v).replace('|','／').replace('\n','<br>') for v in row)+' |')
    line()


line('# VisualTFT 医院版血压界面绘制指南')
line('版本 1.0 · 2026-09-21 · 800×480 横屏 · N050 类电容触摸 · 配色参考 `F:\\VisualTFT\\SPO2_Hospital`\n')
line('本指南把已经确认的深蓝血压界面拆成可以手工导入 VisualTFT 的背景图、按钮图片和动态文本，并补齐所有按钮按下时的样子、点击后目标页面、配置步骤与控件表。**PNG 图片可直接用于绘图；本包不是可直接烧录的 `.tftprj` 工程，也不包含测压固件。**\n')
image('四个核心页面概览','previews/core_pages.png')
line('图中 120/80、72、156、日期、会话号均为排版演示。`assets/backgrounds/` 不含这些演示值，主控数据应通过文本控件显示。补充页面沿用主界面配色和触控尺寸；未改变已经确认的测量流程。\n')
line('## 1. 先看这三种图片的区别\n')
table(['文件夹','用途','能否设为屏幕背景'],[
    ['assets/backgrounds/','11 张 800×480 静态底图；留出按钮、读数和状态文本位置','可以，按页面表逐一选取'],
    ['assets/buttons/','按钮常态 up、按下 down、禁用 disabled；另有时间字段 selected','放到按钮/图标属性，不是整页背景'],
    ['previews/','11 页效果图、控件标注图，以及不同状态的示例','不可以：含演示数据和已绘制的按钮'],
    ['transitions/','每个按钮的按下整页图和目标画面对照','不可以：只用于照着配置、核对动作'],
    ['sources/','所有图片的 SVG 源文件，可修改和重新导出','VisualTFT 优先使用已导出的 PNG'],
    ['控件配置表.csv / manifest.json','坐标、控件 ID、状态与资源清单','查表使用'],
    ['阅读版.html','浏览器离线查看本指南、图片和目录','仅供阅读']])
line('所有素材放在同一目录内；复制或解压时请保留文件夹结构。Markdown 内图片使用相对路径，方便整包移动。本文“目标页面”均指对应主控事件被接受后的显示结果，不意味着仅设置一个跳页属性就能启动或停止血压模块。\n')
line('## 2. 建立工程：按这个顺序操作\n')
line('1. 打开 VisualTFT，选择 **文件 → 新建工程**，保存到一个新的血压工程目录。按屏幕背面完整料号选择硬件、800×480 横屏和电容触摸。\n2. 用户目前只确认“N050 类”。本机 `DeviceInfo.xml` 有 `DC80480DN050 / TypeID 21011 / 5.0 英寸 / 800×480`，这只是可选型号线索；须与实物标签一致。不要因为编译方便而改成其他 7 英寸型号。\n3. 按下一节顺序使用 **文件 → 新建画面** 建立 P00～P10，并重命名。把上电初始画面设为 P00。\n4. 双击画面空白处，打开画面属性；背景图片选本页的 `assets/backgrounds/pXX_*.png`，显示尺寸 800×480，坐标 0,0，不拉伸、不偏移。\n5. 先添加并配置按钮，再添加动态文本。按表输入 X、Y、宽、高，不用鼠标目测位置。打开工具栏“显示控件 ID”核对。\n6. 设置按钮的弹起、按下图片，并按第 4 节填写公共属性。数字、时间和状态文字按第 5 节添加。\n7. 如果 VisualTFT 自动分配的画面或控件 ID 不能修改，保留实际 ID，并在 CSV 的副本中记录映射；联调代码必须使用实际导出的 ID。\n8. 先运行虚拟屏检查排版、按下反馈和事件，再连接主控验证业务跳转。最后按真实型号编译、下载并检查实屏字体与触摸。\n')
line('### 页面清单\n')
table(['建议画面 ID','画面名','实际使用'],[[p['id'],p['key'],p['title']] for p in PAGES.values()])
line('P00～P07 沿用原方案的建议页号；新增 P08 正在停止、P09 换患者确认、P10 时间编辑。`starting`、`notice_locked`、历史翻页等是同一个画面的内容/按钮状态变化，**不额外创建画面**。\n')
line('## 3. 配色和字体配置\n')
table(['用途','HEX','VisualTFT RGB（R;G;B）'],[[key,val,';'.join(str(int(val[i:i+2],16)) for i in (1,3,5))] for key,val in C.items()])
line('背景图已经包含静态中文、分区、单位和边框，按钮 PNG 已包含按钮文字。动态读数和提示要配置屏幕字库。效果图使用微软雅黑和等宽数字；表中的字号是目标像素高度，不是屏幕字库 ID。\n')
line('**先检查“字体资源/字库选择”实际提供的字体。** 本地血氧工程的现有字库包括 ASCII 12×24、16×32、32×64 和 GBK/GB2312 的 24×24、32×32、64×64 等；它们的 ID 只属于那个工程，不应照搬。基础/N 系列不应默认具备物联/F 系列的任意矢量字库能力。\n')
table(['内容','效果图目标','首轮可用字库方案'],[
    ['压力','88 px','ASCII 32×64，高 64；保持文本框坐标不变并垂直居中'],
    ['SYS / DIA','76 px','ASCII 32×64，高 64；三个数字宽 96，可容纳于本表文本框'],
    ['PR','52 px','ASCII 16×32，高 32；若实物支持定制数字字库，再替换为目标高度'],
    ['动态中文正文','22 / 24 px','优先 24×24；必要时把辅助说明改为 16×16，必须实屏确认可读性'],
    ['动态时间/页码','20 / 22 px','16 或 24 像素字库；24 像素时检查长文本宽度，可使用短日期格式'],
    ['按钮、静态标签','PNG 内的 20～30 px','无需再叠加同样的文字控件']])
line('若使用 24×24 中文，超出文本框时优先缩短文案，不要任意缩小重要提示。例如页脚改为“等待主控确认”，日期改为“09-21 09:41”。空值统一 `--`，不显示 0/0 作为无结果。正式字库需要包含 `--`、百分号、斜杠、冒号、所有中文状态文案。\n')
line('## 4. 按钮的公共属性与事件配置\n')
table(['VisualTFT 属性','本包建议填写'],[
    ['控件类型','按钮'],['X / Y / 宽 / 高','按各页按钮表'],
    ['弹起时的图片','assets/buttons/名称_up.png'],['按下时的图片','assets/buttons/名称_down.png'],
    ['裁剪 / 图片裁剪','否。这里每张图片就是按钮自身尺寸，不是从整屏背景裁切'],
    ['文字状态','否。PNG 已含文字；时间字段按钮是例外，使用单独动态文本覆盖'],
    ['触控用途','开关描述'],['操作风格','瞬变；一次按下后弹回，不用长按连发或自锁'],
    ['初始状态','弹起'],['事件通知','是/开启；若软件可选择事件阶段，按下通知即可'],
    ['对外指令','留空，使用系统按钮事件通知；不要再发送第二套自定义命令'],
    ['对内指令','正式业务工程留空，页面和数据由主控状态统一回写'],
    ['触控范围','与按钮矩形一致；不扩张到相邻按钮']])
line('本包统一推荐按钮先上报事件、主控处理后再切页，方便“历史返回到来源页”、动态设置和测量状态一致。若只做无硬件外观演示，可在工程副本中把纯导航按钮设为“触控用途 → 切换画面 → 目标画面”；这是演示配置，不代替 START/STOP/NEW_SESSION/SET_TIME 等设备动作。\n')
line('### 先亲手做一个“开始测量”按钮\n')
line('1. 打开 P01，双击空白处把背景图片选为 `assets/backgrounds/p01_idle.png`。\n2. 从工具栏放入一个按钮，在属性窗口填 `X=492、Y=112、宽=280、高=112`；本指南建议 ID 为 10。\n3. “弹起时的图片”选 `assets/buttons/start_up.png`；“按下时的图片”选 `assets/buttons/start_down.png`。\n4. “裁剪”选否，“文字状态”选否，“触控用途”选开关描述，“操作风格”选瞬变，“初始状态”选弹起，“事件通知”开启。\n5. 本按钮不填直接切换 P02 的对内指令。运行虚拟屏，按下应看到蓝灰按下效果，串口/指令窗口出现该按钮的按下通知。\n6. 主控识别 P01 / B10 并允许开始后，先给 P02 的文本控件写入“启动中”、压力 `--` 和已用时 `00:00`，再切换至 P02。其余按钮照各页控件表和第 8 节动作说明配置。\n')
line('本地指令集 V5.1 的“开关描述”按钮通知子类型为 `01`，状态 `01` 是按下、`00` 是弹起。业务处理只响应一次按下事件，不能两次都执行开始。**“切换画面”按钮子类型为 `00`，其状态位是保留字段，不能套用按下/弹起判断。** 最终以该屏固件、指令助手和串口抓包为准；本文不要求手填猜测的帧字节。停止不使用与开始相同的防重复锁，连续停止请求交给主控安全处理。\n')
line('### 禁用样式不等于不能点击\n')
line('`disabled.png` 只是外观。必须同时把控件置为不可操作，并由主控再次拒绝越权动作。软件若没有单独“禁用图片”属性，可在禁用时隐藏并禁用按钮，在同一位置显示使用 disabled 图片的图标/图片控件；恢复时隐藏该图片控件、启用并显示按钮。具体可用的显示、使能和图片更新命令用本机指令助手确认。不要只换一张灰色图片而保留有效触控。\n')
line('默认：P01 开始、P03 再测、P04 重试都等主控许可；P02/P08 的停止保持可操作。历史首/末页禁用对应翻页按钮，时间未校准也不能伪造日期。\n')
line('## 5. 动态文本的公共属性\n')
table(['属性','填写方法'],[
    ['控件类型','文本控件；数值也是文本，由主控格式化后发送'],
    ['输入方式','用户主机输入；关闭系统键盘/用户触控输入'],
    ['文本初值','使用每页表中的安全初值，不填演示测量数字'],
    ['背景模式','纯色/选定背景色；按表中 BG 精确填写，避免旧数字残影'],
    ['文字颜色','按表中 FG 填写；血压青色，PR 绿色，正文白色'],
    ['对齐','默认左对齐；CLOCK 右对齐，亮度/页码/时间字段居中'],
    ['自动换行','本表单行字段关闭换行；异常正文拆成两行控件'],
    ['刷新','同一框整体覆盖；保持单位和卡片边框不被文本背景擦除']])
line('P10 时间字段的文本在按钮上层，选择操作应由按钮接收；文本设置为仅主机输入，实机确认它不会截获触摸。按下时文本框背景同步为 pressed 色、弹起恢复 button 色；也可使用该机已验证可正确擦除的透明文本模式。若该固件的文本层会拦截触控，改用一个可动态更新文字的按钮承载数值，仍沿用同一控件坐标和事件 ID。不能留下只能点数字边缘才响应的热区。\n')
line('## 6. 按页绘制：背景、控件、坐标\n')
line('坐标格式统一为 `X / Y / W / H`，单位像素。B 表示按钮，T 表示文本；它们在同页中的 ID 不重复。每页先放背景，再放按钮，最后放动态文字。标题/单位等已经在背景内的文字不用重复添加。\n')

for p in PAGES.values():
    line(f'### P{p["id"]:02d} · {p["title"]}（{p["key"]}）\n')
    line(p['note']+'\n')
    line('**最终效果（含演示数据）**\n');image(p['title']+'效果',preview_path(p['key']))
    line('**真正导入的背景图**：`'+p['background']+'`\n');image(p['title']+'背景',p['background'])
    line(f'控件位置图：[查看 B/T 编号标注](previews/p{p["id"]:02d}_{p["key"]}_controls.png)。\n')
    if p['buttons']:
        table(['按钮 ID','标签','X / Y / W / H','弹起 / 按下图片'],[[b['id'],b['label'] or '时间字段选择',f'{b["x"]} / {b["y"]} / {b["w"]} / {b["h"]}',f'{b["key"]}_up.png / {b["key"]}_down.png'] for b in p['buttons']])
    else: line('本页没有可点击按钮，由主控状态驱动画面。\n')
    table(['文本 ID / 名称','X / Y / W / H','字号目标 / 对齐','FG / BG','安全初值'],[[f'{v["id"]} {v["name"]}',f'{v["x"]} / {v["y"]} / {v["w"]} / {v["h"]}',f'{v["size"]} / {v["align"]}',f'{C[v["color"]]} / {C[v["bg"]]}',v['default'] or '空字符串'] for v in p['fields']])

line('## 7. 所有按钮素材：常态、按下、禁用\n')
line('以下 PNG 已按实际控件尺寸导出，无需裁切。三态图片尺寸完全一致。按钮中文已烘焙进 PNG，避免在 VisualTFT 中重复显示两层文字。\n')
for b in BUTTONS.values():
    line(f'### {b["label"] or "时间字段空白选择框"} · `{b["key"]}` · {b["w"]}×{b["h"]}\n')
    table(['弹起（up）','按下（down）','禁用（disabled）'],[[f'![{b["key"]} {state}](assets/buttons/{b["key"]}_{state}.png)' for state in ('up','down','disabled')]])
    line('文件：'+ '、'.join(f'`assets/buttons/{b["key"]}_{state}.png`' for state in ('up','down','disabled'))+'。\n')
image('时间字段选中状态','assets/buttons/time_field_selected.png')
line('时间字段的 selected 是“正在编辑这个字段”的持久高亮，down 是手指正在按下的短暂反馈。图中没有年份/月日数字，数字由 P10 的文本控件显示。\n')

line('## 8. 每个按钮按下后显示什么\n')
line('每组图左边是按钮按下瞬间，右边是目标页面。右边图上的读数只用于说明排版；实际必须等待主控确认和真实数据。图片宽度较大，可以点开原图看 800×480 像素的单页。\n')
table(['序号','来源页 / 控件','操作','目标逻辑状态'],[[a['sequence'],f'P{a["source_id"]:02d} / B{a["control_id"]}',a['label'],a['target']] for a in ACTIONS])
for a in ACTIONS:
    line(f'### {a["sequence"]:02d}. P{a["source_id"]:02d} / B{a["control_id"]} · {a["label"]}\n')
    line(a['effect']+'\n')
    image(a['label']+'按下及目标画面',a['pair_image'])
    line(f'[按下状态原图]({a["pressed_image"]}) · [目标页面原图]({a["destination_image"]})\n')

line('## 9. 不是按钮触发，但必须配置的状态\n')
table(['触发条件','页面行为'],[
    ['上电主控尚未就绪','保持 P00；P01 开始保持禁用'],
    ['启动事件被接受','P02：启动中、压力 --、已用时 00:00、停止可用'],
    ['接收到有效过程压力','P02：测量中、刷新真实袖带压力'],
    ['完整有效结果','一次更新结果字段与会话、时间后进入 P03'],
    ['用户停止且主控确认可重新测量','P01：本次测量已取消；没有本次结果'],
    ['压力数据过时','P02：压力变 --；提示压力数据中断并保留停止'],
    ['主控/模块通信故障或安全状态未确认','P04 锁定：停止入口保留，重试关闭，返回隐藏且禁用'],
    ['历史为空','P05：四行清空，暂无记录，上/下页均禁用'],
    ['维护权限确认','P07 更新为只读版本与通信信息，不自动执行校准'],
    ['RTC 写入失败','保持 P10，说明保存失败，不改变已有结果的原始时间戳']])
for key in ('idle_blocked','starting','pressure_stale','idle_cancelled','notice_locked','history_empty','maintenance_granted','time_failed'):
    line('### 状态：'+key+'\n'); image(key,preview_path(key))
line('P04 的 ID10 在两种模式下语义不同：允许重试时为 START，锁定时为 STOP。主控必须依据当前真实状态解析；切换图片不能独立改变动作。若不便动态更换按钮图片，可在同一位置叠放两个不同 ID 的按钮，由主控互斥显示并互斥使能，并同步修改映射表。\n')
line('## 10. 页面跳转和主控数据怎么接\n')
line('建议先照图绘完所有画面，再让主控提供下面这些语义事件和显示字段。它们是接口名称建议，不是可以直接发送的十六进制协议。屏幕串口波特率、帧头、文本编码和发送函数都按实物型号/固件以及现有驱动确认。\n')
table(['操作','主控职责','屏幕回写'],[
    ['START','检查当前会话、启动许可和上一测量已终止；去重连点','被接受才切 P02；否则保持原页显示原因'],
    ['STOP','优先处理停止；取消当前结果事务；去重但不丢弃安全请求','显示正在停止；可靠确认恢复后才开放开始'],
    ['OPEN_HISTORY / PREV / NEXT','过滤当前会话、分页、带回来源页','四行＋页码＋翻页使能一并更新'],
    ['NEW_SESSION','安全空闲才结束旧会话并建立新会话','清空当前结果、更新会话号、进入 P01'],
    ['BRIGHTNESS +/-','限制范围并转换为该屏支持的亮度值','应用成功后回写百分比和按钮状态'],
    ['TIME_SELECT / TIME +/-','维护编辑草稿、选中字段、日期合法性','更新 P10 数字与选中高亮'],
    ['SET_TIME','校验 RTC 范围、闰年、时间源；写入并读回','成功才显示已校准；失败显示原因'],
    ['OPEN_MAINTENANCE','检查授权状态，提供获准展示的只读数据','未授权仅说明；授权后才显示维护信息']])
line('文本更新建议：过程压力先按最多 5 Hz、已用时按 1 Hz 联调，状态/停止事件优先；这是性能调试起点，不是屏幕规格。结果用“暂隐藏 → 写全字段 → 显示”的顺序或固件已支持的批量刷新机制，防止不同次测量数据混在一起。\n')
line('画面绘好并不会自动具有历史缓存、测量控制、RTC 保存、患者关联和维护鉴权。它们都要有主控或经验证的屏端逻辑配套。屏端失联提示还需要实物固件支持的超时/心跳机制；主控到屏幕断线后，主控自身无法再发一条提示让屏幕知道断线。\n')
line('## 11. 在 VisualTFT 中按顺序检查\n')
line('1. **逐页外观**：背景 800×480；按钮不拉伸，文字不重影，背景内没有演示血压数字。\n2. **按下反馈**：每个按钮按下显示 down 图片，抬起恢复 up；长按不会连续开始测量。\n3. **ID 和通知**：虚拟屏/串口调试窗口记录各按钮的真实画面 ID、控件 ID；确认一次按下只处理一次。\n4. **测量链路**：开始 → 启动中 → 实际压力 → 完整结果；禁止用固定延时伪造成功。\n5. **停止**：启动中、测量中、停止等待和故障锁定均可发停止；状态不明时不能重测。\n6. **数值擦除**：把 188 改成 88、再改成 --，旧数字不得残留；单位不被擦除；三位脉率也不覆盖“次/分”。\n7. **历史**：0、1、4、5 条分别查看；首/末页禁用正确，返回原页面正确；取消和错误不填 0/0。\n8. **换患者**：取消保留原结果；确认建立新会话并清空显示；迟到结果不串到新患者。\n9. **设置**：亮度到上下限正确禁用；时间选择和加减热区完整；非法日期不能保存；取消不修改系统时间。\n10. **实屏检查**：核对屏幕料号、字体、16 位色彩显示、手套触摸和医护阅读距离。静态图片已检查不等于实机或医疗可用性验证通过。\n')
line('## 12. 完整素材索引与本地依据\n')
image('11 个页面总览','previews/all_pages.png')
line('资源清单：[manifest.json](manifest.json)；可直接对照填表：[控件配置表.csv](控件配置表.csv)。所有 SVG 源稿在 `sources/`，生成程序在 `tools/`。PNG 用于 VisualTFT，SVG 用于以后修改版式。\n')
line('本指南中的 VisualTFT 属性以本机安装资料和原工程核对：\n')
line('- `F:\\Program Files\\VisualTFT\\doc\\工程技术笔记\\工程技术笔记-按钮控件应用V2.2.pdf`：按钮属性、瞬变/开关、触控用途、事件通知和裁剪，PDF 第 7～12 页。\n- `F:\\Program Files\\VisualTFT\\doc\\工程技术笔记\\工程技术笔记-文本控件应用 V2.0.pdf`：文本颜色、背景、输入方式与字库选择，PDF 第 8～9 页。\n- `F:\\Program Files\\VisualTFT\\doc\\工程技术笔记\\工程技术笔记-如何自定义矢量字库V1.0.pdf`：不同产品系列的矢量字库能力，第 7 页。\n- `F:\\Program Files\\VisualTFT\\DeviceInfo.xml`：本机型号列表；本机 DN050 条目的键盘支持字段为 0，因此本稿未依赖系统键盘。\n- 原资料包的 M 系列入门手册、大彩串口屏指令集 V5.1；按钮标准事件的状态语义应按所选型号复核。\n- `F:\\VisualTFT\\SPO2_Hospital\\tools\\build_pages.py`、现有 `.tft` 和 PNG：系列配色来源。\n- 原方案：`../docs/superpowers/specs/2026-09-20-n050-blood-pressure-ui-design.md`；HKB-08B+ 错误文案和状态边界沿用该方案。\n')
line('### 素材验证范围\n')
line('交付前自动核对图片尺寸、Markdown 图片引用、页面/控件 ID 唯一性、控件边界、按钮目标和压缩包完整性，并进行页面图片目检。未启动真实 VisualTFT 工程编译、未连接血压模块、未下载到实屏；以上步骤需在绘图与联调时完成。\n')

text='\n'.join(md)+'\n'
(ROOT/'VisualTFT_医院版血压界面绘制指南.md').write_text(text,encoding='utf-8')

# Minimal renderer for this deliberately restricted Markdown: headings, tables,
# paragraphs, images, links and ordered/unordered lists. No dependency downloads.
def inline(s):
    s=escape(s)
    s=re.sub(r'!\[([^\]]*)\]\(([^)]+)\)',r'<a href="\2"><img loading="lazy" alt="\1" src="\2"></a>',s)
    s=re.sub(r'\[([^\]]+)\]\(([^)]+)\)',r'<a href="\2">\1</a>',s)
    s=re.sub(r'`([^`]+)`',r'<code>\1</code>',s)
    s=re.sub(r'\*\*([^*]+)\*\*',r'<strong>\1</strong>',s)
    return s.replace('&lt;br&gt;','<br>')


html=[];toc=[];lines=text.splitlines();i=0
while i<len(lines):
    t=lines[i]
    if not t.strip(): i+=1;continue
    if t.startswith('#'):
        n=len(t)-len(t.lstrip('#'));title=t[n:].strip();slug='section-'+str(len(toc)+len(html))
        html.append(f'<h{n} id="{slug}">{inline(title)}</h{n}>')
        if n==2: toc.append(f'<a href="#{slug}">{escape(title)}</a>')
    elif t.startswith('|'):
        rows=[]
        while i<len(lines) and lines[i].startswith('|'):rows.append(lines[i]);i+=1
        html.append('<div class="table-wrap"><table>')
        for j,row in enumerate(rows):
            if j==1:continue
            tag='th' if j==0 else 'td'
            html.append('<tr>'+''.join(f'<{tag}>{inline(cell.strip())}</{tag}>' for cell in row.strip('|').split('|'))+'</tr>')
        html.append('</table></div>');continue
    elif re.match(r'^\d+\. ',t) or t.startswith('- '):
        ordered=bool(re.match(r'^\d+\. ',t));tag='ol' if ordered else 'ul';html.append('<'+tag+'>')
        while i<len(lines) and (bool(re.match(r'^\d+\. ',lines[i])) if ordered else lines[i].startswith('- ')):
            item=re.sub(r'^\d+\. ','',lines[i]) if ordered else lines[i][2:]
            html.append('<li>'+inline(item)+'</li>');i+=1
        html.append('</'+tag+'>');continue
    else:html.append('<p>'+inline(t)+'</p>')
    i+=1

(ROOT/'阅读版.html').write_text('''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>VisualTFT 血压界面绘制指南</title><style>
*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:#0f1b2d;color:#f4f7fb;font:16px/1.85 "Microsoft YaHei",sans-serif}nav{position:fixed;left:0;top:0;bottom:0;width:252px;padding:24px 16px;border-right:1px solid #30465e;overflow:auto;background:#16263a}nav strong{display:block;margin-bottom:16px;color:#42cde0}nav a{display:block;margin:12px 0;font-size:14px}main{max-width:1220px;margin-left:252px;padding:32px 40px 100px}a{color:#76deed;text-decoration:none}a:hover{text-decoration:underline}h1{font-size:30px}h2{margin-top:60px;padding-top:16px;border-top:1px solid #30465e;font-size:25px}h3{margin-top:38px;color:#68d99f;font-size:20px}img{max-width:100%;height:auto;border-radius:8px}p> a>img{display:block;margin:16px 0}code{font:14px/1.6 Consolas,monospace;background:#22364d;padding:2px 5px;border-radius:4px;overflow-wrap:anywhere}.table-wrap{overflow:auto;margin:20px 0}table{border-collapse:collapse;width:100%;font-size:14px}th,td{border:1px solid #30465e;padding:9px 12px;vertical-align:top}th{background:#22364d;text-align:left}td img{max-width:280px}li{margin:9px 0}strong{color:#fff} @media(max-width:1000px){nav{position:static;width:auto;height:auto}nav a{display:inline-block;margin:4px 10px}main{margin:0;padding:22px}}@media print{nav{display:none}main{margin:0;padding:0}body{background:white;color:black}a{color:inherit}h2,h3{color:black}table{font-size:10px}img{break-inside:avoid}}
</style></head><body><nav><strong>VisualTFT 血压界面<br>绘制指南 / v1.0</strong>'''+''.join(toc)+'</nav><main>'+''.join(html)+'</main></body></html>',encoding='utf-8')
print(json.dumps({'pages':len(PAGES),'variants':len(VARIANTS),'button_types':len(BUTTONS),'button_actions':len(ACTIONS),'png_exports':len(RENDERS),'guide':str(ROOT/'VisualTFT_医院版血压界面绘制指南.md')},ensure_ascii=False))
