# OSF — PyTorch Integration

![Status](https://img.shields.io/badge/status-planned-lightgrey.svg)

## What This Integration Provides

- `OSFDataset`: a `torch.utils.data.Dataset` that streams samples directly from OSF files
- Window-based and event-based sampling strategies
- Multi-channel tensor construction with configurable normalization
- Compatible with `torch.utils.data.DataLoader` (multi-worker, pin-memory)

## Status

**Planned.** Implementation has not started.

## Dependencies

- `osf` Python package (`implementations/python/`)
- `torch >= 2.0`
- `numpy`

## Planned API

```python
from osf.pytorch import OSFDataset
from torch.utils.data import DataLoader

dataset = OSFDataset(
    path="recording.osf5",
    channels=["accel_x", "accel_y", "accel_z"],
    window_size=256,       # samples per training example
    stride=128,            # hop between windows
    transform=None,        # optional torchvision-style transform
)

loader = DataLoader(dataset, batch_size=32, shuffle=True, num_workers=4)

for batch in loader:
    x, labels = batch  # tensors of shape (32, 3, 256)
    loss = model(x)
```

## Notes

OSF's streaming-first design maps naturally onto a Dataset that reads sequentially from disk without loading the entire file into memory. Large recordings (hours of sensor data) can be used as training data without requiring conversion to Parquet or HDF5 first.
