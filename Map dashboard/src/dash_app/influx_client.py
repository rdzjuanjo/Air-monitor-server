from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from typing import Optional

import pandas as pd
from influxdb_client import InfluxDBClient

from .config import Settings


@dataclass(frozen=True)
class QueryWindow:
    start: datetime
    stop: datetime


def _ensure_utc(ts: datetime) -> datetime:
    if ts.tzinfo is None:
        return ts.replace(tzinfo=timezone.utc)
    return ts.astimezone(timezone.utc)


def default_window(hours: int) -> QueryWindow:
    stop = datetime.now(timezone.utc)
    start = stop - timedelta(hours=hours)
    return QueryWindow(start=start, stop=stop)


def _format_time(ts: datetime) -> str:
    return _ensure_utc(ts).isoformat().replace("+00:00", "Z")


def build_flux_query(settings: Settings, window: QueryWindow, station_filter: Optional[list[str]] = None) -> str:
    station_filter = station_filter or []

    fields_expr = (
        f'r._field == "{settings.value_field}" '
        f'or r._field == "{settings.lat_field}" '
        f'or r._field == "{settings.lon_field}"'
    )

    station_filter_expr = ""
    if station_filter:
        joined = ", ".join([f'"{s}"' for s in station_filter])
        station_filter_expr = (
            f"\n  |> filter(fn: (r) => contains(value: r.{settings.station_tag}, set: [{joined}]))"
        )

    return f'''
from(bucket: "{settings.influx_bucket}")
  |> range(start: {_format_time(window.start)}, stop: {_format_time(window.stop)})
  |> filter(fn: (r) => r._measurement == "{settings.influx_measurement}")
  |> filter(fn: (r) => {fields_expr}){station_filter_expr}
  |> keep(columns: ["_time", "_field", "_value", "{settings.station_tag}"])
  |> pivot(rowKey:["_time", "{settings.station_tag}"], columnKey:["_field"], valueColumn:"_value")
  |> sort(columns:["_time"])
'''.strip()


def query_measurements(
    settings: Settings,
    window: QueryWindow,
    station_filter: Optional[list[str]] = None,
) -> pd.DataFrame:
    query = build_flux_query(settings=settings, window=window, station_filter=station_filter)

    with InfluxDBClient(
        url=settings.influx_url,
        token=settings.influx_token,
        org=settings.influx_org,
        timeout=30_000,
    ) as client:
        data = client.query_api().query_data_frame(query)

    if isinstance(data, list):
        if not data:
            return pd.DataFrame(columns=["timestamp", "station_id", "lat", "lon", "value"])
        raw = pd.concat(data, ignore_index=True)
    else:
        raw = data

    if raw.empty:
        return pd.DataFrame(columns=["timestamp", "station_id", "lat", "lon", "value"])

    station_col = settings.station_tag
    value_col = settings.value_field
    lat_col = settings.lat_field
    lon_col = settings.lon_field

    required = ["_time", station_col, value_col, lat_col, lon_col]
    missing = [col for col in required if col not in raw.columns]
    if missing:
        raise ValueError(
            "Missing expected columns after Flux query: " + ", ".join(missing)
        )

    out = pd.DataFrame(
        {
            "timestamp": pd.to_datetime(raw["_time"], utc=True, errors="coerce"),
            "station_id": raw[station_col].astype(str),
            "lat": pd.to_numeric(raw[lat_col], errors="coerce"),
            "lon": pd.to_numeric(raw[lon_col], errors="coerce"),
            "value": pd.to_numeric(raw[value_col], errors="coerce"),
        }
    )

    out = out.dropna(subset=["timestamp", "station_id", "lat", "lon", "value"]) 
    out = out[(out["lat"].between(-90, 90)) & (out["lon"].between(-180, 180))]

    return out.sort_values("timestamp").reset_index(drop=True)


def list_stations(settings: Settings, window: QueryWindow) -> list[str]:
    df = query_measurements(settings=settings, window=window)
    if df.empty:
        return []
    return sorted(df["station_id"].unique().tolist())
