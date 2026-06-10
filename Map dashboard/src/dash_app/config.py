import os
from dataclasses import dataclass


@dataclass(frozen=True)
class Settings:
    influx_url: str
    influx_token: str
    influx_org: str
    influx_bucket: str
    influx_measurement: str
    station_tag: str
    value_field: str
    lat_field: str
    lon_field: str
    window_hours: int = 24
    step_minutes: int = 20


class SettingsError(RuntimeError):
    pass


def _required_env(name: str) -> str:
    value = os.getenv(name, "").strip()
    if not value:
        raise SettingsError(f"Missing required environment variable: {name}")
    return value


def load_settings() -> Settings:
    return Settings(
        influx_url=_required_env("INFLUX_URL"),
        influx_token=_required_env("INFLUX_TOKEN"),
        influx_org=_required_env("INFLUX_ORG"),
        influx_bucket=_required_env("INFLUX_BUCKET"),
        influx_measurement=os.getenv("INFLUX_MEASUREMENT", "env_monitoring").strip(),
        station_tag=os.getenv("INFLUX_STATION_TAG", "station_id").strip(),
        value_field=os.getenv("INFLUX_VALUE_FIELD", "medicion").strip(),
        lat_field=os.getenv("INFLUX_LAT_FIELD", "lat").strip(),
        lon_field=os.getenv("INFLUX_LON_FIELD", "lon").strip(),
        window_hours=int(os.getenv("DASHBOARD_WINDOW_HOURS", "24")),
        step_minutes=int(os.getenv("DASHBOARD_STEP_MINUTES", "20")),
    )
