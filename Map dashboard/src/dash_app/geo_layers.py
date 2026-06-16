from __future__ import annotations

import json
from pathlib import Path

import streamlit as st

DREN_LINES_PATH  = Path(__file__).parent / "dren_lines.geojson"
CUENCA_PATH      = Path(__file__).parent / "cuenca.geojson"
INDUSTRIES_PATH  = Path(__file__).parent / "industries.geojson"


@st.cache_resource(show_spinner=False)
def load_dren_lines(path: Path = DREN_LINES_PATH) -> dict[str, list] | None:
    if not path.exists():
        return None
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    lats: list[float | None] = []
    lons: list[float | None] = []
    for feature in data.get("features", []):
        geometry = feature.get("geometry") or {}
        if geometry.get("type") != "LineString":
            continue
        for lon, lat in geometry["coordinates"]:
            lats.append(lat)
            lons.append(lon)
        lats.append(None)
        lons.append(None)
    return {"lat": lats, "lon": lons}


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
    if not path.exists():
        return None
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    lats, lons, colors, texts = [], [], [], []
    for feat in data.get("features", []):
        geom  = feat.get("geometry") or {}
        props = feat.get("properties") or {}
        if geom.get("type") != "Point":
            continue
        lon, lat = geom["coordinates"]
        lats.append(lat)
        lons.append(lon)
        colors.append(props.get("color", "#888888"))
        texts.append(
            f"<b>{props.get('nombre', '')}</b><br>"
            f"{props.get('giro', '')}<br>"
            f"SCIAN: {props.get('scian', '')}<br>"
            f"Riesgo: <b>{props.get('nivel', '')}</b><br>"
            f"Empleados: {props.get('estrato', '')}"
        )
    return {"lat": lats, "lon": lons, "colors": colors, "texts": texts} if lats else None
