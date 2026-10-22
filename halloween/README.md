# Dźwięki do straszaka na Halloween

Pliki MP3 do wgrania w zakładce z dynią na stronie radia („Wgraj”). Wszystkie są z OpenGameArt na
licencji CC0 (bez obowiązku podawania autora). Przerobione 2026-10-22 w ffmpeg: mono 44,1 kHz,
96 kb/s, każdy podbity do szczytu −1 dBFS.

| Plik | Co to | Źródło |
|---|---|---|
| `czarownica-krzyk-i-smiech.mp3` | Kompozycja, 7 s: krzyk kobiety, śmiech nr 9 z pakietu, na koniec rechot czarownicy | trzy poniższe |
| `krzyk-kobiety.mp3` | Krzyk przerażenia, 1,8 s | [Female Scream 1](https://opengameart.org/node/147410), Nocturnal_Vanguard |
| `krzyki-4-wysokosci.mp3` | Cztery krzyki kobiece w różnych wysokościach, 6,6 s | [Female high-pitched scream SFX](https://opengameart.org/node/131040), WuxiaScrub |
| `smiech-01.mp3` … `smiech-10.mp3` | Dziesięć szalonych śmiechów (pakiet pocięty po ciszy), 1,2–3,4 s każdy | [Maniacal Laughter Pack 1](https://opengameart.org/node/147834), Nocturnal_Vanguard |
| `smiech-czarownicy-krotki.mp3` | Krótki rechot czarownicy, 1,2 s | [Witch Cackle](https://opengameart.org/content/witch-cackle), AntumDeluge |

Radio gra jeden plik, więc żeby połączyć kilka dźwięków, skleja się je wcześniej, np.:

```
ffmpeg -i krzyk-kobiety.mp3 -i smiech-05.mp3 -filter_complex "[0][1]concat=n=2:v=0:a=1" -ac 1 -b:a 96k moj.mp3
```
