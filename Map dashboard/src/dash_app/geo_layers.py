from __future__ import annotations

import json
from pathlib import Path

import streamlit as st

DREN_LINES_PATH = Path(__file__).parent / "dren_lines.geojson"
CUENCA_PATH     = Path(__file__).parent / "cuenca.geojson"
INDUSTRIES_PATH = Path(__file__).parent / "industries.geojson"

# Buckets de drenaje: (acc_min, acc_max, width_px, color)
# acc está normalizado 0-1 según la acumulación máxima de la cuenca
_DREN_BUCKETS = [
    (0.0,  0.05, 0.7,  "#a8d5f5"),
    (0.05, 0.15, 1.3,  "#5aaee0"),
    (0.15, 0.40, 2.2,  "#1a73c8"),
    (0.40, 1.01, 3.5,  "#0a3d7a"),
]



@st.cache_resource(show_spinner=False)
def load_dren_lines(path: Path = DREN_LINES_PATH) -> list[dict] | None:
    """Carga la red de drenaje agrupada en buckets por acumulación de flujo.
    Retorna lista de dicts {lat, lon, width, color} listos para Scattermapbox."""
    if not path.exists():
        return None
    with open(path, encoding="utf-8") as f:
        data = json.load(f)

    buckets: list[dict] = [
        {"lat": [], "lon": [], "width": w, "color": c}
        for _, _, w, c in _DREN_BUCKETS
    ]

    for feature in data.get("features", []):
        geom = feature.get("geometry") or {}
        if geom.get("type") != "LineString":
            continue
        acc = (feature.get("properties") or {}).get("acc", 0.0)
        idx = 0
        for i, (lo, hi, _, _) in enumerate(_DREN_BUCKETS):
            if lo <= acc < hi:
                idx = i
                break
        b = buckets[idx]
        for lon, lat in geom["coordinates"]:
            b["lat"].append(lat)
            b["lon"].append(lon)
        b["lat"].append(None)
        b["lon"].append(None)

    result = [b for b in buckets if b["lat"]]
    return result or None


@st.cache_resource(show_spinner=False)
def load_cuenca(path: Path = CUENCA_PATH) -> dict[str, list] | None:
    if not path.exists():
        return None
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    lats: list[float | None] = []
    lons: list[float | None] = []
    for feature in data.get("features", []):
        geom = feature.get("geometry") or {}
        gtype = geom.get("type")
        coords = geom.get("coordinates", [])
        if gtype == "Polygon":
            rings = coords
        elif gtype == "MultiPolygon":
            rings = [ring for poly in coords for ring in poly]
        else:
            continue
        for ring in rings:
            for lon, lat in ring:
                lats.append(lat)
                lons.append(lon)
            lats.append(None)
            lons.append(None)
    return {"lat": lats, "lon": lons} if lats else None


@st.cache_resource(show_spinner=False)
def load_industries(path: Path = INDUSTRIES_PATH) -> dict | None:
    """Carga todos los puntos de industrias en un único dict lat/lon/texts."""
    if not path.exists():
        return None
    with open(path, encoding="utf-8") as f:
        data = json.load(f)

    lats, lons, texts = [], [], []
    for feat in data.get("features", []):
        geom  = feat.get("geometry") or {}
        props = feat.get("properties") or {}
        if geom.get("type") != "Point":
            continue
        lon, lat = geom["coordinates"]
        lats.append(lat)
        lons.append(lon)
        texts.append(
            f"<b>{props.get('nombre', '')}</b><br>"
            f"{props.get('giro', '')}"
        )

    return {"lat": lats, "lon": lons, "texts": texts} if lats else None
