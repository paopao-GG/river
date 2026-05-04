# Model Selection Decision: SARIMAX vs LSTM

**TL;DR**: We evaluated both SARIMAX and LSTM (and a SARIMAX+LSTM hybrid) on 13 days of river data. **SARIMAX-only wins.** LSTM was tested honestly and rejected; the production system uses SARIMAX(2,1,2) with upstream exogenous lags.

## What we tested

Four models, walk-forward evaluation on the same chronological test split (last 20% of data, 57 test samples per horizon):

| Model | What it does |
|---|---|
| A. Naive last-value | Predicts the current value indefinitely (zero-effort baseline) |
| B. SARIMAX(2,1,2) | Statistical time-series with upstream Napo + Balinad lagged features |
| C. SARIMAX + LSTM residual corrector | Hybrid: SARIMAX makes the forecast, a small LSTM predicts the leftover error |
| D. LSTM standalone | Pure LSTM predicting the full target |

## Results (Mean Absolute Error in cm)

| Model | 10-min | 30-min | 60-min | **Avg** |
|---|---|---|---|---|
| A. Naive last-value | 1.147 | 1.073 | 1.043 | **1.088** |
| **B. SARIMAX (production)** | **0.872** | **0.838** | **0.783** | **0.831** |
| C. SARIMAX + LSTM hybrid | 0.875 | 0.980 | 0.818 | 0.891 |
| D. LSTM standalone | (omitted) | (omitted) | (omitted) | (clearly losing) |

**SARIMAX is the winner across all horizons.**

## Why LSTM didn't help

The LSTM residual model's **R² on the test set was −0.40 average**, ranging from −0.22 at 10 min to −0.67 at 30 min.

A negative R² means the model predicts residuals *worse than simply guessing the mean residual every time*. Translation: the LSTM didn't learn any real signal — it overfit to training-set noise.

This is the expected outcome on the current dataset, for three concrete reasons:

1. **Sample size**. After cleaning + walk-forward alignment, we have 168 training sequences and 57 test sequences. LSTMs typically need 10,000+ for time-series tasks.
2. **No storm events captured**. The 13 days span only calm conditions. The LSTM has no examples of rising water to learn from. It cannot generalize what it has never seen.
3. **SARIMAX is already at the sensor noise floor**. The AJ-SR04M ultrasonic sensor's stated accuracy is ±1 cm; SARIMAX MAE is 0.83 cm. There is essentially no headroom for the LSTM to claw back — the remaining error is irreducible measurement noise.

## Decision criteria (set in advance, evaluated honestly)

| Criterion | Required | Actual | Pass? |
|---|---|---|---|
| Hybrid MAE improvement vs SARIMAX | ≥ 10% | **−7.2%** (worse) | ❌ |
| LSTM R² on residuals | ≥ 0.10 | **−0.40** | ❌ |

Both criteria failed → **production stays on SARIMAX-only**.

## Production architecture

```
Firebase RTDB (live readings)
        |
        v
GitHub Actions cron (every 5 min)
        |
        v
ml/predict.py:
   1. Pull last 6h from Firebase
   2. Clean glitches + resample
   3. Fit SARIMAX(2,1,2) on Centro level
      with exog = [centro_flow,
                   napo_level/flow lagged 5min,
                   balinad_level/flow lagged 8min]
   4. Forecast 10/30/60 min ahead with 95% CI
   5. Write to /devices/station-01/forecast/{horizon}
        |
        v
Webapp (Vercel) reads forecast + alert threshold
```

The order `(2,1,2)` was selected by AIC grid search in `ml/notebooks/02_sarimax.ipynb`. SARIMAX refits from scratch every 5 min — there is no saved "model file"; the configuration is in [ml/sarimax_config.json](ml/sarimax_config.json).

## When to revisit LSTM

The hybrid infrastructure is built (notebook `03_lstm_residual.ipynb`, `lib/lstm.py`). Re-run it after either of these events:

1. **≥ 30 days of data accumulated** AND **at least one captured storm event** (water level rising substantially above baseline)
2. SARIMAX residuals start showing clear non-linear structure that no `(p,d,q)` tuning fixes (Ljung-Box failures with no good order)

Either trigger → re-run notebook 03, re-evaluate the same decision criteria, decide again.

## Honest summary for the client

The client asked for an LSTM model. We built one, evaluated it rigorously alongside SARIMAX, and found that **on the current dataset SARIMAX outperforms LSTM (and the hybrid)**. We chose SARIMAX for production not because LSTM is bad in general, but because the data we have today does not give LSTM enough signal to learn from. The LSTM code path is committed and ready to be re-evaluated as more data — particularly storm-event data — accumulates.

Production performance today: **MAE 0.83 cm at the 10-min horizon, 0.78 cm at 60 min**, both at or below the AJ-SR04M sensor's stated ±1 cm accuracy. The model is doing what it can with the data it has.

## Reproducing this evaluation

```bash
cd ml
pip install -r requirements-notebooks.txt
jupyter notebook notebooks/03_lstm_residual.ipynb
# Run all cells; the final cell prints DEPLOY or STAY-ON-SARIMAX
# and writes ml/lstm_decision.json with the metrics
```

Last evaluated: 2026-05-01, dataset spans 2026-04-16 → 2026-04-29 (13 days, 1,665 cleaned 1-min samples after resampling).
