from __future__ import annotations

from dataclasses import dataclass

import pandas as pd

from .influx_client import QueryWindow


@dataclass(frozen=True)
class TransformMeta:
    frame_count: int
    station_count: int
    coverage_mean: float


def aggregate_to_frames(df: pd.DataFrame, step_minutes: int, window: QueryWindow) -> tuple[pd.DataFrame, TransformMeta]:
    if df.empty:
        empty = pd.DataFrame(
            columns=["time_bucket", "station_id", "lat", "lon", "value", "frame_label", "coverage"]
        )
        return empty, TransformMeta(frame_count=0, station_count=0, coverage_mean=0.0)

    data = df.copy()
    data["time_bucket"] = data["timestamp"].dt.floor(f"{step_minutes}min")

    grouped = (
        data.groupby(["time_bucket", "station_id"], as_index=False)
        .agg(lat=("lat", "mean"), lon=("lon", "mean"), value=("value", "mean"))
        .sort_values("time_bucket")
    )

    station_count = grouped["station_id"].nunique()

    start_bucket = pd.Timestamp(window.start).ceil(f"{step_minutes}min")
    end_bucket   = pd.Timestamp(window.stop).floor(f"{step_minutes}min")
    all_buckets  = pd.date_range(start=start_bucket, end=end_bucket, freq=f"{step_minutes}min", tz="UTC")

    grouped = grouped[grouped["time_bucket"].isin(all_buckets)]
    grouped["frame_label"] = (
        grouped["time_bucket"]
        .dt.tz_convert("America/Mexico_City")
        .dt.strftime("%Y-%m-%d %H:%M %Z")
    )

    coverage = (
        grouped.groupby("time_bucket")["station_id"].nunique().rename("stations_seen").reset_index()
    )
    coverage["coverage"] = 100.0 * coverage["stations_seen"] / max(station_count, 1)

    grouped = grouped.merge(coverage[["time_bucket", "coverage"]], on="time_bucket", how="left")

    frame_count = grouped["time_bucket"].nunique()
    coverage_mean = grouped[["time_bucket", "coverage"]].drop_duplicates()["coverage"].mean()

    if pd.isna(coverage_mean):
        coverage_mean = 0.0

    return (
        grouped.reset_index(drop=True),
        TransformMeta(frame_count=int(frame_count), station_count=int(station_count), coverage_mean=float(coverage_mean)),
    )
