# Multi-adapter measurements

The driver has per-adapter software isolation and lifecycle fixture coverage,
plus runtime checks with one physical adapter. Simultaneous operation with two
or three physical adapters, shared-hub contention and hardware recovery remain
unverified. See [driver tests](testing.md) and the
[Linux configuration guide](../README.md).

`tools/multi_adapter_measure.py` records per-adapter debugfs statistics and USB
topology in one JSON file. It calculates interval rates, average scan,
conversion, USB-request, and present-wait latency, queue-pressure percentiles,
watermark crossings, backpressure samples, and counter-reset events. The
collector identifies each adapter by its USB interface path and records its
USB bus and root-port path, so DRM `cardN` numbering can change between runs
without losing the USB identity.

Run a separate capture for each workload and topology. The output path must not
already exist:

```sh
python3 tools/multi_adapter_measure.py \
  --scenario "two-adapters-shared-hub-video-on-a-input-on-b" \
  --duration 120 --interval 1 \
  --output results/two-adapters-shared-hub.json
```

The scenario label should state adapter count, USB topology, and workload. Use
matched durations and activity for comparisons. Include an idle baseline, a
busy display with an interactive display, and sustained updates on all
adapters. Record the display resolution, refresh rate, scale, and pixel format
in the scenario notes. The JSON contains each raw debugfs snapshot as well as
the summarized deltas, so derived results can be checked against source values.

The summary distinguishes attempted requests, successful transfers, bytes,
timeouts, short transfers, scan/conversion work, and queue pressure. USB
payload throughput is reported in MiB/s. Scan and conversion rates use the
interval deltas of their pixel and duration counters. Queue pressure is sampled
at the requested interval; p50/p95/max and low/high-watermark crossings describe
those samples, not every instant between samples. A counter decrease is
reported as a reset, and its invalid interval is excluded from that counter's
rate.

This tool measures driver accounting and captures topology; it does not
attribute CPU time to a particular adapter or prove that completed USB data was
displayed. Use a separate system profiler for CPU fairness, and compare the
driver counters with what each display visibly shows. It cannot measure the
memory cost attributable to each probe. Simultaneous two-/three-adapter runs
require those physical adapters.
