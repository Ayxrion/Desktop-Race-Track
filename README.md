# Race Track

A top-down desktop race track for the **Elecrow CrowPanel 7.0"** (ESP32-S3, 800×480 RGB, GT911 touch).
AI cars race around a circuit in the background, with lap timing, a leaderboard, weather, and day and night.
It uses the same board setup and rendering approach as the koi pond (Desktop Aquarium V2), but it's a separate project.

## What's on the track

- **Circuits**: eleven real-circuit layouts (approximate), plus procedurally generated ones.
  The presets are Monza, Spa-Francorchamps, Silverstone, Interlagos, Red Bull Ring, Circuit of the Americas, Zandvoort, Hungaroring, Watkins Glen, Imola and Road America.
  By default the device alternates between a preset and a new random circuit for each race.
- **Trackside**: red and white kerbs, gravel traps and tarmac run-off sized to each corner, armco and tyre walls, a pit lane with garages and a paddock, grandstands with crowds on the slowest corners, and forests and fields around the outside.
- **Car types**: Formula (open-wheelers in team pairs), GT, Touring, Endurance (prototypes and GTs together), Classic (1960s cigar-shaped cars in national colours) and Vans (boxy panel vans: slow out of corners, short on grip).
  Pick one in settings, or leave it on Random.
- **AI drivers** steer like real cars. Each car has a position, heading and speed, and a path-tracking controller follows the line it wants: the racing line, or an offset line when passing or defending.
  Cornering is limited by tyre grip, so a car that arrives too fast slides wide, scrubs speed, and can end up on the grass, in the gravel, in the wall, or spinning.
  Drivers plan their own braking points, pick the inside or outside to pass, race side by side, and sometimes make contact.
  They leave racing room for a car alongside instead of steering into it, pick the side that stays open along the next stretch, and back out of a move that's being closed down.
  Lane changes ease in, so a car settles onto its new line instead of running wide onto the grass.
- **Close racing**: within about a second of the car ahead on a straight, a driver gets a tow that adds acceleration and top speed.
  A driver committed to a move brakes later into the next corner, and once their nose is alongside on the inside, the other car usually gives way.
  A leader with a cushion manages the gap, and every driver's form drifts a little over a race, so gaps open and close.
  A car with someone right behind is more likely to make a mistake.
- **Driver traits**, shown as a coloured dot on the leaderboard and named in the tag of the car you're following.
  They change how someone races more than how fast they are, so any trait can win:
  - **Aggressive** (red): a touch more speed into corners and later braking, dives up the inside even mid-corner, and needs the least room. They make the most mistakes and visit the gravel most.
  - **Balanced** (no dot): the baseline.
  - **Cautious** (blue): slightly off the limit with clean lines, and needs a clearer advantage before passing.
  - **Patient** (green): takes it a little easier, rarely attacks, keeps a bigger gap, and waves clearly faster cars through.
  - **Defensive** (orange): covers the inside when someone attacks, and won't give way when a car gets alongside.
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
| 📺 (TV) | Spectator mode on or off |
| ⚙ (gear) | Settings: brightness, weather (Auto or forced), time (Real / Fast / Day / Night), number of cars, track (Rotate / Random / a preset), car type (Random or one series), race length, camera, spectator, **Drivers**, and *Next race now* (tap twice) |
| *(no touch for 30 s)* | The **TV camera** takes over again |

**Camera** (in settings) picks what the TV camera shows:

- **TV director** (default): cuts between the whole track, the closest battle, the leader, a random driver, a grandstand corner, and replays of spins and contact.
- **Follow leader**: stays on whoever is leading, through lead changes.
- **Closest battle**: the tightest fight on track, moving to a new one when it breaks up.
- **Random driver**: rides with one car for 20–40 s, then picks another.
- **Grandstands**: a still shot of each grandstand corner in turn.
- **Whole track**: the full circuit, never moving. It's the smoothest, because only the rows with cars on them are redrawn.

**Spectator** keeps the TV camera running permanently.
Dragging, pinching, tapping cars and the zoom buttons are ignored, while the leaderboard, TV and settings buttons still work.
With spectator off, touching the track takes the camera over, and the TV camera comes back after 30 s without touch.
Pressing buttons or changing settings doesn't stop the TV camera either way.

Settings are saved to flash (NVS). Car count, track, car type and drivers take effect at the next race. Race length applies right away.

**Drivers** (in settings) lists 20 driver slots.
Turn *Custom* on and car 1 gets slot 1's name and trait, car 2 slot 2's, and so on; turn it off for random drivers each race.
Tap a name to type a new one (up to 3 letters or digits), tap a trait to cycle through Balanced, Cautious, Patient, Defensive and Aggressive, and *Copy this race* fills the slots from the field that's racing now.
Slots past the number of cars sit out.

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
- **The leaderboard is a cached 4-bit sprite.** It's rebuilt 4 times a second, along with a list of its text pixels as same-colour runs, so each frame only writes those. Car colour swatches are drawn directly.
- **Zoomed in, only the visible map columns are fetched.** A second copy of the full-size map is stored in 320-pixel-wide vertical bands, so a strip's prefetch is two or three contiguous blocks instead of whole 1600-byte rows. That cuts most of the PSRAM reads.
- **The GDMA copies use 32-byte bursts** (the default is 16), and the rows aren't hashed when the camera moved, since every row changes anyway.
- **Particles are projected once per frame** into a screen-space list with their lit colours, and each strip only draws the ones it overlaps.
- **Translucent panels blend two pixels at a time** with shifts, for the alpha values where that's exact enough.

On the device, with 20 cars, it runs at about 28–29 fps zoomed in (capped at 30) and 24–30 fps on the whole-track view.
PSRAM bandwidth is still the limit when zoomed in, because every row changes.

Track geometry works in track coordinates: *s* is the distance along the centreline, and *d* is the offset across it, positive to the driver's right.
Every baked pixel finds its nearest centreline segment through a 4 px nearest-sample grid, and kerbs, run-off, barriers, pits and grandstands are all defined in (*s*, *d*).

## Credits

Real-circuit layouts are derived from [bacinger/f1-circuits](https://github.com/bacinger/f1-circuits) (MIT licence).
Road America comes from [OpenStreetMap](https://www.openstreetmap.org/copyright) raceway data, © OpenStreetMap contributors, available under the ODbL.
They're simplified, and track widths are exaggerated on screen.
