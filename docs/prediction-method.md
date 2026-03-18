# Water Level Prediction Method

## Overview

The BUP CPE River Monitor webapp includes a **Water Level Prediction** chart that forecasts future water levels based on recent sensor readings. It uses **linear regression** (least-squares method) to fit a trend line through historical data and project it forward in time.

---

## How It Works (Step by Step)

### 1. Fetch Historical Data

The system queries Firebase RTDB for all water level readings from the selected node within the chosen history window (1 hour, 6 hours, or 24 hours). Each reading is a pair of:
- **timestamp** (when the reading was taken)
- **waterLevel_cm** (distance from sensor to water surface in centimeters)

### 2. Normalize Timestamps

Raw timestamps are large numbers (Unix milliseconds, e.g., `1741622955000`). Using them directly in regression would cause floating-point precision issues.

To fix this, all timestamps are normalized to **minutes from the first data point**:

```
normalized_time = (timestamp - first_timestamp) / 60000
```

For example, if the first reading is at `1741622955000` and another is at `1741623555000` (10 minutes later), the normalized values are `0` and `10`.

### 3. Linear Regression (Least Squares)

Linear regression finds the straight line `y = mx + b` that best fits the data, where:
- `y` = predicted water level (cm)
- `x` = time (normalized minutes)
- `m` = **slope** (rate of change in cm per minute)
- `b` = **intercept** (baseline water level)

The slope and intercept are calculated using the **ordinary least squares** formulas:

```
        n * Σ(xi * yi) - Σxi * Σyi
m = ─────────────────────────────────
        n * Σ(xi²) - (Σxi)²

        Σyi - m * Σxi
b = ─────────────────────
              n
```

Where:
- `n` = number of data points
- `xi` = normalized timestamp of each reading
- `yi` = water level of each reading
- `Σ` = sum over all data points

**Interpretation of slope:**
- `m > 0` → Water level is **rising** (positive trend)
- `m < 0` → Water level is **falling** (negative trend)
- `m ≈ 0` → Water level is **stable**

### 4. Generate Forecast Points

Using the regression equation, the system calculates predicted water levels from the last actual reading to the end of the forecast window (30 min, 1 hour, 3 hours, or 6 hours ahead):

```
predicted_level = slope * time_in_minutes + intercept
```

Predicted values are clamped to a minimum of 0 (water level cannot be negative).

### 5. Confidence Band (Uncertainty)

The prediction includes a shaded **confidence band** that shows the range of likely values. This band grows wider the further into the future the prediction extends, reflecting increasing uncertainty.

#### Standard Error of Residuals

First, the system calculates how well the regression line fits the historical data:

```
           Σ(yi - ŷi)²
SE = √ ─────────────────
             n - 2
```

Where `ŷi` is the predicted value for each historical point. A larger SE means the historical data has more scatter around the trend line.

#### Uncertainty Formula

At each forecast point, the uncertainty is:

```
uncertainty = SE × 1.96 × (1 + distance_from_last_reading / (n × 0.5))
```

Where:
- `1.96` corresponds to a **95% confidence interval** (from the standard normal distribution)
- `distance_from_last_reading` = minutes beyond the last actual data point
- `n` = number of historical data points (more data → tighter band)

The upper and lower bounds of the band are:
```
upper = predicted + uncertainty
lower = predicted - uncertainty
```

### 6. Alert Threshold Line

If the node has a configured alert threshold (set in Firebase under `devices/{nodeId}/config/alertThreshold_cm`), a horizontal red dashed line is drawn across the chart. This makes it visually clear whether the predicted water level is expected to cross the danger threshold.

---

## Chart Elements

| Element | Color | Description |
|---------|-------|-------------|
| Solid blue line | `#4fc3f7` | Actual historical water level readings |
| Dashed orange line | `#ff7043` | Predicted future water level (trend) |
| Shaded orange area | `#ff704322` | 95% confidence band (likely range) |
| Dashed red line | `#e74c3c` | Alert threshold (if configured) |

---

## Controls

| Control | Options | Effect |
|---------|---------|--------|
| **Node** | Node 01, 02, 03 | Selects which sensor node to analyze |
| **History** | 1H, 6H, 24H | How much past data to use for the regression |
| **Forecast** | 30min, 1hr, 3hr, 6hr | How far ahead to project the trend |

**Tips:**
- Use a **longer history** for more stable predictions (less affected by short-term noise)
- Use a **shorter history** to capture rapid recent changes (e.g., during a storm)
- **Longer forecasts** will have wider confidence bands (more uncertainty)

---

## Limitations

1. **Linear model only** -- The prediction assumes water level changes at a constant rate. It cannot predict sudden surges, exponential rises, or cyclical patterns.

2. **Extrapolation risk** -- The further into the future the forecast extends, the less reliable it becomes. A 6-hour forecast based on 1 hour of data is speculative.

3. **Minimum 2 data points** -- The regression requires at least 2 readings. If only 1 or 0 readings exist in the history window, only actual data is shown (no prediction).

4. **Garbage in, garbage out** -- If the ultrasonic sensor produces erratic readings (e.g., obstructions, sensor malfunction), the prediction will reflect that noise.

5. **No external factors** -- The model does not account for weather forecasts, upstream dam releases, or other external variables that affect river levels.

---

## Example

Suppose the last 6 hours of data for Node 01 show:

| Time | Water Level |
|------|-------------|
| 10:00 | 45.2 cm |
| 10:10 | 45.8 cm |
| 10:20 | 46.3 cm |
| 10:30 | 46.9 cm |
| 10:40 | 47.5 cm |
| 10:50 | 48.0 cm |

The regression finds:
- **Slope**: ~0.56 cm/min (water rising at 0.56 cm every minute)
- **Intercept**: ~45.2 cm

1-hour forecast (to 11:50):
- Predicted at 11:50: `0.56 × 110 + 45.2 = 106.8 cm`
- Confidence band widens from ±2 cm at 10:50 to ±15 cm at 11:50

If the alert threshold is 100 cm, the red dashed line at 100 cm would show that the predicted trend crosses it around 11:38, giving an early warning.
