# audio-stuff

An assortment of unrelated tiny tools, toys, and curiosities for playing, scanning, ripping,
and converting various audio formats.

* **pm:**
    A module player (with support for .it, .xm, .s3m, .mod, .669, .mtm, .imf, .sfx) written to help
    understand some aspects of Impulse Tracker's player engine better. Originally this player was meant
    to replace libmodplug in Schism Tracker, but that plan was scrapped, as it proved easier to rewrite
    the playback routines in-place while keeping the core structure.

* **pymod:**
    A tiny Amiga .mod player implemented in pure Python. I'm not even sure why I did this. Loads M.K.
    (4-channel) files only, and handles a handful of effects (0/9/A/C/D/F, and partial support for 5 and 6).

* **itscomp:**
    Some abandoned experiments with compressed .its samples. This was written years ago, before Impulse
    Tracker's source code was released, and at the time, there was no open implementation of the sample
    compression function.

* **itgrep:**
    As the name implies, it's a hackish tool to skim through .it files for "interesting" data.

* **gym2it:**
    In theory, this converts a .gym file (a Genesis/Mega Drive audio rip) into an Impulse Tracker module, maybe
    even creating instruments with FM synthesis. In practice, it just produces hilariously bad chiptunes.

* **genrip:**
    Read through a .vgm file and dump out an .it file containing the used sample data. (The resulting file
    merely functions as a sample library, and isn't playable on its own.)

* **soda:**
    My entry to a short contest on /prog/ to construct a programming language. Built around a fictional chip
    with rudimentary audio capabilities (16 channels, tri/saw/sine/square waveforms). Comes with a .mid file
    loader.

## Licenses

This is a collection of unrelated subprojects, with no overarching licensing terms. Please
consult the files named COPYING in each subdirectory for details.

Should you have any questions or comments, you can contact me at <storlek@rigelseven.com>.
