# band

Play songs with **melody, harmony, bass and drums at the same time** on a micro:bit V2.

The band has its own channel in the micro:bit's sound mixer, so normal tones and sound
effects still play on top of it.

## Use it

```blocks
band.playSong(
    "E5:2 B4 C5 D5:2 C5 B4 A4:2 A4 C5 E5:2 D5 C5",   // melody
    "",                                              // harmony
    "E3 E4 E3 E4 A2 A3 A2 A3",                       // bass
    "K H S H",                                       // drums
    180                                              // ms per step
)
```

### Writing notes

* A letter `A`-`G`, then `#` (sharp) or `b` (flat) if you want, then the octave (`4` is the middle).
* `:2` makes a note last 2 steps. No number means 1 step.
* `R` is a rest.
* Drums: `K` = kick, `S` = snare, `H` = hi-hat.
* Each part loops by itself, so a short drum beat repeats under a long melody.

### Other blocks

* `set (part) instrument to (instrument)`: beep, flute, brass, whistle, chip, guitar, bell, bass guitar
* `set (part) volume to (0-100)`
* `set (part) notes to "..."`: change one part while the others keep playing
* `set band step to (ms)`: change the speed
* `stop the band`, `restart the band`, `band is playing`

In the simulator only the melody plays. On a real micro:bit V2 you hear all four parts.
