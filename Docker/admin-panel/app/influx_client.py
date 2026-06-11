from __future__ import annotations

import pandas as pd
from influxdb_client import InfluxDBClient


def _build_query(bucket: str) -> str:
    return f'''
from(bucket: "{bucket}")
  |> range(start: -24h)
  |> filter(fn: (r) => r._measurement == "env_monitoring")
  |> group(columns: ["device_id", "_field"])
  |> last()
  |> toFloat()
  |> group()
'''.strip()


def get_recent_devices(config) -> list[dict]:
    """Devuelve un dict por device_id con su ultimo dato reportado en 24h."""
    query = _build_query(config["INFLUX_BUCKET"])

    with InfluxDBClient(
        url=config["INFLUX_URL"],
        token=config["INFLUX_TOKEN"],
        org=config["INFLUX_ORG"],
        timeout=30_000,
    ) as client:
        data = client.query_api().query_data_frame(query)

    if isinstance(data, list):
        if not data:
            return []
        df = pd.concat(data, ignore_index=True)
    else:
        df = data

    if df.empty:
        return []

    devices = []
    for device_id, group in df.groupby("device_id"):
        row: dict = {"device_id": device_id, "last_seen": group["_time"].max()}
        for _, record in group.iterrows():
            row[record["_field"]] = record["_value"]
            for tag in ("fw", "mq135_cal"):
                if tag in group.columns and pd.notna(record.get(tag)):
                    row[tag] = record[tag]
        devices.append(row)

    return sorted(devices, key=lambda d: d["device_id"])
