from __future__ import annotations

from datetime import datetime, timedelta, timezone

import pandas as pd
from influxdb_client import InfluxDBClient

ONLINE_THRESHOLD = timedelta(hours=24)
DEVICE_LOOKBACK = "-30d"
HISTORY_LOOKBACK = "-7d"


def _build_latest_query(bucket: str) -> str:
    return f'''
from(bucket: "{bucket}")
  |> range(start: {DEVICE_LOOKBACK})
  |> filter(fn: (r) => r._measurement == "env_monitoring")
  |> group(columns: ["device_id", "_field"])
  |> last()
  |> toFloat()
  |> group()
'''.strip()


def _to_dataframe(data) -> pd.DataFrame:
    if isinstance(data, list):
        if not data:
            return pd.DataFrame()
        return pd.concat(data, ignore_index=True)
    return data


def get_recent_devices(config, hidden_ids: set[str] | None = None) -> list[dict]:
    """Devuelve un dict por device_id con su ultimo dato reportado (hasta 30 dias).

    Incluye un flag 'online' (datos en las ultimas 24h) y ordena los
    dispositivos online primero, offline al final.
    """
    query = _build_latest_query(config["INFLUX_BUCKET"])

    with InfluxDBClient(
        url=config["INFLUX_URL"],
        token=config["INFLUX_TOKEN"],
        org=config["INFLUX_ORG"],
        timeout=30_000,
    ) as client:
        data = client.query_api().query_data_frame(query)

    df = _to_dataframe(data)
    if df.empty:
        return []

    hidden_ids = hidden_ids or set()
    now = datetime.now(timezone.utc)

    devices = []
    for device_id, group in df.groupby("device_id"):
        if device_id in hidden_ids:
            continue

        row: dict = {"device_id": device_id, "last_seen": group["_time"].max()}
        for _, record in group.iterrows():
            row[record["_field"]] = record["_value"]
            for tag in ("fw", "mq135_cal"):
                if tag in group.columns and pd.notna(record.get(tag)):
                    row[tag] = record[tag]

        last_seen = row["last_seen"]
        if last_seen.tzinfo is None:
            last_seen = last_seen.replace(tzinfo=timezone.utc)
        row["online"] = (now - last_seen) < ONLINE_THRESHOLD
        devices.append(row)

    devices.sort(key=lambda d: (not d["online"], d["device_id"]))
    return devices


def _build_history_query(bucket: str, device_id: str) -> str:
    return f'''
from(bucket: "{bucket}")
  |> range(start: {HISTORY_LOOKBACK})
  |> filter(fn: (r) => r._measurement == "env_monitoring")
  |> filter(fn: (r) => r.device_id == "{device_id}")
  |> filter(fn: (r) => r._field == "mq135_adc" or r._field == "mq135_adc0")
  |> toFloat()
'''.strip()


def get_device_history(config, device_id: str) -> dict:
    """Devuelve series de mq135_adc y mq135_adc0 de la ultima semana."""
    query = _build_history_query(config["INFLUX_BUCKET"], device_id)

    with InfluxDBClient(
        url=config["INFLUX_URL"],
        token=config["INFLUX_TOKEN"],
        org=config["INFLUX_ORG"],
        timeout=30_000,
    ) as client:
        data = client.query_api().query_data_frame(query)

    df = _to_dataframe(data)
    empty = {"timestamps": [], "mq135_adc": [], "mq135_adc0": []}
    if df.empty:
        return empty

    pivot = (
        df.sort_values("_time")
        .pivot_table(index="_time", columns="_field", values="_value", aggfunc="last")
        .reset_index()
    )

    return {
        "timestamps": [t.isoformat() for t in pivot["_time"]],
        "mq135_adc": [None if pd.isna(v) else float(v) for v in pivot.get("mq135_adc", [])],
        "mq135_adc0": [None if pd.isna(v) else float(v) for v in pivot.get("mq135_adc0", [])],
    }
