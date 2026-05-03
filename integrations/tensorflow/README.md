# OSF — TensorFlow Integration

![Status](https://img.shields.io/badge/status-planned-lightgrey.svg)

## What This Integration Provides

- `osf_dataset()`: a `tf.data.Dataset` factory that reads OSF files as a TensorFlow data pipeline
- Windowed sampling compatible with `tf.keras.Model.fit()`
- Automatic dtype mapping from OSF channel types to TensorFlow dtypes
- Prefetch and interleave support for multi-file training sets

## Status

**Planned.** Implementation has not started.

## Dependencies

- `osf` Python package (`implementations/python/`)
- `tensorflow >= 2.12` or `tensorflow-cpu`
- `numpy`

## Planned API

```python
from osf.tensorflow import osf_dataset

ds = osf_dataset(
    paths=["run1.osf5", "run2.osf5"],
    channels=["voltage", "current", "temperature"],
    window_size=512,
    stride=256,
    batch_size=64,
    shuffle_buffer=1000,
)

model.fit(ds, epochs=10)
```

## Notes

The `tf.data` pipeline is built on Python generators backed by the OSF Python reader. Samples are produced lazily, keeping memory usage proportional to the window size rather than the file size. Multiple OSF files can be interleaved in a single pipeline, making it straightforward to train on entire recording libraries.
