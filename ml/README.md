# River level forecasting (Centro)

SARIMAX water-level forecast for **Centro Station** using upstream (Napo + Balinad) lagged exogenous features.

## Layout
- `lib/preprocess.py` — CSV loader, glitch filter, 1-min resample, cross-correlation
- `notebooks/01_explore.ipynb` — clean, plot, compute upstream→Centro lags, write `lags.json`
- `notebooks/02_sarimax.ipynb` — fit SARIMAX, compare against naive + linear-regression baselines
- `predict.py` — production cron (pulls last 6h from Firebase, fits, writes `/devices/station-01/forecast/{10,30,60}`)
- `requirements.txt` — `statsmodels`, `pandas`, `numpy`, `firebase-admin`, `matplotlib`

## Local setup
```bash
python -m venv .venv && source .venv/bin/activate    # or .venv\Scripts\activate on Windows
pip install -r ml/requirements-notebooks.txt    # for notebooks (no firebase-admin)
# pip install -r ml/requirements.txt            # for predict.py / production cron
jupyter notebook ml/notebooks/01_explore.ipynb
```

Run notebook 01 first — it writes `ml/lags.json` which notebook 02 and `predict.py` consume.

## Production cron
GitHub Actions workflow at `.github/workflows/forecast-cron.yml` runs `predict.py` every 5 min. Required repository secrets:

- `FIREBASE_SERVICE_ACCOUNT` — full JSON of a Firebase service-account key (Project settings → Service accounts → Generate new key)
- `FIREBASE_DB_URL` — e.g. `https://your-project-default-rtdb.firebaseio.com`

The webapp reads `/devices/station-01/forecast` and overlays SARIMAX points + CI on the prediction chart, plus shows a banner when the 95% upper bound of any horizon exceeds the configured `alertThreshold_cm`.

## Honest performance note
On the current ~8-day calm dataset, SARIMAX MAE will land near the AJ-SR04M sensor noise floor (~2 cm). The infrastructure exists so that when a real rain event arrives, the upstream-lag exogenous features can capture rising water at Napo/Balinad before it hits Centro. Re-run the notebooks after the first storm to validate and refit.

## LSTM (deferred)
Out of scope until ≥30 days of data including at least one storm event. Statistical augmentation alone (jitter, window-slicing) will not help — the model needs real flood patterns. See plan at `~/.claude/plans/water-level-forecast-plan.md` Phase 4 for the physics-based synthetic-flood approach if revisited.
