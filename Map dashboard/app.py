from __future__ import annotations

from datetime import date, datetime, timedelta, timezone
from zoneinfo import ZoneInfo

import pandas as pd
import streamlit as st

from src.dash_app.config import SettingsError, load_settings
from src.dash_app.geo_layers import load_cuenca, load_dren_lines, load_industries
from src.dash_app.influx_client import QueryWindow, query_available_dates, query_measurements, query_weekly_adc_range
from src.dash_app.transform import aggregate_to_frames
from src.dash_app.viz import build_animated_map

_GDL = ZoneInfo("America/Mexico_City")


st.set_page_config(page_title="Dashboard ambiental 24h", layout="wide", initial_sidebar_state="collapsed")
st.title("Mapa dinámico de olores")
_now_cdmx = datetime.now(tz=_GDL)
st.caption(f"Hora CDMX: {_now_cdmx.strftime('%H:%M')} — {_now_cdmx.strftime('%d/%m/%Y')}")

# Umbral para valores altos (rojo) en el heatmap, expresado en % de olor (0-100).
HIGH_THRESHOLD_VALUE = 75.0

# Centro por default: punto medio entre El Salto y Juanacatlán, Jalisco.
DEFAULT_CENTER_LAT = 20.52
DEFAULT_CENTER_LON = -103.19
DEFAULT_ZOOM = 13.0


@st.cache_data(show_spinner=False, ttl=3600)
def _fetch_available_dates(settings_dict: dict) -> list:
    return query_available_dates(load_settings())


@st.cache_data(show_spinner=False, ttl=3600)
def _fetch_weekly_range(settings_dict: dict) -> tuple[float, float] | None:
    return query_weekly_adc_range(load_settings())


@st.cache_data(show_spinner=False, ttl=300)
def _fetch(settings_dict: dict, selected_stations: tuple[str, ...], date_iso: str):
    settings = load_settings()
    sel_date = date.fromisoformat(date_iso)
    if sel_date >= date.today():
        day_stop = datetime.now(tz=_GDL)
        day_start = day_stop - timedelta(hours=25)
    else:
        day_start = datetime(sel_date.year, sel_date.month, sel_date.day, tzinfo=_GDL)
        day_stop = day_start + timedelta(hours=24)
    window = QueryWindow(
        start=day_start.astimezone(timezone.utc),
        stop=day_stop.astimezone(timezone.utc),
    )
    station_filter = list(selected_stations) if selected_stations else None
    return query_measurements(settings=settings, window=window, station_filter=station_filter), window


try:
    settings = load_settings()
except SettingsError as exc:
    st.error(str(exc))
    st.info("Copia .env.example a .env y exporta las variables antes de ejecutar.")
    st.stop()

st.sidebar.header("Controles")

available_dates = _fetch_available_dates(settings.__dict__)
selected_date = st.sidebar.date_input(
    "Día a visualizar",
    value=available_dates[0] if available_dates else date.today(),
    min_value=available_dates[-1] if available_dates else date.today(),
    max_value=date.today(),
    help="Muestra datos de 00:00 a 23:59 hora de Guadalajara del día seleccionado.",
)

raw_station_filter = st.sidebar.text_input(
    "Filtro de estaciones (coma separada)",
    value="",
    help="Ejemplo: ST_01,ST_09",
)
selected_stations = tuple(
    [s.strip() for s in raw_station_filter.split(",") if s.strip()]
)

map_zoom = st.sidebar.slider("Zoom inicial", min_value=1.0, max_value=15.0, value=DEFAULT_ZOOM, step=0.5)

st.sidebar.subheader("Apariencia del heatmap")
blur = st.sidebar.slider(
    "Radius del circulo (px)",
    min_value=100,
    max_value=200,
    value=100,
    step=1,
    help="Tamaño del circulo/heatmap de cada estacion en pixeles (parametro "
    "'radius' de density_mapbox). A mayor valor, mas grande se ve el punto.",
)
high_threshold = st.sidebar.slider(
    "Umbral alto (escala de color, rojo)",
    min_value=1.0,
    max_value=100.0,
    value=HIGH_THRESHOLD_VALUE,
    step=0.5,
    help="Porcentaje de olor (0-100%) que se pinta en rojo (extremo de la escala). "
    "Si los valores reales quedan muy por debajo de este numero, los puntos "
    "se ven palidos/transparentes.",
)
heatmap_opacity = 1.0
show_markers = st.sidebar.checkbox(
    "Mostrar marcador solido por estacion",
    value=True,
    help="Agrega un punto solido (sin difuminado) que se mueve y cambia de "
    "color en cada frame de la animacion, igual que el heatmap, coloreado "
    "con la misma escala.",
)
if show_markers:
    marker_size = st.sidebar.slider(
        "Radio maximo del marcador (px)",
        min_value=4,
        max_value=60,
        value=16,
        step=1,
        help="Tamaño del marcador cuando el valor alcanza el umbral alto. "
        "El tamaño se escala segun el valor de cada estacion, normalizado "
        "entre el minimo de los datos y el umbral alto.",
    )
    marker_opacity = st.sidebar.slider(
        "Opacidad del marcador",
        min_value=0.1,
        max_value=1.0,
        value=1.0,
        step=0.05,
    )
else:
    marker_size = 16
    marker_opacity = 1.0

st.sidebar.subheader("Capa de dren")
show_dren = st.sidebar.checkbox("Mostrar red de drenaje", value=True)
dren_width = st.sidebar.slider(
    "Grosor de linea del dren (px)",
    min_value=0.5,
    max_value=5.0,
    value=1.5,
    step=0.5,
)

st.sidebar.subheader("Capas adicionales")
show_cuenca = st.sidebar.checkbox("Mostrar límite de cuenca", value=False)
show_industries = st.sidebar.checkbox("Mostrar industrias (DENUE)", value=True)

if st.sidebar.button("Recargar datos", type="primary"):
    _fetch.clear()

with st.spinner("Consultando InfluxDB..."):
    raw_df, selected_window = _fetch(settings.__dict__, selected_stations, selected_date.isoformat())

if raw_df.empty:
    default_center_lat = DEFAULT_CENTER_LAT
    default_center_lon = DEFAULT_CENTER_LON
else:
    default_center_lat = float(raw_df["lat"].median())
    default_center_lon = float(raw_df["lon"].median())

if "map_center_lat" not in st.session_state:
    st.session_state.map_center_lat = default_center_lat
if "map_center_lon" not in st.session_state:
    st.session_state.map_center_lon = default_center_lon

if st.sidebar.button("Centrar en datos actuales"):
    st.session_state.map_center_lat = default_center_lat
    st.session_state.map_center_lon = default_center_lon

map_center_lat = st.sidebar.number_input(
    "Centro inicial lat",
    min_value=-90.0,
    max_value=90.0,
    step=0.01,
    key="map_center_lat",
)
map_center_lon = st.sidebar.number_input(
    "Centro inicial lon",
    min_value=-180.0,
    max_value=180.0,
    step=0.01,
    key="map_center_lon",
)

frame_df, _meta = aggregate_to_frames(
    raw_df,
    step_minutes=settings.step_minutes,
    window=selected_window,
)

adc_ranges = _fetch_weekly_range(settings.__dict__)
if not frame_df.empty and adc_ranges:
    ranges_df = pd.DataFrame([
        {"station_id": k, "_adc_min": v[0], "_adc_max": v[1]}
        for k, v in adc_ranges.items()
        if v[1] > v[0]
    ])
    if not ranges_df.empty:
        frame_df = frame_df.merge(ranges_df, on="station_id", how="left")
        mask = frame_df["_adc_min"].notna()
        frame_df.loc[mask, "value"] = (
            (frame_df.loc[mask, "value"] - frame_df.loc[mask, "_adc_min"]) /
            (frame_df.loc[mask, "_adc_max"] - frame_df.loc[mask, "_adc_min"]) * 100
        ).clip(0, 100)
        frame_df = frame_df.drop(columns=["_adc_min", "_adc_max"])

if raw_df.empty:
    msg = f"No hay datos para el {selected_date.strftime('%d/%m/%Y')} con los filtros actuales."
    if available_dates:
        fechas = ", ".join(d.strftime("%d/%m/%Y") for d in available_dates[:5])
        msg += f" Fechas recientes con datos: {fechas}."
    st.warning(msg)

dren_lines   = load_dren_lines() if show_dren else None
cuenca_data  = load_cuenca()     if show_cuenca else None
industries   = load_industries() if show_industries else None

fig = build_animated_map(
    frame_df,
    center_lat=float(map_center_lat),
    center_lon=float(map_center_lon),
    zoom=float(map_zoom),
    blur=int(blur),
    high_threshold=float(high_threshold),
    opacity=float(heatmap_opacity),
    show_markers=bool(show_markers),
    marker_size=int(marker_size),
    marker_opacity=float(marker_opacity),
    dren_lines=dren_lines,
    dren_width=float(dren_width),
    dren_color="blue",
    cuenca_data=cuenca_data,
    industries=industries,
)
if fig is None:
    st.warning("No se pudo construir la animacion con los datos actuales.")
    st.stop()

st.plotly_chart(fig, use_container_width=True)
