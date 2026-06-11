from __future__ import annotations

import pandas as pd
import plotly.express as px
import plotly.graph_objects as go


def build_empty_map(center_lat: float, center_lon: float, zoom: float):
    fig = go.Figure(go.Scattermapbox())
    fig.update_layout(
        mapbox_style="open-street-map",
        mapbox={"center": {"lat": float(center_lat), "lon": float(center_lon)}, "zoom": float(zoom)},
        margin={"r": 0, "t": 50, "l": 0, "b": 0},
        title="Sin datos para mostrar",
        height=650,
        uirevision="map-interaction",
    )
    return fig


def build_animated_map(
    df: pd.DataFrame,
    center_lat: float,
    center_lon: float,
    zoom: float,
    blur: int,
    high_threshold: float,
):
    if df.empty:
        return build_empty_map(center_lat=center_lat, center_lon=center_lon, zoom=zoom)

    cmin = float(df["value"].min())
    cmax = max(float(high_threshold), cmin + 1e-9)

    fig = px.density_mapbox(
        df,
        lat="lat",
        lon="lon",
        z="value",
        radius=max(1, int(blur)),
        hover_name="station_id",
        hover_data={"value": ":.3f", "frame_label": True, "coverage": ":.1f"},
        animation_frame="frame_label",
        color_continuous_scale=[
            [0.0, "#1a9850"],
            [0.5, "#fee08b"],
            [1.0, "#d73027"],
        ],
        range_color=[cmin, cmax],
        zoom=zoom,
        height=650,
        title="Evolucion espacial ultimas 24h (paso 20 min)",
    )

    fig.update_layout(
        mapbox_style="open-street-map",
        mapbox={"center": {"lat": float(center_lat), "lon": float(center_lon)}, "zoom": float(zoom)},
        margin={"r": 0, "t": 50, "l": 0, "b": 0},
        uirevision="map-interaction",
    )

    return fig
