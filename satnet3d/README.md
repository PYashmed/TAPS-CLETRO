# TAPS-CLETRO hardware-in-the-loop reproduction package

This project implements fine-grained, time-aware forwarding for dynamic LEO satellite networks. It combines orbital motion, time-varying propagation delay, precomputed routing and gate schedules, and a hardware-in-the-loop (HIL) data path in one experiment workflow.

The package supports the study “Fine-Grained and Time Deterministic Transmission Paradigm for Mega Low Earth Orbit Constellations with Hardware-in-the-Loop Simulation”. It provides six experiment profiles, the corresponding schedule results, packet-level records, analysis scripts and numerical source data for 16 figures and two tables.

## 1\. System requirements

### Software and tested versions

|Component|Version used for package verification|
|-|-|
|Operating system|Ubuntu 24.04.4 LTS, x86-64; Linux 7.0.0-38-generic|
|OMNeT++|6.3.0, build 251110-bb3d6a4731|
|INET|4.6; Ethernet, TSN and NetworkEmulationSupport enabled|
|C++ compiler|Ubuntu Clang 18.1.3|
|GNU Make|4.3|
|Python|3.12.3|
|NumPy / pandas|2.4.6 / 2.3.3|
|Matplotlib / XlsxWriter|3.10.9 / 3.2.9|
|Graphics dependencies in the tested OMNeT++ build|OpenSceneGraph 3.6.5; osgEarth 2.9.0|

The verified platform is the Linux environment above. Python dependency versions are pinned in `simulations/xw/requirements.txt`. The analysis runs on a CPU and supports headless plotting through `MPLBACKEND=Agg`. Orbital-motion and link-delay updates execute sequentially.

The measured machine has an Intel Core i7-13650HX, 20 available logical CPUs and approximately 24 GB RAM. The small demo runs on an ordinary desktop computer. The full archived package is about 10 GB; allow additional space for regenerated data and plots. Full-scene resource requirements depend on the enabled interfaces and statistics.

### Non-standard hardware for a physical HIL run

The logical satellites 424, 484, 427 and 487 are replaced with four programmable time deterministic transmission switches. They require 1 Gbit/s links, VLAN/PCP forwarding, static FDB configuration, time-aware gates, the ability to switch gating based on control messages, and a Linux host with eight data NIC ports. Record the switch model, firmware, physical panel port numbers and clock infrastructure for each deployment alongside its experiment data.

The logical wiring is:

|Host NIC|Simulated boundary|Physical-switch logical port|
|-|-|-|
|enp5s0f0|satellite\[423].eth0|SW424 eth2|
|enp5s0f1|satellite\[425].eth2|SW424 eth0|
|enp5s0f2|satellite\[483].eth0|SW484 eth2|
|enp5s0f3|satellite\[485].eth2|SW484 eth0|
|enp6s0f0|satellite\[544].eth3|SW484 eth1|
|enp6s0f1|satellite\[367].eth1|SW427 eth3|
|enp6s0f2|satellite\[547].eth3|SW487 eth1|
|enp6s0f3, Best Effort|satellite\[486].eth0|SW487 eth2|
|enp6s0f3, Ground Gated/Proposed|satellite\[364].eth1|SW424 eth3|

The final two rows are alternative connections. Direct physical inter-switch connections are SW424 eth1 ↔ SW484 eth3 and SW427 eth1 ↔ SW487 eth3. Their delay is determined by the physical switches and links; geometric ISL-delay emulation on these two connections requires a separately configured delay element. Map these logical names to actual switch panel ports before running. Raw Ethernet socket access needs `CAP\_NET\_RAW` or an equivalent permitted setup.

## 2\. Installation guide

### Install the analysis environment

From this `satnet3d` directory:

```bash
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install -r simulations/xw/requirements.txt
```

Estimated dependency installation time on a desktop with Python installed is a few minutes, depending on download speed. The demo uses OMNeT++, INET, the project library and Python standard-library modules. The pinned Python packages support figure and Source Data analysis.

### Build the simulation library

Install and build OMNeT++ 6.3.0 and INET 4.6 using their supplied instructions. Preserve their own licenses. Enable the INET Ethernet, TSN and NetworkEmulationSupport features. In the tested setup, OMNeT++ has OSG/osgEarth enabled; those development libraries must be available when using that same build.

Use sibling project directories:

```text
workspace/
├── inet4.6/
└── satnet3d/
```

Then, from `satnet3d/`:

```bash
source /path/to/omnetpp-6.3.0/setenv
make makefiles
make -j4
```

For INET at another location:

```bash
make -j4 INET\_ROOT=/absolute/path/to/inet4.6
```

Expected build output: `src/libsatnet3d.so` and build objects under `out/`. A clean build of this package against already built OMNeT++/INET took **21.76 seconds with four build jobs** on the tested machine. Plan for 1–5 minutes on a comparable desktop. This timing covers this project with OMNeT++ and INET already built. Re-run `make makefiles` after changing the source-file set.

## 3\. Demo

The demo executes **10 s of Best Effort virtual simulation** on an **18 × 10 constellation**, withon **one** **flow and a 1 s generation period**. It uses the same packet length, PCP/VLAN, ground coordinates, orbit parameters, link rates and queue settings as the BE experiment. The supplied static route matches the smaller constellation. See the demo details below for the complete parameter table.

### Run

After building the simulation library in Section 2, run from `satnet3d/`:

```bash
python3 simulations/xw/demo/run\_demo.py --verify
```

The runner finds `opp\_run` in the loaded OMNeT++ environment. INET can be provided explicitly with `--inet-root /path/to/inet4.6`. For software checks against a separately built library, use `--library /path/to/libsatnet3d.so`.

### Expected output and runtime

Results are written beneath `simulations/xw/demo/output/`: `flow1.csv`, `BE-Demo-#0.sca`, `simulation.log`, `summary.json` and `run\_info.json`. The supplied reference CSV and summary are in `simulations/xw/demo/expected/`.

Successful verification prints:

```text
PASS: BE virtual simulation matches expected packet records and summary.
```

The verified run took **3.06 s of wall time**, including simulator startup and result writing. The demo uses ordinary CPU hardware and virtual interfaces. Detailed inputs, routing and field tolerances are documented in the demo details below.

## 4\. Instructions for use

### Run a physical HIL experiment

`simulations/xw/hil.ini` keeps common parameters and HIL-only entries. The six `xw\_\*\_omnet\_traffic.ini` files supply their traffic-profile parameters. The TXT importer loads the generated route/GCL inputs directly from `offline/`.

From `satnet3d/simulations/xw/`, with INET in the sibling location above:

```bash
opp\_run -u Cmdenv \\
  -l ../../src/libsatnet3d.so \\
  -l ../../../inet4.6/src/libINET.so \\
  -n ..:../../src:../../../inet4.6/src \\
  -f hil.ini -c HIL-JRS-2TT
```

Use an environment with the raw-socket permissions described above. If INET is elsewhere, replace both its library path and NED path. Configure physical FDB/VLAN entries from the selected TXT `ROUTE` records, and configure gates from its `WINDOWS` records using the matching `satelliteIndexMap` in the traffic INI. Apply these settings through the physical device’s management interface. For Our Proposal/Ground Gated, coordinate GCL changes at 0, 15, 30 and 45 s with the simulation real-time origin; open PCP7 during the 60–61 s drain phase. Best Effort gates are open. Configure link rate and buffers to match the experiment.

In the configuration names and result directories, `BE` denotes the **Best Effort** scheme, `JRS-TER` denotes the **Ground Gated** scheme, and `JRS` denotes **Our Proposal** (TAPS-CLETRO).

|HIL configuration|Output directory relative to `simulations/xw/`|
|-|-|
|HIL-BE-2TT|results/be-2tt|
|HIL-BE-20TT|results/be-20tt|
|HIL-JRS-TER-2TT|results/jrs-ter-2tt|
|HIL-JRS-TER-20TT|results/jrs-ter-20tt|
|HIL-JRS-2TT|results/jrs-2tt|
|HIL-JRS-20TT|results/jrs-20tt|

Each run writes per-flow `flowN.csv`, a configuration/run-specific `.sca`, and redirected `.out` logging. These paths already contain archived records: preserve a copy or use `--result-dir=/path/to/new-run` when running new experiments. Flows generate new originals before 60 s; the run ends at 61 s. The 61 s interval is simulation time. For physical HIL experiments, assess real-time lag and timestamp alignment on the deployment platform. ACK/coordinator behavior is implemented in the simulation model.

### Analyze the archived records and reproduce supplied figures

From `satnet3d/`:

```bash
MPLBACKEND=Agg python simulations/xw/scripts/draw\_2tt.py --delay-only
MPLBACKEND=Agg python simulations/xw/scripts/draw\_2tt.py --jitter-only
MPLBACKEND=Agg python simulations/xw/scripts/draw\_20tt.py
```

The first command generates six TS delay figures, the second regenerates the 2TT range figure from its supplied summary, and the third regenerates the eight 20TT figures. PNG, PDF and SVG outputs are written under `images/2tt/` and `images/20tt/`. To regenerate all 2TT statistics, tables, delivery-rate plot and the large all-flow packet table, run `draw\_2tt.py` without arguments; it loads substantially more data than the demo. Current reproduction uses `draw\_2tt.py` and `draw\_20tt.py`.

Generate and validate the numerical source data from the archived records:

```bash
MPLBACKEND=Agg python simulations/xw/scripts/export\_source\_data.py
```

This creates `simulations/xw/Source Data/`, including the figure/table mapping in `display\_index.csv`. Figure paths in that index identify outputs generated by the plotting commands; pre-rendered figures are distributed separately. On the tested desktop the initial complete export took **130.74 seconds**. It verifies archived 2TT delivery counts, range/RMS/throughput values against raw/scalar evidence, and verifies all 120 20TT flow/sample range values against the plotting function. The manifest covers **18 supplied display items**: 16 figure assets and two tables. Stable asset identifiers link each figure or table to its numerical inputs and analysis command.

### Use your own data

The demo configuration is `simulations/xw/demo/demo.ini`; its static forwarding input is `demo/input/be\_demo\_routes.txt`. Use a separate result directory for a new run:

```bash
python3 simulations/xw/demo/run\_demo.py --output /path/to/results
```

For a modified scenario, copy the INI within the demo directory, adjust the traffic/topology parameters and corresponding forwarding input, and run it with `opp\_run -f demo/my\_demo.ini -c BE-Demo` using the library and NED paths above. The bundled `--verify` mode checks the supplied 18 × 10 reference scenario.

For the full analysis, preserve the six result-directory names and original CSV schemas, and put the corresponding run's SCA in each directory. The scripts select the newest SCA, so keep one matching SCA per analyzed dataset or isolate runs. Ordinary sources include `simTime,endToEndDelay,sequenceNumber,packetBytes`; reliable records also include `attempt,outcome`. Times are seconds, lengths bytes. Match every dataset to its actual generated count, source timing and packet length. The existing 20TT arrival plots use a 59.9 s TS generation cutoff; the 2TT plots use the recorded full generation counts. The `Source Data/README.md` inside the supplied `../Source Data.zip` defines all fields and samples. Extract the archive into `simulations/xw/` to read these files locally.

To change traffic or topology, edit a copied traffic INI together with its matching offline TXT and index/port maps. Generate new routing/GCL schedules in the upstream scheduling workflow, then import the exported TXT results. Check physical boundary coverage again after changing a route.

## 5\. License, source and submission metadata

Source code is distributed as **LGPL-3.0-or-later**, consistent with existing source notices. The full LGPLv3 and incorporated GPLv3 texts are included in `LICENSE`; preserve individual copyright notices. OMNeT++, INET and other dependencies retain their own licenses. The LICENSE file governs source code; data and figure access terms belong to their research records.

The source project's configured GitHub remote is [SatNet3DGroup/satnet3d](https://github.com/SatNet3DGroup/satnet3d). Use the submitted snapshot’s release tag, commit and persistent identifier in the manuscript and data/code availability statements.

## 6\. Data provenance and validation

`Source Data/experimental\_provenance.csv` inside `../Source Data.zip` identifies each archived dataset by its recorded SCA configuration, run ID and seed. The six HIL output-directory mappings define where subsequent experiments write their records. Match each analyzed CSV collection with the SCA from the same run.

The verification workflow covers clean compilation, configuration parsing, software event execution and numerical agreement with the source data. Its commands and measured scope are documented in the environment and verification details below. Physical HIL validation follows the device setup and measurement procedure in Section 4.



## 7\. Detailed environment and verification



The package executes orbital-motion and link-delay updates sequentially and reproduces the archived packet-level statistics. The environment below supports compilation, HIL configuration checks and the analysis workflow.

## Verified platform

|Item|Observed value|
|-|-|
|OS / architecture|Ubuntu 24.04.4 LTS / x86-64|
|Kernel / C library|Linux 7.0.0-38-generic / glibc 2.39|
|Processor|Intel Core i7-13650HX; 20 available logical CPUs|
|Memory|Approximately 24 GB physical RAM|
|OMNeT++|6.3.0; build 251110-bb3d6a4731|
|INET|4.6, INET\_VERSION 0x0406|
|Compiler|Ubuntu Clang 18.1.3|
|Make|GNU Make 4.3|
|OpenSceneGraph|3.6.5|
|osgEarth headers|2.9.0|
|OMNeT++ graphics build flags|WITH\_OSG=yes; WITH\_OSGEARTH=yes|
|Python|3.12.3|
|NumPy|2.4.6|
|pandas|2.3.3|
|Matplotlib|3.10.9, Agg backend|
|Excel writer|XlsxWriter 3.2.9|

INET Ethernet, TSN and NetworkEmulationSupport are enabled. INET is an external dependency, identified here by its installed release version. Analysis uses the CPU and supports headless operation.

## Measured workflow

|Task|Measured time|Measurement scope|
|-|-|-|
|Clean project release build|21.758 s|Four build jobs; prebuilt OMNeT++/INET; isolated source copy|
|Sequential scene execution|85.246 s|0.11 s simulation time; application traffic and physical interfaces disabled|
|BE virtual-simulation demo|3.0563 s|Complete 10 s run; 180 satellites; flow1 period 1 s|
|Source Data export and numerical checks|130.741 s|Archived CSV/SCA inputs; 18 display mappings|

The installation guide estimates 1–5 minutes for the project build on a comparable desktop. Framework installation and dependency downloads are separate setup stages. The timings above describe this verification environment; archived experiment identities are recorded in `Source Data/experimental\_provenance.csv`.

## Validation coverage

* Six HIL configurations parse and map to the six scheme result directories.
* The release library compiles and links from the reduced source tree.
* Sequential scene execution reaches 0.11 s. Recorded movement work is 0.001457667 s; link-delay computation is 0.000160475 s. Both update paths execute during the run.
* Two full BE demo runs produce matching summaries and per-packet records. Actual SCA parameters confirm 18 planes, 10 satellites per plane and 180 instantiated satellites.
* All 18 figure/table mappings resolve to their numerical source data and raw records.
* The 2TT counts, delay range, RMS and throughput agree with the recorded CSV/SCA evidence.
* The 20TT source tables preserve the plot-specific delivered-row filter and 59.9 s TS generation cutoff.

Software verification covers configuration, event execution and numerical correspondence. Physical deployment additionally evaluates device forwarding, GCL alignment, clock precision and real-time lag using the hardware procedure in README Section 4.

### Scene execution check

Append these options to the HIL-JRS-2TT command in the project README to run the scene as a software update check:

```bash
--scheduler-class=omnetpp::cSequentialScheduler --sim-time-limit=0.11s \\
'--\*.stations\[\*].numApps=0' '--\*\*.phyLayer.externalEnabled=false' \\
'--\*\*.phyLayer.enableShadowGuard=false' \\
'--\*.mobilityManager.\*.scalar-recording=true' \\
'--\*.dynamicChannelManager.\*.scalar-recording=true' \\
'--\*\*.param-recording=false' --cmdenv-redirect-output=false \\
--result-dir=/tmp/xw-scene-check
```

The scalar output records `wallMoveSeconds` and `wallComputeSeconds`, which confirm execution of the motion and delay updates.



## 8\. Detailed BE demo inputs and expected output

Paths in this section are relative to `simulations/xw/demo/` unless a command specifies its working directory.



This demo runs flow1 through an **18 × 10 Walker-Delta constellation for 10 s**, with one new original packet every **1 s**. It uses the sequential simulator and virtual Ethernet links.

## Run

Build the project as described in the repository README, then run from `satnet3d/`:

```bash
source /path/to/omnetpp-6.3.0/setenv
python3 simulations/xw/demo/run\_demo.py --verify
```

For INET outside the sibling project directory:

```bash
python3 simulations/xw/demo/run\_demo.py --verify --inet-root /path/to/inet4.6
```

The runner locates the built `src/libsatnet3d.so`, executes `BE-Demo` from `demo.ini` and writes results under `demo/output/`. Python standard-library modules handle execution and verification. The demo uses the OMNeT++/INET installation and ordinary CPU hardware.

The equivalent simulator command, from `simulations/xw/`, is:

```bash
opp\_run -u Cmdenv -l ../../src/libsatnet3d.so \\
  -l ../../../inet4.6/src/libINET.so \\
  -n ..:../../src:../../../inet4.6/src \\
  -f demo/demo.ini -c BE-Demo
```

## Scenario

|Parameter|Value|
|-|-|
|Scheme|Best Effort, virtual simulation|
|Simulation time|10 s|
|Planes × satellites per plane|18 × 10 = 180|
|Business traffic|flow1 only; one source and one destination|
|Generation period / initial offset|1 s / 0 s|
|Packet length / PCP / VLAN|1478 B / 1 / 1|
|Orbital altitude / inclination / phase factor|500 km / 58° / 23|
|Link rate / propagation speed|1 Gbit/s / 299792458 m/s|
|Motion and propagation-delay updates|0.1 s|
|Source coordinates|18.6058617056° N, 125.856377639° W|
|Destination coordinates|41.6523930159° N, 74.2336407027° W|
|Source stopTime|60 s, retained from the original BE flow|
|Ground-access refresh interval|100 s|

The common physical and queue settings come from `../hil.ini`'s General section. The demo selects a separate sequential configuration with normal virtual PHYs and the original BE application types. The destination is renumbered from stations\[11] to stations\[1] after removing the other flows.

The smaller constellation uses access satellites **28** and **163**. The supplied BE route is:

```text
stations\[0] → 28 → 27 → 17 → 7 → 174 → 173 → 163 → stations\[1]
```

`input/be\_demo\_routes.txt` supplies this shortest initial geometric-distance route. Its satellite IDs and access interfaces match the 180-star topology. The parameter table above records the scenario settings.

## Expected output

```text
output/
├── flow1.csv
├── BE-Demo-#0.sca
├── simulation.log
├── summary.json
└── run\_info.json
```

`expected/flow1.csv` and `expected/summary.json` contain the reproducible reference result. Verification reports:

```text
PASS: BE virtual simulation matches expected packet records and summary.
```

|Result|Reference value|
|-|-|
|Generated / successfully received originals|11 / 10|
|Received packet sequences|0–9|
|Minimum / maximum delay|120.678 / 120.918 ms|
|Mean delay / delay range|120.7961 / 0.240 ms|
|Measured wall time|3.0563 s|

The source emits at t=0,1,…,10 s because its original stopTime=60 s is retained. The simulator finishes at 10 s, so the final original remains in flight; packets emitted at t=0,…,9 s are delivered. Delay and packet fields are checked against the supplied reference with numerical tolerances documented in `run\_demo.py`.

For another output directory, add `--output /path/to/results`. A comparable desktop can complete this 10 s virtual experiment in a few seconds; the measured time above includes the simulator process startup, initialization, event execution and result writing.