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
  A move starts with a real speed edge (closing on the car ahead, or a faster straight-line speed in its tow), or, right on someone's gearbox into a corner, as a deliberate move round the outside.
  A car that loses the tow while alongside keeps its momentum and coasts instead of braking.
  Drivers brake in time for a much slower car ahead, and drive round stopped or crawling cars.
- **The racing line** is a minimum-curvature line that uses the whole width of the road: wide in, late apex, wide out.
  A few preset corners that are tighter than the road is wide are rounded off so there's always a smooth way through.
- **Each driver's own line**: drivers vary the racing line through every corner (earlier or later apex, more or less kerb, wider or tighter on entry and exit), and change it lap to lap.
  Braking harder than they're comfortable with, or getting on the throttle too early, uses up grip the tyres need for turning, so a greedy driver runs wide or snaps into oversteer.
  Close behind another car in a corner the dirty air costs grip. Drivers cover the inside into a corner when a car at least as quick is right on their tail, switch back underneath on the exit, and try round the outside, where the car on the inside has to leave room and loses a little exit speed.
  Side by side, a driver plans its speed for the path it's really on (beside the other car or along the kerb), and an attacker carries a little more than is comfortable.
  The classic AI (everyone on the one racing line) is still in the code: set `aiLines` to `false` in `cars.h`.
- **Close racing**: within about a second of the car ahead and in line behind it, anywhere the track is flat out, a driver gets a strong tow that adds acceleration and top speed (never past what the next corner allows).
  A car alongside gets no tow, so two cars side by side don't leapfrog each other down a straight.
  Pulling out to pass, a driver keeps the tow's speed for a moment and gets a push to pass; the car they've just passed has to get back in the tow before attacking again.
  A driver committed to a move brakes later into the next corner, and once their nose is alongside on the inside, the other car usually gives way.
  A leader with a cushion manages the gap, and every driver's form drifts a little over a race, so gaps open and close.
  Races stay open: the further back a car is in its class, the more speed it finds down the straights. That builds up gradually once a car is more than a couple of seconds off its class leader, so cars near the front race on merit.
  A car with someone right behind is more likely to make a mistake.
- **Drivers** all race the same way, so no kind of driver has an edge. They differ in skill (how often they make mistakes), a little raw pace, their form on the day, and their own lines through corners.
  Pace and skill are set for a whole season, so every season has its favourites and its underdogs.
  Every driver lets cars that are lapping them through (blue flags).
- **Damage**: a hard hit on the wall or another car does damage. A damaged car loses some top speed and grip, trails smoke and gets a red mark on the leaderboard; a big enough crash puts it out of the race, and it's pushed off the road where it smokes for a while. A retirement scores no points. In endless races damage slows a car but never puts it out.
- **Safety car**: after a retirement (unless there are fewer than 3 laps to go) the safety car comes out ahead of the leader with its lights flashing. Nobody may pass, and the field closes up behind it. Once the wreck is off the road and a lap has gone by, it's *in this lap*: it pulls off before the line, and racing resumes when the leader crosses it.
- **Tyres and pit stops**: tyres wear as a car laps and lose grip, slowly at first and then faster. In a race of 8 laps or more every driver plans one stop around half distance, and comes in early for a cheap stop under the safety car, or to have heavy damage repaired. A stop is a drive down the pit lane at the speed limit, new tyres at the car's own garage (a new nose too, if it needs one), and back out: about 9 seconds in all. Tracks without a pit lane, and short races, have no stops.
- **Race weekend loop**: a quick qualifying sets the grid (the quickest drivers usually start at the front, but a mistake costs places), the cars form up behind the start lights, race for the set number of laps, and take the chequered flag.
  The result is shown, then the championship standings, and then the next circuit is built.
  Races can also be endless.
- **Championship**: a season is 10 races, scored 25-18-15-12-10-8-6-4-2-1 by place in class, so in Endurance the first GT scores as much as the first prototype. The prototypes go to whoever has had the fewest so far, and each driver keeps their team colours for the season.
  The standings show points, wins and places gained or lost; the last race crowns the champion, and the next one starts a new season with new favourites. A title clinched early is called out.
  Endless races don't count. The standings are saved to flash, so a season carries on after a reboot.
- **Driver records**: career starts, wins, podiums, poles, fastest laps, titles and the longest winning run, kept across seasons (Settings → *Records*).
  The results call out first wins and podiums, winning runs, wins from far back and big climbs, and the grid ticker announces pole.
- **Timing**: positions, gaps to the leader (or laps down), cars in the pits, retirements, last laps, the fastest lap, and a ticker for lead changes, passes for the podium, fastest laps, spins, damage, pit stops and the safety car.
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
| ⚙ (gear) | Settings: brightness, weather (Auto or forced), time (Real / Fast / Day / Night), number of cars, track (Rotate / Random / a preset), car type (Random or one series), race length, camera, spectator, **Drivers**, **Records**, and *Next race now* (tap twice) |
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
Turn *Custom* on and car 1 gets slot 1's name, car 2 slot 2's, and so on; turn it off for the standard field.
Points and records belong to a name, so a renamed driver starts from scratch. *New season* (tap twice) wipes the standings; the records stay.
Tap a name to type a new one (up to 3 letters or digits), and *Copy this race* fills the slots from the field that's racing now.
Slots past the number of cars sit out.

## Building (Arduino IDE)

Same toolchain as the koi pond:

1. **ESP32 core**: Espressif `esp32` boards package **3.3.0**.
2. **Libraries**: **LovyanGFX 1.2.21, the FIXED copy** (the two patched files are in [`lovyangfx-fix/`](lovyangfx-fix/); copy them over the library's `src/lgfx/v1/platforms/esp32s3/` files again whenever they change), and **ArduinoJson 6.21.x**. Besides making the RGB panel work on core 3.x, `Bus_RGB.cpp` works around an ESP-IDF 5.5 driver bug that could leave the picture shifted sideways and flickering until a reboot. The serial stats print `rgb: resync on` when the workaround is active.
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
  champ.h            championship standings and driver records (NVS)
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
- **The AI plans cheaply.** For each car, the corner windows of its own line (and of a car it's racing beside) are gathered once per step, with their blend factors worked out at the start of the race, so each point on the line costs no divides and no library cosine. The full look 180 px ahead for corner speeds runs every other step, alternating between cars. In between, a car re-checks the point it's on and the corner point that set its speed last time.
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
