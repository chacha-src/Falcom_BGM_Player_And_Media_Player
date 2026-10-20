# おまけタクティクス用アイコン（交差した剣、既存 ui_icons と同じ紫フラット調）
import math
from PIL import Image, ImageDraw

S = 512
col = (112, 84, 214, 255)


def sword(flip):
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx = 256
    d.polygon([(cx - 30, 340), (cx - 30, 70), (cx, 20), (cx + 30, 70), (cx + 30, 340)], fill=col)
    d.rounded_rectangle([(cx - 92, 336), (cx + 92, 372)], 16, fill=col)
    d.rectangle([(cx - 18, 372), (cx + 18, 446)], fill=col)
    d.ellipse([(cx - 34, 436), (cx + 34, 500)], fill=col)
    return im.rotate(-40 if flip else 40, resample=Image.BICUBIC, center=(256, 270))


im = Image.alpha_composite(sword(False), sword(True))
sizes = [16, 20, 24, 32, 48]
im.save('res/ui_icons/tactics.ico', sizes=[(s, s) for s in sizes])
im.resize((128, 128), Image.LANCZOS).save('tactics_src/tactics_icon_prev.png')
