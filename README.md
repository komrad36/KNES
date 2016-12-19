Lightweight but complete NES emulator. Straightforward implementation in a
few thousand lines of C++.

Written from scratch in a speedcoding challenge in just 72 hours.

- - - -

![picture alt](https://raw.githubusercontent.com/komrad36/KNES/master/KNES_Cliffhanger_sped_up.gif "KNES Playing Cliffhanger (sped up)")

- - - -

Intended to showcase low-level work, processor and instruction set work, 6502 emulation, basic game loop mechanics,
audio, video, user interaction. Also provides a compact emulator,
fully open and free to study and modify.

No external dependencies except for
those needed for interfacing:

- PortAudio for sound (http://www.portaudio.com/)
- GLFW for video (http://www.glfw.org/)

If you compile GLFW yourself, be sure to specify
shared build ('cmake -DBUILD_SHARED_LIBS=ON .')
or you will enter dependency hell at link-time.

Fully cross-platform. Tested on Windows (MSVC) and Linux (g++).
Makefile provided for the latter case.

Fully playable, with sound! Full CPU, APU, PPU emulation.

6 of the most common mappers are supported (0, 1, 2, 3, 4, 7), covering roughly
80-90% of all NES games. Get a .nes v1 file and go!

Written from scratch in a speedcoding challenge (72 hours!). This means
the code is NOT terribly clean. Always loved the 6502 and wanted to try
something crazy. Got it fully working, with 6 mappers, in 3 days.

I tend not to like OO much, especially for speedcoding, so here it's pretty
much only used for mapper polymorphism.

    Usage: KNES <rom_file>

Keymap (modify as desired in 'main.cpp'):

 NES                  |  Keyboard
----------------------|--------------
 Up/Down/Left/Right   |  Arrow Keys
 Start                |  Enter
 Select               |  Right Shift
 A                    |  Z
 B                    |  X
 Turbo A              |  S
 Turbo B              |  D
-------------------------------------

Emulator keys:

 Keyboard             |  Action
----------------------|--------------
 Tilde                |  Fast-forward
 Escape               |  Quit
 ALT+F4               |  Quit
-------------------------------------

The display window can be freely resized at runtime.
You can also set proper full-screen mode at the top
of 'main.cpp', and also enable V-SYNC if you are
experiencing tearing issues.

I love the 6502 and am relatively confident in the CPU emulation
but have much less knowledge about the PPU and APU
and am sure at least a few things are wrong here and there.

Feel free to correct and/or teach me about the PPU and APU!

Major thanks to http://www.6502.org/ for CPU ref, and especially
to http://nesdev.com/, which I basically spent the three days
scouring every inch of, especially to figure out the mappers and PPU.
