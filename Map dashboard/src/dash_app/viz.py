from __future__ import annotations

import pandas as pd
import plotly.express as px
import plotly.graph_objects as go


def _add_dren_layer(fig: go.Figure, dren_lines: dict | None, dren_width: float, dren_color: str) -> None:
    if not dren_lines or not dren_lines.get("lat"):
        return
    fig.add_trace(
        go.Scattermapbox(
            lat=dren_lines["lat"],
            lon=dren_lines["lon"],
            mode="lines",
            line={"width": float(dren_width), "color": dren_color},
            hoverinfo="skip",
            name="Dren",
        )
    )


def build_empty_map(
    center_lat: float,
    center_lon: float,
    zoom: float,
    dren_lines: dict | None = None,
    dren_width: float = 1.5,
    dren_color: str = "blue",
):
    fig = go.Figure(go.Scattermapbox())
    fig.update_layout(
        mapbox_style="open-street-map",
        mapbox={"center": {"lat": float(center_lat), "lon": float(center_lon)}, "zoom": float(zoom)},
        margin={"r": 0, "t": 50, "l": 0, "b": 0},
        title="Sin datos para mostrar",
        height=650,
        uirevision="map-interaction",
    )
    _add_dren_layer(fig, dren_lines, dren_width, dren_color)
    return fig


HEATMAP_COLORSCALE = [
    [0.0, "#1a9850"],
    [0.5, "#fee08b"],
    [1.0, "#d73027"],
]


def build_animated_map(
    df: pd.DataFrame,
    center_lat: float,
    center_lon: float,
    zoom: float,
    blur: int,
    high_threshold: float,
    opacity: float = 0.8,
    show_markers: bool = False,
    marker_size: int = 16,
    marker_opacity: float = 1.0,
    dren_lines: dict | None = None,
    dren_width: float = 1.5,
    dren_color: str = "blue",
):
    if df.empty:
        return build_empty_map(
            center_lat=center_lat,
            center_lon=center_lon,
            zoom=zoom,
            dren_lines=dren_lines,
            dren_width=dren_width,
            dren_color=dren_color,
        )

    cmin = float(df["value"].min())
    cmax = max(float(high_threshold), cmin + 1e-9)

    fig = px.density_mapbox(
        df,
        lat="lat",
        lon="lon",
        z="value",
        radius=max(1, int(blur)),
        opacity=float(opacity),
        hover_name="station_id",
        hover_data={"value": ":.3f", "frame_label": True, "coverage": ":.1f"},
        animation_frame="frame_label",
        color_continuous_scale=HEATMAP_COLORSCALE,
        range_color=[cmin, cmax],
        zoom=zoom,
        height=650,
    )

    fig.update_layout(
        title=None,
        mapbox_style="open-street-map",
        mapbox={"center": {"lat": float(center_lat), "lon": float(center_lon)}, "zoom": float(zoom)},
        margin={"r": 0, "t": 80, "l": 0, "b": 0},
        uirevision="map-interaction",
        coloraxis_showscale=False,
    )

    # Mueve los controles de animacion (play/pause y linea de tiempo) arriba
    # del mapa, justo debajo del titulo de la pagina, en el mismo nivel
    # (el boton a la izquierda y la linea de tiempo ocupando el resto del ancho).
    if fig.layout.updatemenus:
        updatemenu = fig.layout.updatemenus[0].to_plotly_json()
        updatemenu.update({"x": 0.0, "y": 1.15, "xanchor": "left", "yanchor": "top"})
        fig.update_layout(updatemenus=[updatemenu])
    if fig.layout.sliders:
        slider = fig.layout.sliders[0].to_plotly_json()
        slider.update({"x": 0.15, "y": 1.15, "len": 0.85, "xanchor": "left", "yanchor": "top"})
        fig.update_layout(sliders=[slider])

    if show_markers:
        # Marcador solido coloreado con la misma escala/rango que el heatmap,
        # que se mueve, cambia de color y de tamaño en cada frame de la
        # animacion (igual que el heatmap), ya que se agrega tanto al estado
        # inicial (fig.data) como a cada fig.frames[i] (frame.data + traces).
        size_min = 4.0
        size_range = max(cmax - cmin, 1e-9)

        def _marker_trace(sub: pd.DataFrame) -> go.Scattermapbox:
            normalized = ((sub["value"] - cmin) / size_range).clip(0.0, 1.0)
            sizes = size_min + normalized * (float(marker_size) - size_min)
            return go.Scattermapbox(
                lat=sub["lat"],
                lon=sub["lon"],
                mode="markers+text",
                marker={
                    "size": sizes,
                    "sizemin": size_min,
                    "opacity": float(marker_opacity),
                    "color": sub["value"],
                    "colorscale": HEATMAP_COLORSCALE,
                    "cmin": cmin,
                    "cmax": cmax,
                    "showscale": False,
                },
                text=sub["station_id"],
                textposition="top right",
                hovertext=[
                    f"{s}: {v:.3f}" for s, v in zip(sub["station_id"], sub["value"])
                ],
                hoverinfo="text",
                name="Estaciones",
            )

        groups = {label: sub for label, sub in df.groupby("frame_label")}

        new_frames = []
        for frame in fig.frames:
            sub = groups.get(frame.name, df.iloc[0:0])
            frame.data = list(frame.data) + [_marker_trace(sub)]
            frame.traces = [0, 1]
            new_frames.append(frame)
        fig.frames = new_frames

        first_label = fig.frames[0].name if fig.frames else None
        first_sub = groups.get(first_label, df.iloc[0:0])
        fig.add_trace(_marker_trace(first_sub))

    # Capa estatica del dren, agregada al final y fuera de frame.traces para
    # que permanezca fija durante toda la animacion.
    _add_dren_layer(fig, dren_lines, dren_width, dren_color)

    return fig
