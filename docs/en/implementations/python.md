---
title: Python implementation
description: The osfdata package — Python binding to OSF over the Rust foundation
sidebar_position: 4
image: "/img/om_social_card.png"
keywords:
  - OSF
  - Python
  - osfdata
  - PyO3
  - NumPy
last_update:
  date: 2026-06-04
  author: Optimeas GmbH
license: CC-BY-4.0
copyright: © 2026 optiMEAS GmbH und optiMEAS Switzerland GmbH
---

🇩🇪 [German version](../../de/implementations/python.md)

# Python implementation

The Python binding is provided by the **`osfdata`** package (imported as
`osf`). It is not a pure-Python implementation but a thin PyO3 wrapper over
the [Rust foundation](rust.md) `osf-core`: that yields millisecond load times
and numeric data without a copy as NumPy arrays.

```python
import osf

mgr = osf.load("measurement.osf")          # .osf and .osfz
temp = mgr.channel("Sensor.Temperature")
values = temp.samples()                      # NumPy array, dtype matches the OSF type
time   = temp.timestamps_ns()
```

Installation:

```bash
pip install osfdata        # or:  uv pip install osfdata
```

The **full documentation** — supported platforms, API overview
(`DataManager`, `Channel`, `Segment`, `WriterBuilder`), writing, OSFZ and
usage notes — is in the chapter
**[Python integration](../integrations/python.md)**. It is deliberately not
duplicated here.

## Source code and further reading

- Package on PyPI: [pypi.org/project/osfdata](https://pypi.org/project/osfdata/)
- Source code on GitHub: [github.com/optimeas/osf](https://github.com/optimeas/osf),
  directory `implementations/python/`
- Full API: [Python integration](../integrations/python.md)
- Underlying core: [Rust implementation](rust.md)

> This document is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Attribution: optiMEAS GmbH and optiMEAS Switzerland GmbH.
