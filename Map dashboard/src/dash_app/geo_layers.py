from __future__ import annotations

import json
from pathlib import Path

import streamlit as st

DREN_LINES_PATH = Path(__file__).parent / "dren_lines.geojson"


@st.cache_resource(show_spinner=False)
def load_dren_lines(path: Path = DREN_LINES_PATH) -> dict[str, list]:
    """Carga las LineStrings de la red de drenaje en arrays lat/lon para
    Scattermapbox (con None como separador entre lineas)."""
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
