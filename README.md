# Race Track

A top-down desktop race track for the **Elecrow CrowPanel 7.0"** (ESP32-S3, 800×480 RGB, GT911 touch).
AI cars race around a circuit in the background, with lap timing, a leaderboard, weather, and day and night.
It uses the same board setup and rendering approach as the koi pond (Desktop Aquarium V2), but it's a separate project.

## What's on the track

- **Circuits**: eleven real-circuit layouts (approximate), plus procedurally generated ones.
  The presets are Monza, Spa-Francorchamps, Silverstone, Interlagos, Red Bull Ring, Circuit of the Americas, Zandvoort, Hungaroring, Watkins Glen, Imola and Road America.
  By default the device alternates between a preset and a new random circuit for each race.
- **Trackside**: red and white kerbs, gravel traps and tarmac run-off sized to each corner, armco and tyre walls, a pit lane with garages and a paddock, grandstands with crowds on the slowest corners, and forests and fields around the outside.
- **Series**: Formula (open-wheelers in team pairs), GT, Touring, Endurance (prototypes and GTs together) and Classic (1960s cigar-shaped cars in national colours).
- **AI drivers** steer like real cars. Each car has a position, heading and speed, and a path-tracking controller follows the line it wants: the racing line, or an offset line when passing or defending.
  Cornering is limited by tyre grip, so a car that arrives too fast slides wide, scrubs speed, and can end up on the grass, in the gravel, in the wall, or spinning.
  Drivers plan their own braking points, slipstream, pick the inside or outside to pass, race side by side, and sometimes make contact.
- **Driver traits**, shown as a coloured dot on the leaderboard and named in the tag of the car you're following:
  - **Aggressive** (red): carries the most speed into corners, brakes latest, attacks even into corners, and needs the least room. They're fastest, but they make the most mistakes, end up in the gravel or the wall, and cause contact.
  - **Balanced** (no dot): the baseline.
  - **Cautious** (blue): a little off the limit, with clean lines, and passes only on straights with a clear advantage.
  - **Patient** (green): takes it easy, rarely attacks, keeps a big gap, and waves faster cars through.
  - **Defensive** (orange): covers the inside when someone attacks.
  Every driver also lets cars that are lapping them through (blue flags).
- **Race weekend loop**: cars form up on the grid behind the start lights, race for the set number of laps, and take the chequered flag.
  The podium is shown, and then the next circuit is built.
  Races can also be endless.
- **Timing**: positions, gaps to the leader (or laps down), last laps, the fastest lap, and a ticker for lead changes, passes for the podium, fastest laps and spins.
- **Weather** from OpenWeatherMap: rain and storms make the track darker and wetter and slow the cars, which throw up spray. Storms add lightning, snow whitens the grass, and fog washes the view out.
- **Day and night** from NTP time. At night the circuit is floodlit, cars run headlights and tail lights, and camera flashes go off in the grandstands.
- **OTA updates** from this repo's GitHub releases.

## Controls

| Gesture / button | Action |
|---|---|
| Tap a car | Follow it (tap empty track to stop following) |
| Drag | Pan |
| Pinch, or **−** / **+** | Zoom between the whole track (0.5×) and close-up (2.5×) |
| ☰ (list) | Show or hide the leaderboard |
| ⚙ (gear) | Settings: brightness, weather (Auto or forced), time (Real / Fast / Day / Night), number of cars, track (Rotate / Random / a preset), race length, series, and *Next race now* (tap twice) |
| *(no touch for 30 s)* | **TV camera**: cuts between the whole track, the closest battle, the leader, a grandstand corner, and replays of spins |

Settings are saved to flash (NVS). Car count, track and series take effect at the next race. Race length applies right away.

## Building (Arduino IDE)

Same toolchain as the koi pond:

1. **ESP32 core**: Espressif `esp32` boards package **3.3.0**.
2. **Libraries**: **LovyanGFX 1.2.21, the FIXED copy** (the two patched files are in [`lovyangfx-fix/`](lovyangfx-fix/)), and **ArduinoJson 6.21.x**.
3. **Config**: copy `race_track/wifi_config.h.template` to `race_track/wifi_config.h` and fill it in. That file is gitignored.
4. Open `race_track/race_track.ino` and set these in the **Tools** menu:
   - Board: **ESP32S3 Dev Module**
   - PSRAM: **OPI PSRAM**
   - Flash Size: **4MB (32Mb)**
   - Partition Scheme: **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)**

Or from the command line:

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=min_spiffs,FlashSize=4M" race_track
```

Serial at 115200 prints the track build, FPS, heap, and a per-phase ms/frame breakdown every 5 s.

## OTA releases

`version.h` points OTA at `ayxrion/race-track`.
Pushing a tag like `v0.1.1` runs [`.github/workflows/release.yml`](.github/workflows/release.yml), which builds the firmware with the LovyanGFX fix and attaches `firmware.bin` to a GitHub release.
It needs these Actions secrets: `WIFI_SSID`, `WIFI_PASSWORD`, `WEATHER_API_KEY`, `WEATHER_LAT`, `WEATHER_LON`, `TIMEZONE_OFFSET_HOURS`.

## Project layout

```
race_track/
  race_track.ino     setup, race-to-race rebuild, main loop, render order, strip presenter
  display_config.h   LGFX class (CrowPanel pins + GT911)
  common.h           constants, RNG, colour + blend helpers, fast primitives, lighting state
  camera.h           world <-> screen transform, zoom steps, inertia
  track_presets.h    real-circuit centrelines (resampled GeoJSON)
  track.h            centreline, racing line, speed profile, corners, kerbs, run-off, barriers, pits, stands
  world.h            scenery bake (palette-indexed), trees, palette lighting, background render
  effects.h          dust / smoke / spray particles, camera flashes, floodlights, rain / snow / fog / lightning
  cars.h             car classes, liveries, driving AI, drawing
  race.h             grid, start lights, laps, chequered flag, results, event ticker
  settings.h         NVS persistence, choosing the next race
  ui.h               touch, buttons, settings panel, leaderboard, TV camera
  ota_update.h       GitHub-release OTA
  weather.h          OpenWeatherMap on a background task
  daynight.h         NTP time and day progress
lovyangfx-fix/       patched LovyanGFX esp32s3 files
```

Rendering works like the koi pond's.
The scenery is baked once per circuit into an 8-bit palette-indexed map in PSRAM, so lighting, wet track, snow and lightning only change a 256-entry palette.
Each frame is composed in SRAM strips, the map rows come in by GDMA prefetch, and only changed rows are copied to the framebuffer.

On top of that:

- **The race simulation runs on core 0**, in parallel with rendering on core 1. Before each frame, core 1 snapshots what it draws: car poses, particles, the leaderboard and the ticker text. Then it kicks off the next simulation steps and renders the snapshot.
- **A still camera only re-renders rows that change.** The whole-track view and the grandstand-corner shots only redraw the rows touched by cars, particles or changing UI, this frame or last. Everything else stays on the panel. The TV camera snaps exactly onto its target when it settles, so it really is still.
- **The leaderboard is a cached 4-bit sprite.** It's rebuilt 4 times a second and copied into the strips. Car colour swatches are drawn directly.

On the device it runs at about 24–30 fps on the whole-track view (capped at 30) and 17–21 fps zoomed in, where PSRAM bandwidth is the limit because every row changes.

Track geometry works in track coordinates: *s* is the distance along the centreline, and *d* is the offset across it, positive to the driver's right.
Every baked pixel finds its nearest centreline segment through a 4 px nearest-sample grid, and kerbs, run-off, barriers, pits and grandstands are all defined in (*s*, *d*).

## Credits

Real-circuit layouts are derived from [bacinger/f1-circuits](https://github.com/bacinger/f1-circuits) (MIT licence).
Road America comes from [OpenStreetMap](https://www.openstreetmap.org/copyright) raceway data, © OpenStreetMap contributors, available under the ODbL.
They're simplified, and track widths are exaggerated on screen.
