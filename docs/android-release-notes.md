Experimental Android build of LoreanXavier's native P.T. PC port (https://github.com/LoreanXavier/pt-pc). All the real
work is theirs; this adds the Android layer.

**Coming from v0.2.1 or newer?** Install right over it; your game files and saves stay. Coming from anything older, uninstall
that first (one last time) and pick your .pkg again.

What's new in v0.2.5:

- The bottom screen's clock now follows the hallway clock: when the hallway's turns to 00:00 in the later loops, so does
  the one on the bottom screen.
- B backs out of the pause menu (cancelling the quit question first, if it's up).

What's new in v0.2.4:

- Newer Adreno 8xx GPUs work (confirmed on a Snapdragon 7s Gen 4 tablet) through the device's own driver.
- No more lockouts from the driver setting: if the chosen driver can't run your GPU, the game falls back to the next one by
  itself (bundled or imported, then the device's own, then the bundled one again).
- **Settings > Display > Import a driver**: load any AdrenoTools driver .zip (for example a Turnip build from K11MCH1's
  releases) with the file picker. It shows up under Graphics driver from the next start.
- If no driver works at all, the next start shows a screen to try again, import a driver or pick one, before the game loads.
- A stronger fix for "cannot prepare its data folder": the game now moves to its private storage whenever that folder fails.

What's new in v0.2.3:

- **Settings > Display > Resolution**: Auto (picks a value for your GPU: 67% on fast chips like the Adreno 740, 50% on
  slower ones like the Adreno 710), or 50% to 100% yourself. Applies instantly. Lower is faster.
- **Settings > Display > Graphics driver**: the bundled Mesa Turnip (default) or your device's own driver, from the next
  start. If you see flickering green blocks or other glitches, try your device's own and let us know if it helped.
- These replace render_scale.txt and driver.txt, which Android often hid from the app when a file manager made them.
- The log of the run before stays as pt-previous.log next to pt.log, so you can send both after changing a setting.

What's new in v0.2.2:

- The second screen is only ever a screen built into the device (like the AYN Thor's bottom one). A monitor or TV on
  USB-C or HDMI, a wireless display, or desktop modes like Motorola Smart Connect and Samsung DeX are left alone; before,
  the clock could take over the monitor.
- pt.log also records which screens the app found, for reports from other dual-screen devices.

What's new in v0.2.1:

- Phones and tablets with a newer Adreno GPU (8xx, such as the Snapdragon 7s Gen 4 and 8 Elite) now fall back to the
  device's own graphics driver when the bundled one can't run them. Untested so far; logs welcome.
- Much smoother on some phones: the game no longer rebuilds its display setup every frame when the driver asks it to
  (this cost an Adreno 710 phone most of its frame rate).
- If the app's save folder can't be written (left behind by an earlier install or made by a file manager), settings and
  saves go to the app's private storage instead of the game refusing to start.
- pt.log now says which app version and device it came from.

What's new in v0.2.0:

- Say the name at the end out loud. The game listens through the microphone, recognized on your device with whisper.cpp
  (nothing is recorded or sent anywhere, and the app has no internet access). The app asks for the microphone once before
  the first start. Holding both triggers still works if you'd rather not. You can try it in Settings > Microphone test.
- Touch controls for playing without a controller: drag on the left to walk, on the right to look, plus Use, Zoom, Back,
  Pause and Name buttons. They show up when you touch the screen and fade out after a few seconds or when a controller is
  used. Turn them off in Settings > Display > Touch controls.
- The sticks now work in the pause menu's language list and brightness bar (they only took the d-pad before).
- The APK is bigger (about 80 MB) because it carries the speech model.

From v0.1.1:

- Dual-screen handhelds (AYN Thor and similar): the bottom screen shows the hallway's clock, the loop you're on and the
  radio subtitles, with *static* where the voice drops out. Settings > Display > Second screen.
- Subtitles keep up with the radio when it repeats its broadcast in the later loops.

Still true:

- Snapdragon (Adreno 6xx/7xx) only, Android 10+. Tested on an AYN Thor Max.
- No game files included. On first start, choose your own P.T. fake PKG (made from your dump) and the app sets the game up
  from it. The three extracted files (chunk1.psarc, texture.qar, pathid_list_ps4.bin) in Download work too.
- Logs go to Download/PT-logs. Please attach pt.log (and pt-crash.txt if there is one) with your device and chip when
  reporting a problem.

See the README for details and known issues.
