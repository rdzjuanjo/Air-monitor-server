from __future__ import annotations

from datetime import datetime, timezone

import streamlit as st

from src.dash_app.config import SettingsError, load_settings
from src.dash_app.influx_client import default_window, query_measurements
from src.dash_app.transform import aggregate_to_frames
from src.dash_app.viz import build_animated_map


st.set_page_config(page_title="Dashboard ambiental 24h", layout="wide")
st.title("Dashboard dinamico espacial - ultimas 24 horas")

# Threshold fijo para valores altos (rojo) en el heatmap.
# CVOL (MQ135) en ppm: <8 buena, 8-16 media, >16 mala (ver mqGetAirQualityLevel
# en air-monitor/lib/instruments/MQ/MQSensor.h). Con 100 los valores reales
# (2-30 ppm) quedaban casi todos en el extremo verde del rango de color,
# por lo que el punto se veia casi invisible/transparente en el mapa.
HIGH_THRESHOLD_VALUE = 16.0

# Centro por default: punto medio entre El Salto, Jalisco y Las Pintas.
DEFAULT_CENTER_LAT = 20.5470
DEFAULT_CENTER_LON = -103.2537
DEFAULT_ZOOM = 11.5


@st.cache_data(show_spinner=False)
def _fetch(settings_dict: dict, selected_stations: tuple[str, ...]):
    settings = load_settings()
    window = default_window(settings.window_hours)
    station_filter = list(selected_stations) if selected_stations else None
    return query_measurements(settings=settings, window=window, station_filter=station_filter)


try:
    settings = load_settings()
except SettingsError as exc:
    st.error(str(exc))
    st.info("Copia .env.example a .env y exporta las variables antes de ejecutar.")
    st.stop()

st.sidebar.header("Controles")
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
    min_value=1,
    max_value=100,
    value=30,
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
    help="Valor de CVOL (ppm) que se pinta en rojo (extremo de la escala). "
    "Si los valores reales quedan muy por debajo de este numero, los puntos "
    "se ven palidos/transparentes.",
)
heatmap_opacity = st.sidebar.slider(
    "Opacidad del heatmap",
    min_value=0.1,
    max_value=1.0,
    value=0.8,
    step=0.05,
)
show_markers = st.sidebar.checkbox(
    "Mostrar marcador solido por estacion",
    value=False,
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

if st.sidebar.button("Recargar datos", type="primary"):
    _fetch.clear()

with st.spinner("Consultando InfluxDB..."):
    raw_df = _fetch(settings.__dict__, selected_stations)

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

frame_df, meta = aggregate_to_frames(
    raw_df,
    step_minutes=settings.step_minutes,
    now_utc=datetime.now(timezone.utc),
)

col1, col2, col3, col4 = st.columns(4)
col1.metric("Hora actual (UTC)", datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M"))
col2.metric("Frames disponibles", meta.frame_count)
col3.metric("Estaciones activas", meta.station_count)
col4.metric("Cobertura media", f"{meta.coverage_mean:.1f}%")

if raw_df.empty:
    st.warning("No hay datos para las ultimas 24 horas con los filtros actuales. Mostrando mapa vacio.")

st.caption("El mapa usa OpenStreetMap. Puedes arrastrar, hacer zoom y moverte libremente.")
st.caption("Escala de color: verde (bajo) -> amarillo (medio) -> rojo (alto).")
st.caption(
    f"Parametros actuales -> umbral alto: {high_threshold:.3f} | "
    f"radio/blur: {blur} | opacidad: {heatmap_opacity:.2f}"
)
if not frame_df.empty:
    st.caption(
        f"Rango real de 'value' en los datos mostrados: "
        f"{frame_df['value'].min():.3f} - {frame_df['value'].max():.3f}"
    )

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
)
if fig is None:
    st.warning("No se pudo construir la animacion con los datos actuales.")
    st.stop()

st.plotly_chart(fig, use_container_width=True)

with st.expander("Muestra de datos agregados (20 min)"):
    st.dataframe(frame_df.head(200), use_container_width=True)
