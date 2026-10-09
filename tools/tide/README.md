# tools/tide: oracles for the tide engine's test data

Nothing here builds or runs in CI. These are the programs that produced the pinned numbers in
`tests/core/tide/` and `tests/fixtures/core/tide/`, kept so the numbers can be regenerated and
checked (docs/harmonics-engine.md §2.6, §8.1).

| File | Produces |
|---|---|
| `congen_golden.cpp` | `tests/fixtures/core/tide/congen_golden.csv` (data rows): libcongen's speeds, (V₀+u) and f for every catalogue entry in 19 years from 1700 to 2200 |
| `proto_predict.py` | the design prototype (HE §1): SP 98 astronomy, the yearly and instantaneous schemes, and the comparison with CO-OPS |
| `vectors.py` | the 2026 vectors pinned in `test_tide_constexpr.cpp` and `test_tide_astronomy.cpp` |
| `vet_anchorage.py` | the 9455920 vetting of HE §1.1 and the catalogue's RP1, KJ2 and 3KM5 notes |

## congen golden table

```sh
work=~/.cache/metoceanviewer-dev/tmp/congen   # scratch, never /tmp on the host
mkdir -p "$work" && cd "$work"
curl -LO https://flaterco.com/files/xtide/congen-1.7-r2.tar.xz
echo "1dbae015e338e5780a505ebf233058f5e91628319a11bfbc465cb556cfb4b0d7  congen-1.7-r2.tar.xz" | sha256sum -c
tar xJf congen-1.7-r2.tar.xz && (cd congen-1.7 && ./configure --disable-shared && make)
g++ -O2 -std=c++17 -I congen-1.7 -o congen_golden "$REPO/tools/tide/congen_golden.cpp" \
    congen-1.7/.libs/libcongen.a -lm
grep -v '^#' "$REPO/tests/fixtures/core/tide/congen_golden.csv" | tail -n +2 | cut -d, -f1 | uniq > names.txt
./congen_golden congen-1.7/congen_input.txt names.txt \
    1700 1750 1800 1850 1899 1900 1901 1950 1970 1999 2000 2024 2026 2027 2050 2100 2150 2199 2200 > body.csv
```

`body.csv` replaces everything after the fixture's `#` header. After a catalogue change, write
`names.txt` from `nos_catalogue` instead (one name per line, catalogue order). The 2026-10-09
run reproduced the committed rows byte for byte.

## Prototype vectors and the Anchorage vetting

```sh
python3 -I tools/tide/vectors.py
python3 -I tools/tide/vet_anchorage.py congen-1.7/congen_input.txt PROTO_DIR [STRIDE]
```

`PROTO_DIR` holds CO-OPS responses (public domain):
`harcon_9455920.json` from
`https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations/9455920/harcon.json?units=metric`,
and `pred_9455920_<begin>.json` from
`https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?product=predictions&datum=MSL&time_zone=gmt&units=metric&interval=6&format=json&station=9455920&begin_date=<begin>&end_date=<end>`
for the windows 20251230–20260102, 20260629–20260704 and 20261001–20261008. The 2026-10-09 run
used responses fetched on 2026-10-08. `proto_predict.py` reads the same files for its other
stations. The H3 fixture recorder (HE §8.2) will commit such responses; until then the RP1 and
KJ2 readings are provisional.
