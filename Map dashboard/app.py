from __future__ import annotations

from datetime import datetime, timezone

import streamlit as st

from src.dash_app.config import SettingsError, load_settings
from src.dash_app.influx_client import default_window, query_measurements
from src.dash_app.transform import aggregate_to_frames, validate_station_coordinates_consistency
from src.dash_app.viz import build_animated_map


st.set_page_config(page_title="Dashboard ambiental 24h", layout="wide")
st.title("Dashboard dinamico espacial - ultimas 24 horas")

# Threshold fijo para valores altos (rojo) en el heatmap.
# Ajustalo segun tu parametro ambiental.
HIGH_THRESHOLD_VALUE = 100.0


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

map_zoom = st.sidebar.slider("Zoom inicial", min_value=1.0, max_value=15.0, value=6.0, step=0.5)
blur = st.sidebar.slider("Blur (modo calor)", min_value=1, max_value=50, value=18, step=1)

if st.sidebar.button("Recargar datos", type="primary"):
    _fetch.clear()

with st.spinner("Consultando InfluxDB..."):
    raw_df = _fetch(settings.__dict__, selected_stations)

if raw_df.empty:
    default_center_lat = 0.0
    default_center_lon = 0.0
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

issues = validate_station_coordinates_consistency(raw_df)

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
    st.warning("No hay datos para las ultimas 24 horas con los filtros actuales.")
    st.stop()

value_min = float(frame_df["value"].min())
high_threshold = float(HIGH_THRESHOLD_VALUE)

if not issues.empty:
    st.warning("Se detectaron estaciones con lat/lon inconsistentes en el periodo.")
    st.dataframe(issues, use_container_width=True)

st.caption("El mapa usa OpenStreetMap. Puedes arrastrar, hacer zoom y moverte libremente.")
st.caption("Escala de color: verde (bajo) -> amarillo (medio) -> rojo (alto).")
st.caption(f"Threshold alto fijo en codigo: {high_threshold:.3f}")

fig = build_animated_map(
    frame_df,
    center_lat=float(map_center_lat),
    center_lon=float(map_center_lon),
    zoom=float(map_zoom),
    blur=int(blur),
    high_threshold=float(high_threshold),
)
if fig is None:
    st.warning("No se pudo construir la animacion con los datos actuales.")
    st.stop()

st.plotly_chart(fig, use_container_width=True)

with st.expander("Muestra de datos agregados (20 min)"):
    st.dataframe(frame_df.head(200), use_container_width=True)
