# Water Level Prediction Method

## Overview

The BUP CPE River Monitor webapp includes a **Water Level Prediction** chart that forecasts future water levels for **Centro Station (Node 1)** based on recent sensor readings. It uses **multiple linear regression** with both water level and flow rate data to produce more accurate predictions than a simple trend line.

---

## How It Works (Step by Step)

### 1. Fetch Historical Data

The system queries Firebase RTDB for all readings from Centro Station within the chosen history window (1 hour, 6 hours, or 24 hours). Each reading includes:
- **timestamp** (when the reading was taken)
- **waterLevel_cm** (raw ultrasonic distance, converted to actual water level)
- **flowRate_lpm** (water flow rate in liters per minute)

### 2. Compute Features

For each consecutive pair of readings, the system computes 4 features:

| Feature | Formula | Purpose |
|---------|---------|---------|
| **Time** | `(timestamp - t0) / 60000` | Overall trend (normalized to minutes) |
| **Level change rate** | `(wl[i] - wl[i-1]) / dt` | How fast water is rising/falling (cm/min) |
| **Flow rate** | `flowRate_lpm[i]` | Current flow — leading indicator of level changes |
| **Flow change rate** | `(fr[i] - fr[i-1]) / dt` | Acceleration/deceleration of flow (L/min/min) |

**Why flow rate matters:**
- Rising flow + stable level = level is **about to rise**
- Falling flow + high level = level is **about to drop**
- Flow rate reacts faster than water level to upstream changes

### 3. Multiple Linear Regression (Normal Equation)

The model fits:

```
water_level = b0 + b1 * time + b2 * level_change_rate + b3 * flow_rate + b4 * flow_change_rate
```

The coefficients `[b0, b1, b2, b3, b4]` are solved using the **normal equation**:

```
B = (X^T X)^-1 X^T Y
```

Where:
- `X` = matrix of features (with intercept column of 1s)
- `Y` = vector of water level values
- `B` = coefficient vector

The system solves this via **Gaussian elimination with partial pivoting** for numerical stability.

**Fallback:** If there aren't enough data points for multivariate regression (need at least 6), the system falls back to simple time-based linear regression.

### 4. Generate Forecast Points

Using the regression model, the system projects water levels from the last reading to the forecast horizon (30 min, 1 hour, 3 hours, or 6 hours).

For multivariate predictions, the rates (level change, flow change) **decay exponentially** over the forecast period:

```
decayed_rate = last_rate × e^(-2 × progress)
```

Where `progress` goes from 0 (start of forecast) to 1 (end). This prevents runaway predictions — the model assumes rates gradually return toward zero rather than continuing indefinitely.

Predicted values are clamped to **0–200 cm** (max water level = 2 meters).

### 5. Confidence Band (Uncertainty)

The prediction includes a shaded **95% confidence band** that widens over time.

#### Standard Error

```
           Σ(yi - ŷi)²
SE = √ ─────────────────
           n - k - 1
```

Where `k` is the number of features (4 for multivariate, 1 for fallback).

#### Uncertainty at each forecast point

```
uncertainty = SE × 1.96 × (1 + distance_from_last_reading / (n × 0.5))
```

- `1.96` = 95% confidence interval (standard normal)
- The band grows wider with distance from the last actual reading
- More historical data points = tighter band

### 6. Alert Threshold Line

If the node has a configured `alertThreshold_cm` in Firebase, a horizontal red dashed line is drawn. This shows whether the predicted water level is expected to cross the danger threshold.

---

## Chart Elements

| Element | Color | Description |
|---------|-------|-------------|
| Solid blue line | `#4fc3f7` | Actual historical water level readings |
| Green dashed line | `#81c784` | Flow rate (L/min) on right axis |
| Dashed orange line | `#ff7043` | Predicted future water level |
| Shaded orange area | `#ff704322` | 95% confidence band (likely range) |
| Dashed red line | `#e74c3c` | Alert threshold (if configured) |

---

## Controls

| Control | Options | Effect |
|---------|---------|--------|
| **Node** | Centro Station (Node 1) | Fixed to Centro Station |
| **History** | 1H, 6H, 24H | How much past data to use for regression |
| **Forecast** | 30min, 1hr, 3hr, 6hr | How far ahead to project |

**Tips:**
- Use a **longer history** for more stable predictions (less affected by short-term noise)
- Use a **shorter history** to capture rapid recent changes (e.g., during a storm)
- **Longer forecasts** will have wider confidence bands (more uncertainty)
- Watch the **flow rate line** — if flow is rising sharply, expect water level to follow

---

## Limitations

1. **Linear relationships assumed** -- The model assumes water level changes linearly with the features. It cannot capture non-linear dynamics like exponential flood surges.

2. **Extrapolation risk** -- The further into the future, the less reliable. The exponential decay on rates helps, but long forecasts remain speculative.

3. **Minimum data points** -- Multivariate regression needs at least 6 readings. With fewer, the system falls back to simple linear regression (time only). With fewer than 2, no prediction is shown.

4. **Sensor quality dependent** -- If the ultrasonic sensor produces erratic readings or the flow sensor is clogged, the prediction will reflect that noise.

5. **No external factors** -- The model does not account for weather forecasts, upstream dam releases, rainfall, or other external variables.

6. **Single station** -- Currently predicts Centro Station only. Cross-station lag correlation (using upstream data to predict downstream) is not yet implemented.

---

## Example

Suppose the last hour of data for Centro Station shows:

| Time | Water Level | Flow Rate |
|------|-------------|-----------|
| 10:00 | 45.2 cm | 12.0 L/min |
| 10:10 | 45.8 cm | 14.5 L/min |
| 10:20 | 46.3 cm | 18.0 L/min |
| 10:30 | 46.9 cm | 20.2 L/min |
| 10:40 | 47.5 cm | 19.8 L/min |
| 10:50 | 48.0 cm | 19.5 L/min |

The regression detects:
- **Positive time trend** (level rising ~0.56 cm/min)
- **High flow rate** (sustained ~19 L/min)
- **Flow rate stabilizing** (change rate near zero)

The prediction would show continued rise but at a gradually slowing rate (since the flow rate is no longer increasing), converging toward a plateau — more realistic than a simple straight line that would predict indefinite rise.
