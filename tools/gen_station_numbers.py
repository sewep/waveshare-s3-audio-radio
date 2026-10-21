"""Generuje radio/station_numbers.h: polskie liczebniki 1..N jako krótkie nagrania MP3.

MP3, a nie Ogg Opus: ESP32-audioI2S 4.0.0 przy każdym pliku Ogg mniejszym niż 64 KB czeka 3 s
w getLastGranulePosition() (próbuje przeczytać ostatnie 64 KB z limitem 3000 ms).
Bez ramki Xing/Info: ta sama biblioteka liczy z niej czas trwania w pełnych sekundach i dzieli
przez niego, więc plik krótszy niż 1 s kończy się dzieleniem przez zero.

Synteza lokalna (Piper), bez usług w chmurze. Wymaga: piper-tts, soundfile, numpy, scipy
oraz modelu głosu Pipera (np. pl_PL-gosia-medium.onnx z huggingface.co/rhasspy/piper-voices).

    python tools/gen_station_numbers.py <model.onnx> radio/station_numbers.h [--count 40]
"""

import argparse
import io
import zlib

import numpy as np
import soundfile as sf
from piper import PiperVoice
from scipy.signal import resample_poly

OUT_RATE = 16000  # MPEG-2 Layer III; do mowy wystarczy 16 kHz
MP3_QUALITY = 0.3  # compression_level w libsndfile: 0 = najlepsza jakość, 1 = najmniejszy plik

UNITS = ["", "jeden", "dwa", "trzy", "cztery", "pięć", "sześć", "siedem", "osiem", "dziewięć"]
TEENS = ["dziesięć", "jedenaście", "dwanaście", "trzynaście", "czternaście", "piętnaście",
         "szesnaście", "siedemnaście", "osiemnaście", "dziewiętnaście"]
TENS = ["", "", "dwadzieścia", "trzydzieści", "czterdzieści", "pięćdziesiąt",
        "sześćdziesiąt", "siedemdziesiąt", "osiemdziesiąt", "dziewięćdziesiąt"]


def number_words(n: int) -> str:
    if n < 10:
        return UNITS[n]
    if n < 20:
        return TEENS[n - 10]
    tens, units = divmod(n, 10)
    return f"{TENS[tens]} {UNITS[units]}".strip()


def synthesize(voice: PiperVoice, text: str) -> tuple[np.ndarray, int]:
    chunks = list(voice.synthesize(text))
    audio = np.concatenate([c.audio_int16_array for c in chunks]).astype(np.float32) / 32768
    return audio, chunks[0].sample_rate


def prepare(audio: np.ndarray, rate: int) -> np.ndarray:
    # obcięcie ciszy, zmiana częstotliwości, normalizacja do -1 dBFS i krótkie marginesy
    loud = np.flatnonzero(np.abs(audio) > 0.02)
    if loud.size:
        audio = audio[max(0, loud[0] - rate // 50): loud[-1] + rate // 20]
    g = np.gcd(OUT_RATE, rate)
    audio = resample_poly(audio, OUT_RATE // g, rate // g)
    audio *= 0.89 / max(1e-6, np.max(np.abs(audio)))
    pad = np.zeros(OUT_RATE // 20, dtype=np.float32)
    return np.concatenate([pad, audio, pad])


MP3_BITRATES = {  # kb/s dla Layer III: MPEG-1 oraz MPEG-2/2.5
    3: [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320],
    2: [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160],
}
MP3_RATES = {3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000]}


def strip_xing_frame(data: bytes) -> bytes:
    """Usuwa pierwszą ramkę, jeśli to ramka metadanych Xing/Info (bez dźwięku)."""
    hdr = int.from_bytes(data[:4], "big")
    if hdr & 0xFFE00000 != 0xFFE00000:
        raise ValueError("MP3 nie zaczyna się od ramki")
    version = (hdr >> 19) & 3
    bitrate = MP3_BITRATES[3 if version == 3 else 2][(hdr >> 12) & 0xF] * 1000
    rate = MP3_RATES[version][(hdr >> 10) & 3]
    padding = (hdr >> 9) & 1
    frame_len = (144 if version == 3 else 72) * bitrate // rate + padding
    first = data[:frame_len]
    if b"Xing" in first or b"Info" in first:
        return data[frame_len:]
    return data


def encode_mp3(audio: np.ndarray) -> bytes:
    buf = io.BytesIO()
    sf.write(buf, audio, OUT_RATE, format="MP3", subtype="MPEG_LAYER_III", compression_level=MP3_QUALITY)
    return strip_xing_frame(buf.getvalue())


def c_array(name: str, data: bytes) -> str:
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 20]) for i in range(0, len(data), 20)]
    return f"static const uint8_t {name}[] = {{\n  " + ",\n  ".join(rows) + "\n};\n"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("out")
    ap.add_argument("--count", type=int, default=40)
    args = ap.parse_args()

    voice = PiperVoice.load(args.model)
    clips, durations = [], []
    for n in range(1, args.count + 1):
        text = number_words(n)
        audio = prepare(*synthesize(voice, text))
        clips.append(encode_mp3(audio))
        durations.append(round(len(audio) * 1000 / OUT_RATE))
        print(f"{n:2} {text:24} {len(clips[-1])} B, {durations[-1]} ms")

    crc = 0
    for c in clips:
        crc = zlib.crc32(c, crc)
    parts = [
        "#pragma once\n",
        "// Wygenerowane przez tools/gen_station_numbers.py — nie edytuj ręcznie.\n",
        f"// Polskie liczebniki 1..{args.count} (Piper, MP3 {OUT_RATE // 1000} kHz mono), "
        f"razem {sum(map(len, clips))} B.\n\n",
        "#include <stdint.h>\n#include <stddef.h>\n\n",
    ]
    parts += [c_array(f"STATION_NUMBER_{n}", c) + "\n" for n, c in enumerate(clips, 1)]
    parts.append("struct NumberClip {\n  const uint8_t* data;\n  size_t len;\n  uint16_t ms;  // długość nagrania\n};\n\n")
    parts.append("// Indeks 0 = „jeden”.\nstatic const NumberClip STATION_NUMBER_CLIPS[] = {\n")
    parts += [f"  {{STATION_NUMBER_{n}, sizeof(STATION_NUMBER_{n}), {d}}},\n"
              for n, d in zip(range(1, args.count + 1), durations)]
    parts.append("};\n\n")
    parts.append(f"constexpr int STATION_NUMBER_COUNT = {args.count};\n")
    parts.append('constexpr const char* STATION_NUMBER_EXT = ".mp3";\n')
    parts.append(f"constexpr uint32_t STATION_NUMBER_CLIPS_CRC = 0x{crc:08x};\n")
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(parts))
    print(f"zapisano {args.out}, razem {sum(map(len, clips))} B")


if __name__ == "__main__":
    main()
