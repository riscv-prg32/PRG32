# Audio design

The performance is a 32-bar, continuously looping tracker arrangement of
Bach's public-domain BWV 846 prelude. Each bar retains the composition's
characteristic broken-chord contour while adapting register, density, and
voice leading to the PRG32 eight-voice mixer.

| Channel | Role | Oscillator | Stereo role |
|---:|---|---|---|
| 0 | Pedal bass | Triangle | centre-left |
| 1 | Low arpeggio | Pulse | left |
| 2 | Inner arpeggio | Triangle | centre-left |
| 3 | Upper arpeggio | Saw | centre-right |
| 4 | Treble shimmer | Narrow pulse | right |
| 5 | Delayed echo | Saw | moving across field |
| 6 | Harmonic halo | Wide pulse | moving opposite echo |
| 7 | Breath/accent | Filtered noise | alternating edges |

The JSON generator encodes synth IDs exactly as documented by
`development-c6`: bit 15 marks procedural synthesis; resonance occupies bits
11:10, cutoff bits 9:6, pulse width bits 5:2, and waveform bits 1:0. The
instruments deliberately cover all four waveform values and multiple filter,
resonance, pulse-width, ADSR, volume, and pan settings.

The score issues explicit `NOTE_OFF` events before retriggering each melodic
voice. This exercises synth release stages and avoids relying on voice stealing.
Channels 5 and 6 cross the stereo field while channel 7 alternates hard left
and right. A centered low foundation preserves musical balance and makes mono
fold-down coherent.

The code does not initialize hardware directly; a cartridge uses the resident
PRG32 audio service and its attached AUDIO block. It detects mono versus stereo
for display purposes, while the runtime applies the documented pan-ignore
fallback in mono.
