/**
 * The four parts of the band.
 */
enum BandPart {
    //% block="melody"
    Melody = 0,
    //% block="harmony"
    Harmony = 1,
    //% block="bass"
    Bass = 2,
    //% block="drums"
    Drums = 3
}

/**
 * Instruments (the sound each part makes).
 */
enum BandInstrument {
    //% block="beep (8-bit square)"
    Beep = 0,
    //% block="flute (soft)"
    Flute = 1,
    //% block="brass (buzzy)"
    Brass = 2,
    //% block="whistle (smooth)"
    Whistle = 3,
    //% block="chip (thin 8-bit)"
    Chip = 4,
    //% block="guitar (plucked)"
    Guitar = 5,
    //% block="bell"
    Bell = 6,
    //% block="bass guitar (round)"
    BassGuitar = 7
}

/**
 * Play songs with up to 4 instruments at the same time: melody, harmony, bass and drums.
 * micro:bit V2 only (it uses the V2 sound mixer).
 *
 * Notes are written like "E5:2 B4 C5 R:2":
 *   a letter A-G, optional # (sharp) or b (flat), then the octave (4 is the middle).
 *   ":2" makes a note last 2 steps (no number = 1 step). R is a rest.
 * Drums use K (kick), S (snare) and H (hi-hat), e.g. "K H S H K K S H".
 * Each part loops by itself, so a short drum beat repeats under a long melody.
 */
//% color="#d6336c" weight=95 icon="" block="Band"
namespace band {
    let simPlaying = false
    let simMelody: number[] = []
    let simStep = 150
    let simRun = 0

    //% shim=band::setTrack
    function _setTrack(voice: number, data: Buffer): void {
        // simulator: only the melody is played (one note at a time)
        if (voice == 0) {
            simMelody = []
            for (let i = 0; i + 3 < data.length; i += 4) {
                simMelody.push(data.getNumber(NumberFormat.UInt16LE, i))
                simMelody.push(data.getNumber(NumberFormat.UInt16LE, i + 2))
            }
        }
    }

    // (In the simulator these remember the setting; on the micro:bit the C++ code runs instead.
    // Each needs at least one line inside, or the simulator looks for a built-in version and crashes.)
    let simInstrument: number[] = [4, 1, 7, 0]
    let simVolume: number[] = [80, 50, 75, 80]

    //% shim=band::setInstrument
    function _setInstrument(voice: number, instrument: number): void {
        simInstrument[voice] = instrument
    }

    //% shim=band::setPartVolume
    function _setPartVolume(voice: number, volume: number): void {
        simVolume[voice] = volume
    }

    //% shim=band::setStepMs
    function _setStepMs(ms: number): void {
        simStep = ms
    }

    //% shim=band::start
    function _start(): void {
        simPlaying = true
        simRun += 1
        const run = simRun
        control.inBackground(function () {
            while (simPlaying && run == simRun && simMelody.length > 0) {
                for (let i = 0; i + 1 < simMelody.length; i += 2) {
                    if (!simPlaying || run != simRun) return
                    const f = simMelody[i]
                    const ms = simMelody[i + 1] * simStep
                    if (f > 3) {
                        music.playTone(f, ms * 5 / 6)
                        basic.pause(ms / 6)
                    } else {
                        basic.pause(ms)
                    }
                }
            }
        })
    }

    //% shim=band::stop
    function _stop(): void {
        simPlaying = false
    }

    //% shim=band::isPlaying
    function _isPlaying(): boolean {
        return simPlaying
    }

    /**
     * Turns notes like "E5:2 B4 R K S H" into the band's format.
     */
    export function notesToBuffer(notes: string): Buffer {
        const words = notes.split(" ").filter(w => w.length > 0)
        const buf = pins.createBuffer(words.length * 4)
        const letters = "C D EF G A B"
        let n = 0
        for (let w of words) {
            let len = 1
            const c = w.indexOf(":")
            if (c >= 0) {
                len = parseInt(w.substr(c + 1))
                w = w.substr(0, c)
            }
            if (!(len >= 1)) len = 1
            let f = 0
            const first = w.charAt(0).toUpperCase()
            if (first == "K") f = 1
            else if (first == "S") f = 2
            else if (first == "H") f = 3
            else if (first != "R" && first != "-") {
                let note = letters.indexOf(first)
                if (note < 0) continue
                let pos = 1
                if (w.charAt(1) == "#") { note += 1; pos = 2 }
                else if (w.charAt(1) == "b") { note -= 1; pos = 2 }
                let octave = parseInt(w.substr(pos))
                if (!(octave >= 0)) octave = 4
                const midi = (octave + 1) * 12 + note
                f = Math.round(440 * Math.pow(2, (midi - 69) / 12))
                if (f < 4) f = 4
            }
            buf.setNumber(NumberFormat.UInt16LE, n * 4, f)
            buf.setNumber(NumberFormat.UInt16LE, n * 4 + 2, len)
            n++
        }
        return buf.slice(0, n * 4)
    }

    /**
     * Play a song with up to four parts at once. Leave a part empty ("") to not use it.
     * The song keeps looping until you stop it.
     * @param melody the tune, eg: "E5:2 B4 C5 D5:2 C5 B4"
     * @param harmony notes played with the tune, eg: ""
     * @param bass low notes, eg: "E3:4 A2:4"
     * @param drums K = kick, S = snare, H = hi-hat, eg: "K H S H"
     * @param stepMs how long one step lasts in milliseconds, eg: 150
     */
    //% blockId=band_play_song
    //% block="play song|melody $melody|harmony $harmony|bass $bass|drums $drums|step (ms) $stepMs"
    //% stepMs.min=40 stepMs.max=1000 stepMs.defl=150
    //% melody.defl="E5:2 B4 C5 D5:2 C5 B4" harmony.defl="" bass.defl="E3:4 A2:4" drums.defl="K H S H"
    //% weight=100
    export function playSong(melody: string, harmony: string, bass: string, drums: string, stepMs: number) {
        _stop()
        _setStepMs(stepMs)
        _setTrack(0, notesToBuffer(melody))
        _setTrack(1, notesToBuffer(harmony))
        _setTrack(2, notesToBuffer(bass))
        _setTrack(3, notesToBuffer(drums))
        _start()
    }

    /**
     * Change the notes of one part (the others keep playing).
     */
    //% blockId=band_set_part
    //% block="set $part notes to $notes"
    //% weight=90
    export function setPart(part: BandPart, notes: string) {
        _setTrack(part, notesToBuffer(notes))
    }

    /**
     * Stop the band.
     */
    //% blockId=band_stop block="stop the band"
    //% weight=80
    export function stop() {
        _stop()
    }

    /**
     * Start the band again from the beginning of every part.
     */
    //% blockId=band_restart block="restart the band"
    //% weight=79
    export function restart() {
        _start()
    }

    /**
     * Choose the instrument for a part.
     */
    //% blockId=band_set_instrument block="set $part instrument to $instrument"
    //% weight=70
    export function setInstrument(part: BandPart, instrument: BandInstrument) {
        _setInstrument(part, instrument)
    }

    /**
     * Set how loud one part is (0 to 100).
     */
    //% blockId=band_set_volume block="set $part volume to $volume"
    //% volume.min=0 volume.max=100 volume.defl=80
    //% weight=60
    export function setPartVolume(part: BandPart, volume: number) {
        _setPartVolume(part, volume)
    }

    /**
     * Set the speed: how long one step lasts in milliseconds (smaller = faster).
     */
    //% blockId=band_set_step block="set band step to $ms ms"
    //% ms.min=40 ms.max=1000 ms.defl=150
    //% weight=50
    export function setStep(ms: number) {
        _setStepMs(ms)
    }

    /**
     * True while the band is playing.
     */
    //% blockId=band_is_playing block="band is playing"
    //% weight=40
    export function isPlaying(): boolean {
        return _isPlaying()
    }
}
