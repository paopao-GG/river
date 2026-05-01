"""Data loading + cleaning for river-data CSVs and Firebase RTDB readings.

Cleaning rules (per docs/project.md and the dataset's known glitch behavior):
  - Drop rows where water_level == sensor_height (clamped sentinel; AJ-SR04M glitch)
  - Drop rows where |delta_level / delta_t| > 50 cm/min (physically implausible)
  - Resample to uniform 1-min grid; forward-fill gaps <= 5 min, drop longer gaps
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
import pandas as pd

SENSOR_HEIGHT_CM = 200          # default; matches docs/project.md
MAX_DELTA_CM_PER_MIN = 50       # physical sanity cap
MAX_FFILL_MINUTES = 5

STATION_NAME_TO_ID = {
    "Centro Station": "station-01",
    "Napo Station": "station-02",
    "Balinad Station": "station-03",
}


def load_csv_dir(csv_dir: str | Path) -> pd.DataFrame:
    """Load all river-data-*.csv files under csv_dir into one long-format frame."""
    csv_dir = Path(csv_dir)
    files = sorted(csv_dir.glob("river-data-*.csv"))
    if not files:
        raise FileNotFoundError(f"No river-data-*.csv files in {csv_dir}")

    frames = [pd.read_csv(f) for f in files]
    df = pd.concat(frames, ignore_index=True)

    df = df.rename(columns={
        "Station": "station_name",
        "Water Level (cm)": "water_level",
        "Flow Rate (L/min)": "flow_rate",
        "DateTime": "datetime",
    })
    df["timestamp"] = pd.to_datetime(df["datetime"], format="mixed", errors="coerce")
    df = df.dropna(subset=["timestamp"])
    df["station_id"] = df["station_name"].map(STATION_NAME_TO_ID)
    df = df.dropna(subset=["station_id"])
    return df[["timestamp", "station_id", "water_level", "flow_rate"]]


def clean_station(df: pd.DataFrame, sensor_height: float = SENSOR_HEIGHT_CM) -> pd.DataFrame:
    """Apply glitch + physical-plausibility filters to a single-station frame."""
    df = df.sort_values("timestamp").drop_duplicates(subset="timestamp").copy()

    # Drop clamped sentinel (AJ-SR04M glitch hits the upper rail)
    df = df[df["water_level"] < sensor_height - 1e-6]
    # Also drop zero (clamped lower rail) if it appears
    df = df[df["water_level"] > 1e-6]

    # Physically implausible deltas
    df["dt_min"] = df["timestamp"].diff().dt.total_seconds() / 60.0
    df["dlevel"] = df["water_level"].diff()
    bad = (df["dt_min"] > 0) & (df["dlevel"].abs() / df["dt_min"] > MAX_DELTA_CM_PER_MIN)
    df = df[~bad].drop(columns=["dt_min", "dlevel"])

    return df.reset_index(drop=True)


def resample_1min(df: pd.DataFrame) -> pd.DataFrame:
    """Resample a cleaned single-station frame to 1-minute uniform grid."""
    df = df.set_index("timestamp").sort_index()
    df = df[["water_level", "flow_rate"]].resample("1min").mean()
    df = df.ffill(limit=MAX_FFILL_MINUTES)
    return df


def build_wide(df_long: pd.DataFrame) -> pd.DataFrame:
    """Build a wide frame indexed by 1-min timestamps, columns per station x metric."""
    parts = []
    for sid, group in df_long.groupby("station_id"):
        cleaned = clean_station(group.drop(columns="station_id"))
        wide = resample_1min(cleaned)
        wide.columns = [f"{sid}_{c}" for c in wide.columns]
        parts.append(wide)
    return pd.concat(parts, axis=1).dropna(how="all")


def cross_correlation(a: pd.Series, b: pd.Series, max_lag_min: int = 60):
    """Return (lags, corr) where corr[i] = corr(a, b.shift(lags[i])).
    Positive lag = b leads a (i.e. b is upstream of a)."""
    a = a.dropna()
    b = b.dropna()
    idx = a.index.intersection(b.index)
    a, b = a.loc[idx], b.loc[idx]
    lags = np.arange(-max_lag_min, max_lag_min + 1)
    out = []
    for k in lags:
        if k >= 0:
            x, y = a.iloc[k:], b.iloc[: len(b) - k]
        else:
            x, y = a.iloc[: len(a) + k], b.iloc[-k:]
        if len(x) < 10:
            out.append(np.nan)
        else:
            out.append(float(np.corrcoef(x.values, y.values)[0, 1]))
    return lags, np.array(out)
