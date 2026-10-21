"""Generuje radio/station_numbers.h: polskie liczebniki 1..N jako krótkie nagrania Ogg Opus.

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

OUT_RATE = 16000  # Opus przyjmuje 8/12/16/24/48 kHz; do mowy wystarczy 16 kHz

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


def encode_opus(audio: np.ndarray) -> bytes:
    buf = io.BytesIO()
    sf.write(buf, audio, OUT_RATE, format="OGG", subtype="OPUS")
    return buf.getvalue()


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
    clips = []
    for n in range(1, args.count + 1):
        text = number_words(n)
        clips.append(encode_opus(prepare(*synthesize(voice, text))))
        print(f"{n:2} {text:24} {len(clips[-1])} B")

    crc = 0
    for c in clips:
        crc = zlib.crc32(c, crc)
    parts = [
        "#pragma once\n",
        "// Wygenerowane przez tools/gen_station_numbers.py — nie edytuj ręcznie.\n",
        f"// Polskie liczebniki 1..{args.count} (Piper, Ogg Opus {OUT_RATE // 1000} kHz), "
        f"razem {sum(map(len, clips))} B.\n\n",
        "#include <stdint.h>\n#include <stddef.h>\n\n",
    ]
    parts += [c_array(f"STATION_NUMBER_{n}", c) + "\n" for n, c in enumerate(clips, 1)]
    parts.append("struct NumberClip {\n  const uint8_t* data;\n  size_t len;\n};\n\n")
    parts.append("// Indeks 0 = „jeden”.\nstatic const NumberClip STATION_NUMBER_CLIPS[] = {\n")
    parts += [f"  {{STATION_NUMBER_{n}, sizeof(STATION_NUMBER_{n})}},\n" for n in range(1, args.count + 1)]
    parts.append("};\n\n")
    parts.append(f"constexpr int STATION_NUMBER_COUNT = {args.count};\n")
    parts.append(f"constexpr uint32_t STATION_NUMBER_CLIPS_CRC = 0x{crc:08x};\n")
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(parts))
    print(f"zapisano {args.out}, razem {sum(map(len, clips))} B")


if __name__ == "__main__":
    main()
