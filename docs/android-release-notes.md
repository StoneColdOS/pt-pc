Experimental Android build of LoreanXavier's native P.T. PC port (https://github.com/LoreanXavier/pt-pc). All the real
work is theirs; this adds the Android layer.

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
