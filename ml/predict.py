"""Production forecast cron.

Pulls last 6h of all 3 stations from Firebase RTDB, fits SARIMAX with upstream
exogenous lags, and writes 10/30/60-min Centro water-level forecasts back to
/devices/station-01/forecast/{horizon}.

Designed to run from GitHub Actions every 5 minutes. Auth via the
FIREBASE_SERVICE_ACCOUNT secret (full JSON of a service account key) and the
FIREBASE_DB_URL secret (the RTDB URL).
"""

from __future__ import annotations

import json
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
import pandas as pd
import firebase_admin
from firebase_admin import credentials, db
from statsmodels.tsa.statespace.sarimax import SARIMAX

sys.path.insert(0, str(Path(__file__).parent))
from lib.preprocess import (  # noqa: E402
    MAX_DELTA_CM_PER_MIN,
    MAX_FFILL_MINUTES,
    SENSOR_HEIGHT_CM,
)

CENTRO = "station-01"
NAPO = "station-02"
BALINAD = "station-03"
HORIZONS_MIN = (10, 30, 60)
LOOKBACK_HOURS = 6
DEFAULT_LAGS = {NAPO: 5, BALINAD: 8}


def _load_order() -> tuple:
    cfg_path = Path(__file__).parent / "sarimax_config.json"
    try:
        return tuple(json.loads(cfg_path.read_text()).get("order", [1, 1, 1]))
    except (FileNotFoundError, json.JSONDecodeError) as e:
        print(f"WARN: falling back to (1,1,1): {e}", file=sys.stderr)
        return (1, 1, 1)


ORDER = _load_order()


def _init_firebase() -> None:
    if firebase_admin._apps:
        return
    sa_json = os.environ["FIREBASE_SERVICE_ACCOUNT"]
    db_url = os.environ["FIREBASE_DB_URL"]
    cred = credentials.Certificate(json.loads(sa_json))
    firebase_admin.initialize_app(cred, {"databaseURL": db_url})


def fetch_recent(station_id: str, since_ms: int) -> pd.DataFrame:
    ref = db.reference(f"devices/{station_id}/readings")
    snap = ref.order_by_child("timestamp").start_at(since_ms).get() or {}
    rows = []
    for r in snap.values():
        if not r or "timestamp" not in r or r.get("waterLevel_cm") is None:
            continue
        rows.append({
            "timestamp": pd.to_datetime(r["timestamp"], unit="ms", utc=True),
            "distance_cm": float(r["waterLevel_cm"]),
            "flow_rate": float(r.get("flowRate_lpm") or 0.0),
        })
    return pd.DataFrame(rows).sort_values("timestamp").reset_index(drop=True)


def _sensor_height(station_id: str) -> float:
    cfg = db.reference(f"devices/{station_id}/config").get() or {}
    return float(cfg.get("sensorHeight_cm", SENSOR_HEIGHT_CM))


def clean_and_resample(df: pd.DataFrame, sensor_height: float) -> pd.DataFrame:
    if df.empty:
        return df
    df = df.copy()
    df["water_level"] = sensor_height - df["distance_cm"]
    df = df[(df["water_level"] > 0) & (df["water_level"] < sensor_height - 1e-6)]

    df["dt_min"] = df["timestamp"].diff().dt.total_seconds() / 60.0
    df["dlevel"] = df["water_level"].diff()
    bad = (df["dt_min"] > 0) & (df["dlevel"].abs() / df["dt_min"] > MAX_DELTA_CM_PER_MIN)
    df = df[~bad]

    df = df.set_index("timestamp")[["water_level", "flow_rate"]]
    df = df.resample("1min").mean().ffill(limit=MAX_FFILL_MINUTES)
    return df


def build_exog(centro: pd.DataFrame, napo: pd.DataFrame, balinad: pd.DataFrame) -> pd.DataFrame:
    feats = pd.DataFrame(index=centro.index)
    feats["centro_flow"] = centro["flow_rate"]
    Ln, Lb = DEFAULT_LAGS[NAPO], DEFAULT_LAGS[BALINAD]
    feats[f"napo_level_lag{Ln}"] = napo["water_level"].reindex(feats.index).shift(Ln)
    feats[f"napo_flow_lag{Ln}"] = napo["flow_rate"].reindex(feats.index).shift(Ln)
    feats[f"balinad_level_lag{Lb}"] = balinad["water_level"].reindex(feats.index).shift(Lb)
    feats[f"balinad_flow_lag{Lb}"] = balinad["flow_rate"].reindex(feats.index).shift(Lb)
    return feats


def _persist_exog_for_horizon(exog: pd.DataFrame, steps: int) -> pd.DataFrame:
    last = exog.iloc[-1:].copy()
    future_idx = pd.date_range(exog.index[-1] + pd.Timedelta(minutes=1), periods=steps, freq="1min")
    return pd.concat([last] * steps).set_index(future_idx)


def fit_and_forecast(centro: pd.DataFrame, exog: pd.DataFrame) -> dict:
    common = centro.index.intersection(exog.index)
    y = centro.loc[common, "water_level"].dropna()
    X = exog.loc[y.index].dropna()
    y = y.loc[X.index]

    if len(y) < 60:
        raise RuntimeError(f"Not enough cleaned points to fit (got {len(y)}, need >=60)")

    model = SARIMAX(y, exog=X, order=ORDER,
                    enforce_stationarity=False, enforce_invertibility=False).fit(disp=False)

    out = {}
    for h in HORIZONS_MIN:
        X_future = _persist_exog_for_horizon(X, h)
        fc = model.get_forecast(steps=h, exog=X_future)
        ci = fc.conf_int(alpha=0.05)
        out[str(h)] = {
            "value_cm": float(fc.predicted_mean.iloc[-1]),
            "ci_lower_cm": float(ci.iloc[-1, 0]),
            "ci_upper_cm": float(ci.iloc[-1, 1]),
            "horizon_min": h,
            "generated_at": datetime.now(timezone.utc).isoformat(),
        }
    return out


def main() -> int:
    _init_firebase()
    status_ref = db.reference(f"devices/{CENTRO}/forecast_status")
    try:
        since_ms = int((datetime.now(timezone.utc).timestamp() - LOOKBACK_HOURS * 3600) * 1000)

        centro_h = _sensor_height(CENTRO)
        napo_h = _sensor_height(NAPO)
        bal_h = _sensor_height(BALINAD)

        centro = clean_and_resample(fetch_recent(CENTRO, since_ms), centro_h)
        napo = clean_and_resample(fetch_recent(NAPO, since_ms), napo_h)
        balinad = clean_and_resample(fetch_recent(BALINAD, since_ms), bal_h)

        if centro.empty:
            raise RuntimeError("No Centro readings in lookback window")

        exog = build_exog(centro, napo, balinad)
        forecasts = fit_and_forecast(centro, exog)

        db.reference(f"devices/{CENTRO}/forecast").set(forecasts)
        status_ref.set({
            "ok": True,
            "order": list(ORDER),
            "generated_at": datetime.now(timezone.utc).isoformat(),
        })
        print(json.dumps(forecasts, indent=2))
        return 0
    except Exception as e:
        status_ref.set({
            "ok": False,
            "error": str(e)[:200],
            "failed_at": datetime.now(timezone.utc).isoformat(),
        })
        raise


if __name__ == "__main__":
    sys.exit(main())
