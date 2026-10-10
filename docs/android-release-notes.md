Experimental Android build of LoreanXavier's native P.T. PC port (https://github.com/LoreanXavier/pt-pc). All the real
work is theirs; this adds the Android layer.

What's new in v0.1.1:

- Dual-screen handhelds (AYN Thor and similar): the bottom screen shows the hallway's clock, frozen at 23:59, with the loop
  you're on and the radio and other subtitle lines as they play. Long stretches of radio static are marked *static*.
  Change it in Settings > Display > Second screen: clock and subtitles, clock only, dim clock, or off.
- Subtitles now keep up with the radio when it repeats its broadcast in the later loops. Before, the repeated lines came
  up blank after the first pass.

Still true from v0.1.0:

- Snapdragon (Adreno 6xx/7xx) only, Android 10+. Tested on an AYN Thor Max.
- No game files included. On first start, choose your own P.T. fake PKG (made from your dump) and the app sets the game up
  from it. The three extracted files (chunk1.psarc, texture.qar, pathid_list_ps4.bin) in Download work too.
- Hold both triggers for the voice part.
- Logs go to Download/PT-logs. Please attach pt.log (and pt-crash.txt if there is one) with your device and chip when
  reporting a problem.

See the README for details and known issues.
