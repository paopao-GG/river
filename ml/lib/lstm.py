"""LSTM residual corrector for the SARIMAX hybrid pipeline.

Trained to predict the *residual* (actual - SARIMAX prediction) at horizons
{10, 30, 60} min, given a 30-step sequence of multivariate features:
[centro_level, centro_flow, napo_level, napo_flow, balinad_level, balinad_flow].

Deliberately tiny (1 layer, 16 hidden units) because we have ~1100 samples.
"""

from __future__ import annotations

import numpy as np
import torch
import torch.nn as nn

LOOKBACK = 30
N_FEATURES = 6
HORIZONS = (10, 30, 60)


class ResidualLSTM(nn.Module):
    def __init__(self, n_features: int = N_FEATURES, hidden: int = 16, n_horizons: int = len(HORIZONS), dropout: float = 0.3):
        super().__init__()
        self.lstm = nn.LSTM(n_features, hidden, batch_first=True, dropout=0.0)
        self.dropout = nn.Dropout(dropout)
        self.head = nn.Linear(hidden, n_horizons)

    def forward(self, x):  # x: (B, T, F)
        out, _ = self.lstm(x)
        last = out[:, -1, :]              # last timestep
        return self.head(self.dropout(last))   # (B, n_horizons)


def make_sequences(features: np.ndarray, residuals: np.ndarray, lookback: int = LOOKBACK, horizons=HORIZONS):
    """Build (X, Y) for residual training.

    features: (T, F) full feature matrix at 1-min cadence
    residuals: (T, len(horizons)) residual at horizon h for the row indexed by t
               (i.e. residuals[t, j] = y[t + horizons[j]] - sarimax_pred(t + horizons[j]))

    Returns X: (N, lookback, F), Y: (N, len(horizons))
    """
    max_h = max(horizons)
    n = len(features) - lookback - max_h + 1
    if n <= 0:
        raise ValueError(f"Not enough samples: have {len(features)}, need >{lookback + max_h}")
    X = np.stack([features[i : i + lookback] for i in range(n)])
    Y = residuals[lookback - 1 : lookback - 1 + n]
    return X.astype(np.float32), Y.astype(np.float32)


def train_residual_lstm(
    X_train, Y_train, X_val, Y_val,
    epochs: int = 200, batch_size: int = 32, lr: float = 1e-3, patience: int = 20, device: str = "cpu",
):
    """Train with early stopping on val MSE. Returns (model, train_history)."""
    model = ResidualLSTM().to(device)
    opt = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=1e-4)
    loss_fn = nn.MSELoss()

    X_train_t = torch.tensor(X_train, device=device)
    Y_train_t = torch.tensor(Y_train, device=device)
    X_val_t = torch.tensor(X_val, device=device)
    Y_val_t = torch.tensor(Y_val, device=device)

    best_val = float("inf")
    best_state = None
    bad_epochs = 0
    history = {"train": [], "val": []}

    n = len(X_train_t)
    for epoch in range(epochs):
        model.train()
        perm = torch.randperm(n, device=device)
        train_loss_sum = 0.0
        for i in range(0, n, batch_size):
            idx = perm[i : i + batch_size]
            opt.zero_grad()
            pred = model(X_train_t[idx])
            loss = loss_fn(pred, Y_train_t[idx])
            loss.backward()
            opt.step()
            train_loss_sum += loss.item() * len(idx)
        train_loss = train_loss_sum / n

        model.eval()
        with torch.no_grad():
            val_loss = loss_fn(model(X_val_t), Y_val_t).item()
        history["train"].append(train_loss)
        history["val"].append(val_loss)

        if val_loss < best_val - 1e-6:
            best_val = val_loss
            best_state = {k: v.clone() for k, v in model.state_dict().items()}
            bad_epochs = 0
        else:
            bad_epochs += 1
            if bad_epochs >= patience:
                break

    if best_state is not None:
        model.load_state_dict(best_state)
    model.eval()
    return model, history
