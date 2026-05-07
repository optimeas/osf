# osfdata Examples

Runnable example scripts demonstrating typical use of the `osfdata`
Python package. Each script can be invoked from any directory; bare
filenames are looked up automatically in the repository's top-level
`examples/` folder, while relative or absolute paths are used as given.

## The examples

| File | What it does |
|---|---|
| `01_inspect_channels.py` | Loads an OSF file, picks four random channels, prints metadata plus the first ten samples of each. Useful for getting a feel for an unknown file. |
| `02_list_channels.py` | Prints a complete table of all channels with name, unit, sample count, and data type. The full inventory of a file. |
| `03_motorbike_stats.py` | Computes time spent in each speed band on the motorbike recording, plus exhaust manifold temperature statistics correlated with speed. |
| `04_save_analysis.py` | Same calculations as `03`, but writes the results to a new OSF5 file rather than printing them — demonstrates the writer API. |

## Running

These scripts assume `osfdata` is installed (`maturin develop` for
development, or `pip install osfdata` for a regular install). NumPy
is required as well.

Default invocation, with no argument, loads each script's chosen
default sample file:

    python 01_inspect_channels.py
    python 02_list_channels.py
    python 03_motorbike_stats.py
    python 04_save_analysis.py

Each script accepts an optional file argument:

    python 01_inspect_channels.py weather_station.osfz
    python 02_list_channels.py "V:\path\to\your\own.osf"

See the docstring at the top of each script for full usage details.

## Generated output

`04_save_analysis.py` produces a file `04_motorbike_analysis.osf`
next to the script. This is generated output and is gitignored;
delete it if it bothers you.

## Sample data

The OSF sample files live in the repository's top-level `examples/`
directory (three levels up from this folder). The scripts find them
automatically when you pass a bare filename like `motorbike.osf`.

Available samples:

- `steam_loco.osf` — 123 channels, OSF4 format with mixed timestamped data
- `motorbike.osf` — 81 channels, real-world motorbike telemetry recording
- `weather_station.osfz` — 28 channels, gzip-compressed OSFZ file
- `generated/*.osf` — synthetic test files covering all data types
