# OSF C++ examples

Four small programs that demonstrate the public `osf::` API.
Build them with `-D OSF_BUILD_EXAMPLES=ON` (the default).

## inspect

Print file-level metadata (version, creator, compression) and a one-line
summary for every channel.

```
inspect examples/weather_station.osfz
inspect examples/generated/osf5_mixed.osf
```

## dump

Print the first N samples of a channel (default 20).

```
dump examples/generated/osf5_mixed.osf
dump examples/generated/osf5_mixed.osf "Channel_0" 5
dump examples/weather_station.osfz "temperature" 10
```

## write

Generate a small OSF5 file from scratch: one equidistant float channel
(50-sample sine at 100 Hz) and one timestamped double channel (5 events).

```
write out.osf
```

## copy

Round-trip an OSF / OSFZ file through the library (load → write OSF5 → reload)
and confirm that the channel count is preserved.

```
copy examples/weather_station.osfz copy_out.osf
copy examples/generated/osf5_mixed.osf copy_out.osf
```
