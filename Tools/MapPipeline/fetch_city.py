#!/usr/bin/env python3
"""Выгрузка настоящего города из OpenStreetMap для «Перегона» (как города в Euro Truck Simulator).

Запуск (нужен только Python 3, без сторонних библиотек):
    python Tools/MapPipeline/fetch_city.py novosibirsk
Результат: Content/Data/Cities/novosibirsk.city — игра подхватит его сама при следующем запуске
(в Output Log: «[Peregon] Город Новосибирск из OSM: N дорог, M зданий»).
Сырой ответ Overpass кешируется в Tools/MapPipeline/data/<город>.osm.json — повторный запуск без сети:
    python Tools/MapPipeline/fetch_city.py novosibirsk --cached

Масштаб — как в ETS: трасса между городами сжата 1:2, а дома, улицы, развязки — в натуральную величину.
Чтобы город уместился, его «сжимают» к краям: в радиусе CORE от центра всё 1:1 (центр, мосты, площади),
дальше масштаб плавно переходит к 1:2 (как у трассы). Здания при этом не уменьшаются — уменьшаются расстояния между
ними; на окраинах остаются только крупные и стоящие вдоль больших улиц (лишние, которые налезли бы на
соседей и дороги, отбрасываются). Улицы на окраинах — только магистральные.

Что выгружается (для каждой сущности — теги, нужные игре):
  дороги      highway=*: класс, полосы (lanes), одностороннее, кольцо (junction=roundabout), мост/тоннель,
              уровень (layer), ограничение скорости, название;
  перекрёстки узлы с highway=traffic_signals / crossing / stop / give_way;
  здания      building=*: контур, этажность (building:levels, height), вид, название;
  вода        natural=water, waterway=riverbank (Обь, с островами);
  парки       leisure=park|garden, landuse=grass|forest;
  железная дорога railway=rail;
  достопримечательности — по названию (см. LANDMARKS): вместо коробки игра ставит свою модель.

Данные OSM © участники OpenStreetMap, лицензия ODbL — указывайте это в титрах игры.
"""
import argparse
import json
import math
import os
import sys
import time
import urllib.parse
import urllib.request

# Серверы Overpass: при ошибке или таймауте берётся следующий.
OVERPASS = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
    "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
]
TILES = 4  # город качается кусками TILES × TILES — целиком сервер не успевает (504)
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

# Начало трассы в игре (совпадает с rgen::NovosibirskKemerovoOrigin) — туда должен выходить город.
ROUTE_START = (55.105, 82.955)

CITIES = {
    "novosibirsk": {
        "name": "Новосибирск",
        "center": (55.0302, 82.9204),          # площадь Ленина
        "bbox": (54.975, 82.865, 55.112, 83.010),  # юг, запад, север, восток
        "core": 2600.0,                        # м: ядро 1:1 (площадь Ленина, Красный проспект, Октябрьский мост)
        "blend": 1500.0,                       # м: переход от 1:1 к 1:2
        "landmarks": [
            # (ключ модели в игре, подстрока названия, что ищем: building/bridge)
            ("opera", "оперы и балета", "building"),
            ("oktyabrsky", "Октябрьский мост", "bridge"),
        ],
    },
}

HIGHWAYS = ("motorway", "trunk", "primary", "secondary", "tertiary", "unclassified", "residential", "living_street",
            "motorway_link", "trunk_link", "primary_link", "secondary_link", "tertiary_link")
MAJOR = {"motorway", "trunk", "primary", "secondary", "tertiary", "motorway_link", "trunk_link", "primary_link",
         "secondary_link", "tertiary_link"}
K = 2.0  # масштаб трассы между городами (1:2)


def query(tile, clip):
    """Всё, что начинается в куске tile; геометрия обрезается рамкой города clip (иначе Обь придёт целиком)."""
    s, w, n, e = tile
    b = f"({s},{w},{n},{e})"
    c = "({},{},{},{})".format(*clip)
    hw = "|".join(HIGHWAYS)
    return f"""
[out:json][timeout:180];
(
  way["highway"~"^({hw})$"]{b};
  way["building"]{b};
  way["natural"="water"]{b};
  relation["natural"="water"]{b};
  way["waterway"="riverbank"]{b};
  relation["waterway"="riverbank"]{b};
  way["leisure"~"^(park|garden)$"]{b};
  way["landuse"~"^(grass|forest)$"]{b};
  way["railway"="rail"]{b};
  node["highway"~"^(traffic_signals|crossing|stop|give_way)$"]{b};
);
out body geom{c};
"""


def post(q):
    """Запрос с повторами по нескольким серверам."""
    data = urllib.parse.urlencode({"data": q}).encode()
    last = None
    for attempt in range(3):
        for url in OVERPASS:
            req = urllib.request.Request(url, data=data, headers={"User-Agent": "peregon-map-pipeline/1.1"})
            try:
                with urllib.request.urlopen(req, timeout=300) as resp:
                    return json.load(resp)
            except Exception as ex:  # 429/504/обрыв — пробуем другой сервер
                last = ex
                print(f"    {url.split('/')[2]}: {ex}")
        wait = 30 * (attempt + 1)
        print(f"    все серверы заняты, жду {wait} с…")
        time.sleep(wait)
    raise SystemExit(f"Overpass недоступен: {last}. Запустите позже — уже скачанные куски сохранятся.")


def fetch(bbox, city):
    s, w, n, e = bbox
    tile_dir = os.path.join(HERE, "data", f"{city}.tiles")
    os.makedirs(tile_dir, exist_ok=True)
    seen = set()
    out = []
    total = TILES * TILES
    for i in range(TILES):
        for j in range(TILES):
            k = i * TILES + j + 1
            tile = (s + (n - s) * i / TILES, w + (e - w) * j / TILES,
                    s + (n - s) * (i + 1) / TILES, w + (e - w) * (j + 1) / TILES)
            path = os.path.join(tile_dir, f"{i}_{j}.json")
            if os.path.exists(path):
                with open(path, encoding="utf-8") as f:
                    part = json.load(f)
                print(f"  кусок {k}/{total}: уже скачан")
            else:
                print(f"  кусок {k}/{total}…")
                part = post(query(tile, bbox))
                with open(path, "w", encoding="utf-8") as f:
                    json.dump(part, f, ensure_ascii=False)
                time.sleep(2)  # не злим сервер
            for el in part.get("elements", []):
                key = (el.get("type"), el.get("id"))
                if key not in seen:
                    seen.add(key)
                    out.append(el)
    return {"elements": out}


# ---------------------------------------------------------------------------------------------
# Проекция и «сжатие» как в ETS
# ---------------------------------------------------------------------------------------------
class Warp:
    """Метры от центра города → метры игры. Ядро 1:1, дальше масштаб плавно уходит к 1:K."""

    def __init__(self, center, core, blend):
        self.lat0, self.lon0 = center
        self.core = core
        self.blend = blend
        self.mlat = 111320.0
        self.mlon = 111320.0 * math.cos(math.radians(self.lat0))
        # Таблица r → r' (интеграл местного масштаба).
        self.table = []
        r, rr, step = 0.0, 0.0, 10.0
        while r < 200000.0:
            self.table.append((r, rr))
            t = min(1.0, max(0.0, (r - core) / blend))
            k = 1.0 - (1.0 - 1.0 / K) * (t * t * (3 - 2 * t))
            r += step
            rr += k * step
        self.start = self.game_raw(*ROUTE_START)

    def local(self, lat, lon):
        return (lat - self.lat0) * self.mlat, (lon - self.lon0) * self.mlon  # север, восток

    def radial(self, r):
        i = min(int(r / 10.0), len(self.table) - 2)
        r0, a = self.table[i]
        _, b = self.table[i + 1]
        return a + (b - a) * (r - r0) / 10.0

    def scale_at(self, r):
        t = min(1.0, max(0.0, (r - self.core) / self.blend))
        return 1.0 - (1.0 - 1.0 / K) * (t * t * (3 - 2 * t))

    def game_raw(self, lat, lon):
        n, e = self.local(lat, lon)
        r = math.hypot(n, e)
        if r < 1e-6:
            return 0.0, 0.0
        rr = self.radial(r)
        return n * rr / r, e * rr / r

    def game(self, lat, lon):
        """Координаты игры (X — север, Y — восток): начало трассы — в (0, 0)."""
        x, y = self.game_raw(lat, lon)
        return x - self.start[0], y - self.start[1]


# ---------------------------------------------------------------------------------------------
# Геометрия
# ---------------------------------------------------------------------------------------------
def poly_area(pts):
    a = 0.0
    for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]):
        a += x0 * y1 - x1 * y0
    return a * 0.5


def centroid(pts):
    sx = sum(p[0] for p in pts) / len(pts)
    sy = sum(p[1] for p in pts) / len(pts)
    return sx, sy


def principal_yaw(pts):
    """Направление длинной стороны контура (рад, от севера к востоку)."""
    best, yaw = -1.0, 0.0
    for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]):
        d = math.hypot(x1 - x0, y1 - y0)
        if d > best:
            best, yaw = d, math.atan2(y1 - y0, x1 - x0)
    return yaw


def dp_simplify(pts, keep, eps):
    """Дуглас — Пекер; индексы из keep (перекрёстки, светофоры) не выбрасываются."""
    if len(pts) <= 2:
        return list(range(len(pts)))
    out = {0, len(pts) - 1} | {i for i in range(len(pts)) if keep(i)}
    stack = [(0, len(pts) - 1)]
    while stack:
        a, b = stack.pop()
        ax, ay = pts[a]
        bx, by = pts[b]
        dx, dy = bx - ax, by - ay
        L = math.hypot(dx, dy) or 1e-9
        best, bi = -1.0, -1
        for i in range(a + 1, b):
            px, py = pts[i]
            d = abs((px - ax) * dy - (py - ay) * dx) / L
            if i in out:
                d = max(d, eps * 2)
            if d > best:
                best, bi = d, i
        if bi > 0 and best > eps:
            out.add(bi)
            stack.append((a, bi))
            stack.append((bi, b))
    return sorted(out)


def seg_dist(p, a, b):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    t = 0.0 if L2 < 1e-9 else max(0.0, min(1.0, ((p[0] - ax) * dx + (p[1] - ay) * dy) / L2))
    return math.hypot(p[0] - ax - t * dx, p[1] - ay - t * dy)


def to_int(v, default=0):
    try:
        return int(float(str(v).split(";")[0].replace(",", ".").split()[0]))
    except (ValueError, IndexError):
        return default


# Этажность по виду здания, если в OSM не указана.
DEFAULT_LEVELS = {
    "apartments": 9, "residential": 5, "dormitory": 5, "house": 1, "detached": 1, "semidetached_house": 2, "garage": 1,
    "garages": 1, "shed": 1, "hut": 1, "industrial": 2, "warehouse": 1, "commercial": 3, "retail": 2, "office": 5,
    "school": 3, "kindergarten": 2, "hospital": 5, "university": 5, "college": 4, "church": 2, "cathedral": 3,
    "public": 3, "civic": 3, "government": 4, "train_station": 2, "service": 1, "kiosk": 1, "roof": 1, "yes": 4,
}


def building_kind(tag, levels, r_real):
    """Вид фасада для игры."""
    if tag in ("house", "detached", "semidetached_house", "hut", "bungalow", "cabin"):
        return "house"
    if tag in ("garage", "garages", "shed", "service"):
        return "garage"
    if tag in ("industrial", "warehouse", "factory", "manufacture"):
        return "industrial"
    if tag in ("church", "cathedral", "chapel", "mosque", "temple"):
        return "church"
    if tag in ("school", "kindergarten", "hospital", "university", "college", "office", "commercial", "retail",
               "government", "public", "civic", "train_station"):
        return "office" if levels >= 3 else "shop"
    if r_real < 1600.0 and levels <= 7:
        return "stucco"   # исторический центр: «сталинки», доходные дома
    if levels >= 5:
        return "panel"
    return "brick"


def road_lanes(tags, hw):
    oneway = tags.get("oneway") in ("yes", "1", "true") or tags.get("junction") == "roundabout" or hw.endswith("_link") \
        or hw == "motorway"
    lanes = to_int(tags.get("lanes"), 0)
    if lanes <= 0:
        base = {"motorway": 4, "trunk": 4, "primary": 4, "secondary": 2, "tertiary": 2}.get(hw, 2)
        if oneway:
            base = 2 if hw in ("motorway", "trunk", "primary") else 1
        lanes = base
    return max(1, min(lanes, 8)), oneway


def is_water_tags(tags):
    return tags.get("natural") == "water" or tags.get("waterway") == "riverbank"


def water_rings(el, bbox):
    """Контуры воды одного объекта (путь или мультиполигон) в широте/долготе.

    Обь в OSM — огромный мультиполигон из десятков линий; рамка города режет их, и за рамкой точки приходят
    пустыми. Склеиваем куски по общим концам; оставшиеся открытые цепочки замыкаем вдоль рамки. Обходов рамки
    два (вода «слева» или «справа» от цепочек) — берём тот, что даёт меньшую площадь: река в городе меньше
    суши вокруг неё. Возвращает (внешние контуры, острова)."""
    s, w, n, e = bbox

    def pieces(geom):
        out, cur = [], []
        for g in geom or []:
            if isinstance(g, dict) and "lat" in g and "lon" in g:
                cur.append((g["lat"], g["lon"]))
            else:
                if len(cur) >= 2:
                    out.append(cur)
                cur = []
        if len(cur) >= 2:
            out.append(cur)
        return out

    outer, inner = [], []
    if el.get("type") == "way":
        outer = pieces(el.get("geometry"))
    else:
        for m in el.get("members", []):
            if m.get("type") == "way":
                (inner if m.get("role") == "inner" else outer).extend(pieces(m.get("geometry")))

    def key(p):
        return (round(p[0], 7), round(p[1], 7))

    def stitch(segs):
        chains = [list(c) for c in segs]
        closed, opened = [], []
        while chains:
            c = chains.pop()
            grown = True
            while grown and key(c[0]) != key(c[-1]):
                grown = False
                for i, d in enumerate(chains):
                    if key(d[0]) == key(c[-1]):
                        c += d[1:]
                    elif key(d[-1]) == key(c[-1]):
                        c += d[::-1][1:]
                    elif key(d[-1]) == key(c[0]):
                        c = d + c[1:]
                    elif key(d[0]) == key(c[0]):
                        c = d[::-1] + c[1:]
                    else:
                        continue
                    chains.pop(i)
                    grown = True
                    break
            if len(c) >= 4 and key(c[0]) == key(c[-1]):
                closed.append(c)
            elif len(c) >= 2:
                opened.append(c)
        return closed, opened

    def per(p):
        """Точка → (прижатая к рамке точка, параметр по периметру против часовой: юг, восток, север, запад)."""
        lat = min(max(p[0], s), n)
        lon = min(max(p[1], w), e)
        d = {"s": lat - s, "e": e - lon, "n": n - lat, "w": lon - w}
        side = min(d, key=d.get)
        H, W = n - s, e - w
        if side == "s":
            return (s, lon), lon - w
        if side == "e":
            return (lat, e), W + (lat - s)
        if side == "n":
            return (n, lon), W + H + (e - lon)
        return (lat, w), 2 * W + H + (n - lat)

    H, W = n - s, e - w
    P = 2 * (W + H)
    corners = [(W, (s, e)), (W + H, (n, e)), (2 * W + H, (n, w)), (P, (s, w))]

    def arc(t0, t1):
        """Точки рамки строго между параметрами t0 → t1 (против часовой), с углами и шагом ~0.002°."""
        if t1 < t0:
            t1 += P
        pts = []
        t = t0
        step = 0.002
        while True:
            nxt = t + step
            # ближайший угол
            for base in (0.0, P):
                for tc, c in corners:
                    if t < base + tc < min(nxt, t1):
                        nxt = base + tc
            if nxt >= t1:
                break
            pts.append(point_at(nxt % P))
            t = nxt
        return pts

    def point_at(t):
        if t <= W:
            return (s, w + t)
        if t <= W + H:
            return (s + (t - W), e)
        if t <= 2 * W + H:
            return (n, e - (t - W - H))
        return (n - (t - 2 * W - H), w)

    def area(r):
        a = 0.0
        for (y0, x0), (y1, x1) in zip(r, r[1:] + r[:1]):
            a += x0 * y1 - x1 * y0
        return abs(a) * 0.5

    closed, opened = stitch(outer)
    rings = [c for c in closed]
    if opened:
        ends = []  # (t, индекс цепочки, 0 — начало / 1 — конец, точка на рамке)
        for i, c in enumerate(opened):
            for k, p in ((0, c[0]), (1, c[-1])):
                q, t = per(p)
                ends.append((t, i, k, q))
        ends.sort()
        best = None
        for shift in (0, 1):
            # Пары соседних по рамке концов: (1,2),(3,4)… или (0,1),(2,3)…
            # Соседние по рамке концы соединяются отрезком рамки между ними (против часовой от первого ко второму).
            partner = {}
            m = len(ends)
            for j in range(shift, m + shift, 2):
                ea, eb = ends[j % m], ends[(j + 1) % m]
                pts = arc(ea[0], eb[0])
                partner[(ea[1], ea[2])] = (eb, [ea[3]] + pts + [eb[3]])
                partner[(eb[1], eb[2])] = (ea, [eb[3]] + pts[::-1] + [ea[3]])
            used = set()
            loops = []
            for i in range(len(opened)):
                if i in used:
                    continue
                ring, ci, k = [], i, 0
                while ci not in used:
                    used.add(ci)
                    ring += opened[ci] if k == 0 else opened[ci][::-1]
                    nb, link = partner[(ci, 1 - k)]
                    ring += link
                    ci, k = nb[1], nb[2]
                loops.append(ring)
            total = sum(area(r) for r in loops)
            if best is None or total < best[0]:
                best = (total, loops)
        rings += best[1]
    isl_closed, _ = stitch(inner)
    return rings, isl_closed


def clean_clipped(els):
    """Геометрия, обрезанная рамкой города, приходит с пустыми точками (null) за рамкой. У дорог оставляем
    самый длинный непрерывный кусок (узлы и точки — в паре), у контуров просто выкидываем пустые точки."""
    def ok(g):
        return isinstance(g, dict) and "lat" in g and "lon" in g
    for el in els:
        if is_water_tags(el.get("tags", {})):
            continue  # воду собирает water_rings: ей нужны разрывы, чтобы замкнуть контур по рамке
        geom = el.get("geometry")
        if el.get("type") == "way" and geom and not all(ok(g) for g in geom):
            nodes = el.get("nodes", [])
            if "highway" in el.get("tags", {}) and len(nodes) == len(geom):
                best, cur = (0, 0), None
                for i, g in enumerate(geom + [None]):
                    if ok(g):
                        cur = i if cur is None else cur
                    elif cur is not None:
                        if i - cur > best[1] - best[0]:
                            best = (cur, i)
                        cur = None
                el["geometry"] = geom[best[0]:best[1]]
                el["nodes"] = nodes[best[0]:best[1]]
                if len(el["geometry"]) < 2:
                    el["geometry"] = []
            else:
                el["geometry"] = [g for g in geom if ok(g)]
                if len(el["geometry"]) < 3:
                    el["geometry"] = []
        for m in el.get("members", []):
            mg = m.get("geometry")
            if mg:
                m["geometry"] = [g for g in mg if ok(g)]
    return els


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("city", nargs="?", default="novosibirsk", choices=sorted(CITIES))
    ap.add_argument("--cached", action="store_true", help="не ходить в сеть, взять data/<город>.osm.json")
    ap.add_argument("--input", help="готовый ответ Overpass (JSON) вместо запроса")
    ap.add_argument("--out", help="куда записать .city")
    args = ap.parse_args()
    cfg = CITIES[args.city]

    cache = os.path.join(HERE, "data", f"{args.city}.osm.json")
    if args.input:
        with open(args.input, encoding="utf-8") as f:
            payload = json.load(f)
    elif args.cached:
        with open(cache, encoding="utf-8") as f:
            payload = json.load(f)
    else:
        print("Запрос к Overpass API (крупный город — до нескольких минут)…")
        payload = fetch(cfg["bbox"], args.city)
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        with open(cache, "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False)
        print(f"  ответ сохранён: {cache}")

    warp = Warp(cfg["center"], cfg["core"], cfg["blend"])
    els = clean_clipped(payload.get("elements", []))
    print(f"  элементов: {len(els)}")

    # Узлы со смыслом для перекрёстков.
    node_flags = {}
    for el in els:
        if el.get("type") == "node":
            hw = el.get("tags", {}).get("highway", "")
            flag = {"traffic_signals": 1, "crossing": 2, "stop": 4, "give_way": 8}.get(hw, 0)
            if flag:
                node_flags[el["id"]] = node_flags.get(el["id"], 0) | flag

    out_nodes = {}  # osm id → (индекс, x, y)
    node_list = []

    def node_index(osm_id, lat, lon):
        if osm_id not in out_nodes:
            x, y = warp.game(lat, lon)
            out_nodes[osm_id] = len(node_list)
            node_list.append((x, y, node_flags.get(osm_id, 0)))
        return out_nodes[osm_id]

    # ---- Дороги ----
    roads = []
    use_count = {}
    for el in els:
        if el.get("type") == "way" and el.get("tags", {}).get("highway") in HIGHWAYS:
            for nid in el.get("nodes", []):
                use_count[nid] = use_count.get(nid, 0) + 1
    landmark_roads = {}
    for el in els:
        tags = el.get("tags", {})
        hw = tags.get("highway")
        if el.get("type") != "way" or hw not in HIGHWAYS or not el.get("geometry"):
            continue
        geom = el["geometry"]
        nodes = el.get("nodes", [])
        if len(nodes) != len(geom):
            continue
        # На окраинах (в сжатой зоне) — только магистральные улицы: иначе сетка дворов превращается в кашу.
        mid = geom[len(geom) // 2]
        n, e = warp.local(mid["lat"], mid["lon"])
        r = math.hypot(n, e)
        if hw not in MAJOR and r > cfg["core"] + 300.0:
            continue
        pts = [warp.game(g["lat"], g["lon"]) for g in geom]
        keep = dp_simplify(pts, lambda i: use_count.get(nodes[i], 0) > 1 or nodes[i] in node_flags, 0.6)
        idx = [node_index(nodes[i], geom[i]["lat"], geom[i]["lon"]) for i in keep]
        lanes, oneway = road_lanes(tags, hw)
        name = tags.get("name", "").replace("|", "/")
        style = ""
        for key, needle, what in cfg["landmarks"]:
            if what == "bridge" and needle.lower() in name.lower() and tags.get("bridge"):
                style = key
                landmark_roads[key] = landmark_roads.get(key, 0) + 1
        roads.append({
            "cls": hw, "lanes": lanes, "oneway": int(oneway), "ring": int(tags.get("junction") == "roundabout"),
            "bridge": int(tags.get("bridge") not in (None, "no")), "tunnel": int(tags.get("tunnel") not in (None, "no")),
            "layer": to_int(tags.get("layer"), 0), "speed": to_int(tags.get("maxspeed"), 0), "style": style,
            "name": name, "nodes": idx,
        })

    # Выезд на трассу: от ближайшего узла крупной дороги — к началу трассы (0, 0).
    best, best_d = -1, 1e18
    for rd in roads:
        if rd["cls"] in ("motorway", "trunk", "primary", "secondary"):
            for i in (rd["nodes"][0], rd["nodes"][-1]):
                x, y, _ = node_list[i]
                d = math.hypot(x, y)
                if d < best_d:
                    best, best_d = i, d
    if best >= 0 and best_d > 1.0:
        node_list.append((0.0, 0.0, 0))
        roads.append({"cls": "trunk", "lanes": 2, "oneway": 0, "ring": 0, "bridge": 0, "tunnel": 0, "layer": 0, "speed": 90,
                      "style": "exit", "name": "Р-255 «Сибирь»", "nodes": [best, len(node_list) - 1]})
        print(f"  выезд на трассу: {best_d:.0f} м от последнего узла города")

    # ---- Здания ----
    buildings = []
    landmarks = []
    road_segs = []
    for rd in roads:
        half = rd["lanes"] * 1.75 + 2.5
        for a, b in zip(rd["nodes"], rd["nodes"][1:]):
            road_segs.append((node_list[a][:2], node_list[b][:2], half))
    # Сетка для быстрых проверок «налезает ли» (ячейка 50 м).
    grid = {}

    def cells(cx, cy, rad):
        for gx in range(int((cx - rad) // 50), int((cx + rad) // 50) + 1):
            for gy in range(int((cy - rad) // 50), int((cy + rad) // 50) + 1):
                yield gx, gy

    seg_grid = {}
    for si, (a, b, half) in enumerate(road_segs):
        x0, x1 = sorted((a[0], b[0]))
        y0, y1 = sorted((a[1], b[1]))
        for gx in range(int((x0 - half) // 50), int((x1 + half) // 50) + 1):
            for gy in range(int((y0 - half) // 50), int((y1 + half) // 50) + 1):
                seg_grid.setdefault((gx, gy), []).append(si)

    cand = []
    for el in els:
        tags = el.get("tags", {})
        if el.get("type") != "way" or "building" not in tags or not el.get("geometry"):
            continue
        geo = el["geometry"]
        if len(geo) < 4:
            continue
        ring_real = [warp.local(g["lat"], g["lon"]) for g in geo[:-1]]
        area = abs(poly_area(ring_real))
        if area < 40.0:
            continue
        cn, ce = centroid(ring_real)
        r_real = math.hypot(cn, ce)
        btag = tags.get("building", "yes")
        levels = to_int(tags.get("building:levels"), 0)
        if levels <= 0 and tags.get("height"):
            levels = max(1, round(to_int(tags.get("height"), 0) / 3.0))
        if levels <= 0:
            levels = DEFAULT_LEVELS.get(btag, 4)
        name = tags.get("name", "").replace("|", "/")
        # Контур не сжимается: сдвигаем его целиком туда, куда «сжатие» перенесло центр.
        gc = warp.game(*_inverse(warp, cn, ce))
        ring = [(gc[0] + (n - cn), gc[1] + (e - ce)) for n, e in ring_real]
        lm = None
        for key, needle, what in cfg["landmarks"]:
            if what == "building" and needle.lower() in name.lower():
                lm = key
        cand.append((area * levels, ring, levels, building_kind(btag, levels, r_real), name, lm, r_real, gc))

    cand.sort(key=lambda c: -c[0])  # сначала крупные: при тесноте на окраинах остаются они
    dropped = 0
    for weight, ring, levels, kind, name, lm, r_real, gc in cand:
        rad = max(math.hypot(p[0] - gc[0], p[1] - gc[1]) for p in ring)
        if lm:
            landmarks.append((lm, gc[0], gc[1], principal_yaw(ring), rad))
            continue
        compressed = warp.scale_at(r_real) < 0.98
        if compressed:
            clash = False
            for cell in cells(gc[0], gc[1], rad):
                for (ox, oy, orad) in grid.get(cell, ()):
                    if math.hypot(ox - gc[0], oy - gc[1]) < (orad + rad) * 0.8:
                        clash = True
                        break
                if clash:
                    break
            if not clash:
                for cell in cells(gc[0], gc[1], rad):
                    for si in seg_grid.get(cell, ()):
                        a, b, half = road_segs[si]
                        if seg_dist(gc, a, b) < half + rad * 0.6:
                            clash = True
                            break
                    if clash:
                        break
            if clash:
                dropped += 1
                continue
        for cell in cells(gc[0], gc[1], rad):
            grid.setdefault(cell, []).append((gc[0], gc[1], rad))
        buildings.append((kind, levels, name, ring))
    print(f"  зданий: {len(buildings)} (на окраинах убрано налезающих: {dropped})")

    # ---- Вода, острова, парки, железная дорога ----
    water, islands, parks, rails = [], [], [], []

    def ring_of(geom):
        return [warp.game(g["lat"], g["lon"]) for g in geom]

    for el in els:
        tags = el.get("tags", {})
        is_water = is_water_tags(tags)
        if is_water and el.get("type") in ("way", "relation"):
            outer_rings, isl = water_rings(el, cfg["bbox"])
            water += [[warp.game(la, lo) for la, lo in r] for r in outer_rings]
            islands += [[warp.game(la, lo) for la, lo in r] for r in isl]
            continue
        if el.get("type") == "way" and el.get("geometry"):
            if tags.get("leisure") in ("park", "garden") or tags.get("landuse") in ("grass", "forest"):
                parks.append(ring_of(el["geometry"]))
            elif tags.get("railway") == "rail":
                rails.append(ring_of(el["geometry"]))

    for key, needle, what in cfg["landmarks"]:
        if what == "bridge" and key not in landmark_roads:
            print(f"  ! мост «{needle}» не найден в выгрузке")
        if what == "building" and not any(l[0] == key for l in landmarks):
            print(f"  ! здание «{needle}» не найдено в выгрузке")

    out = args.out or os.path.join(ROOT, "Content", "Data", "Cities", f"{args.city}.city")
    os.makedirs(os.path.dirname(out), exist_ok=True)

    def fmt(pts):
        return " ".join(f"{x:.2f} {y:.2f}" for x, y in pts)

    with open(out, "w", encoding="utf-8") as f:
        f.write("PEREGON-CITY 1\n")
        f.write(f"CITY {cfg['name']}\n")
        f.write("SOURCE © OpenStreetMap contributors, ODbL\n")
        cx, cy = warp.game(*cfg["center"])
        f.write(f"CENTER {cx:.2f} {cy:.2f} {cfg['core']:.0f}\n")
        for x, y, fl in node_list:
            f.write(f"N {x:.2f} {y:.2f} {fl}\n")
        for rd in roads:
            f.write(f"W {rd['cls']} {rd['lanes']} {rd['oneway']} {rd['ring']} {rd['bridge']} {rd['tunnel']} {rd['layer']} {rd['speed']} "
                    f"{rd['style'] or '-'}|{rd['name']}|{' '.join(map(str, rd['nodes']))}\n")
        for kind, levels, name, ring in buildings:
            f.write(f"B {kind} {levels}|{name}|{fmt(ring)}\n")
        for key, x, y, yaw, rad in landmarks:
            f.write(f"L {key} {x:.2f} {y:.2f} {yaw:.4f} {rad:.1f}\n")
        for ring in water:
            f.write(f"WATER {fmt(ring)}\n")
        for ring in islands:
            f.write(f"ISLAND {fmt(ring)}\n")
        for ring in parks:
            f.write(f"PARK {fmt(ring)}\n")
        for line in rails:
            f.write(f"RAIL {fmt(line)}\n")
    size = os.path.getsize(out) / 1e6
    print(f"Готово: {out} ({size:.1f} МБ): узлов {len(node_list)}, дорог {len(roads)}, зданий {len(buildings)}, "
          f"достопримечательностей {len(landmarks)}, водоёмов {len(water)}")


def _inverse(warp, n, e):
    """Метры от центра → широта/долгота (для warp.game по центру контура)."""
    return warp.lat0 + n / warp.mlat, warp.lon0 + e / warp.mlon


if __name__ == "__main__":
    main()
