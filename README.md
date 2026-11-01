# Update 2 - music from the screen's SD card

Copy these over the matching files in J_Sign_Final (same folders). Only changed/new files are here.
This replaces Update 1 (includes its fixes).

## What it does
The SD card goes in the SCREEN. Song titles on the Music page are the real file names.
The screen reads the card and sends the song to the ESP32 over the UART; the ESP32 decodes it and plays it
through the NS4168. LED commands share the same UART.

## Card
FAT32 (not exFAT), .mp3 or .wav files in the ROOT of the card, 48 songs max, names under 63 characters.
Songs play alphabetically and loop. Use MP3 at 128-192 kbps; WAV only if 16-bit mono (stereo WAV is too fast
for the link). Swap cards any time: the Music page re-reads the card every time you open it.

## Wiring (unchanged except speed)
GD32 PA2 (TX) -> ESP32 GPIO16 (RX), GD32 PA3 (RX) <- ESP32 GPIO17 (TX), shared ground.
The link now runs at 1,000,000 baud (LINK_BAUD in gd32/src/hal.h and esp32/src/main.cpp - change both together).
NS4168: BCLK GPIO27, LRCLK GPIO26, DIN GPIO25.

## New / changed files
gd32/src:   main.c hal.c hal.h splash.h music_ui.h  | new: player.h sd_diskio.c fatfs/ff.c fatfs/ffunicode.c spl/gd32f20x_spi.c
gd32/include: new: ff.h ffconf.h diskio.h
esp32/src:  main.cpp (rewritten: no SD, streaming decoder)
No platformio.ini changes.

## Also in here
- Splash sparkles no longer use blurred shadows (that was the lag); LCD write speed doubled.
- Touch calibration is more forgiving and shows a live "pen / raw" line while you tap the dots.
