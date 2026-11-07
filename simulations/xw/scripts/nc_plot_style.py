import matplotlib as mpl

"""\n统一的绘图风格与配色配置：\n- 字号：在不显得拥挤的前提下尽量放大\n- 配色：统一管理，方便全文图统一修改\n\n本模块仅提供常量，不直接改 rcParams，\n具体 rcParams 仍由各脚本根据需要自行调用并可微调。\n"""

# 统一字体与字号设置（英文优先 Arial）
FONT_FAMILY = 'Arial'
# 坐标轴标签与标题字号（统一）
# 取原始与上次修改之间的中间值，既比最初大又比当前小
FONT_SIZE_LABEL = 9
FONT_SIZE_TITLE = 9
# 坐标轴刻度字号（统一）
FONT_SIZE_TICK = 8
# 图例字号（统一）
FONT_SIZE_LEGEND = 8
# 图中标注文字、柱顶数字等（统一）
FONT_SIZE_TEXT = 7
FONT_SIZE_BAR = 7

# 方案B：暖白底 + 冷灰蓝面板（NPG/Nature 风格配色）
FIG_BG = '#F4F1EA'        # 整体画布暖白色
AX_BG = '#EEF2F7'         # 坐标区冷灰蓝
CARD_EDGE = '#3F5F85'     # 钢蓝边线
GRID_COLOR = '#95A9C3'    # 网格线
LEGEND_BG = '#F1ECE2'     # 图例底色

# 三方案主色顺序（NC 推荐 turquoise + red，色盲友好）
COLOR_SCHEME = [
    '#00A69D',  # 方案1: 青绿 (turquoise)
    '#E64B35',  # 方案2: 珊瑚红 (coral/red)
    '#3C5488',  # 方案3: 深蓝 (dark blue)
]

# 高优先级业务配色（10/11）
COLOR_FLOW_10 = '#3C5488'   # 深蓝
COLOR_FLOW_11 = '#E64B35'   # 珊瑚红

# 重传包/错误包配色
COLOR_RETRANSMIT = '#E64B35'  # 珊瑚红

# 正常包配色（延迟柱状图）
COLOR_NORMAL = '#3C5488'  # 深蓝

# 柱状图配色：实际值 / 理想值、低/高优先级
COLOR_ACTUAL = '#3C5488'   # 实际/低优先级：深蓝
COLOR_THEORY = '#E64B35'   # 理想/高优先级：珊瑚红
COLOR_LOW = COLOR_ACTUAL
COLOR_HIGH = COLOR_THEORY

# 三种方案的统一标签（英文名，用于图例和标题）
SCHEME_LABELS = [
    'Best effort',    # 尽力而为
    'Ground Gated',   # 地面门控
    'Our Proposal',   # 所提方案
]

# 仅用于 x 轴刻度的换行标签（进一步压缩，避免重叠）
SCHEME_LABELS_WRAPPED = [
    'Best effort',
    'Ground\nGated',
    'Our Proposal',
]

# Stable output identifiers are independent of the displayed scheme labels.
SCHEME_FILE_STEMS = {
    'Best effort': 'best_effort',
    'Ground Gated': 'ground_gated',
    'Our Proposal': 'our_proposal',
}
