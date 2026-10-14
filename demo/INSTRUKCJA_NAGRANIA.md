# Nagranie demonstracji – plan B

Cel: 3-minutowe nagranie pokazu, które odtwarzasz na prezentacji zamiast pokazu na żywo – na wypadek
braku sieci w sali, wolnego komputera albo stresu. Pokaz prowadzi **skrypt reżysera**
(`demo/rezyser_demo.js`): sam przełącza RUN, ustawia czujniki, naciska przyciski i wpisuje polecenia
konsoli, a na ekranie wyświetla podpisy z numerem kroku i czasem. Ty tylko nagrywasz ekran.

> Oś czasu została sprawdzona symulacją silnika `predictive.h` – zdarzenia w Wokwi pojawiają się
> w zaplanowanych chwilach z dokładnością do ok. 3 s (odczyt DHT22 co 2,5 s, okno analizy 1 s).

## Co będzie na nagraniu

| Czas | Krok | Co się dzieje | Na co zwrócić uwagę (narracja) |
|---|---|---|---|
| 0:00 | 1. Uruchomienie | przełącznik RUN → praca | zielona lampka świeci ciągle; linia stanu `PRACA … OK`, „kontrola za ~200 h” |
| 0:20 | 2. Drgania +0,10 g | suwak *Acceleration X* = 0,10 g | ok. 0:22 `VIB -> OSTRZEZENIE (3.17)`, żółta lampka, `STAN -> PLAN`; ok. 0:36 raport `stan`: przyczyna **niewywazenie/luz** |
| 0:50 | 3. Rośnie temperatura | temperatura 40 → 56 °C (1 °C co 2,5 s) | prognoza maleje, zużycie przyspiesza (×3–4); ok. 1:30 przyczyna zmienia się na **lozysko/smarowanie** |
| ok. 1:35 | 4. KONTROLA bez progu | (automatycznie) | `STAN -> KONTROLA`, czerwona miga, sygnał – choć T < 60 °C i v < 7,1 mm/s; ok. 1:38 raport `stan` |
| 2:00 | 5. Alarm 71 °C | temperatura = 71 °C | ok. 2:01 `TEMP -> ALARM (71.00)` – przekroczony próg alarmu |
| 2:20 | 6. Kwitowanie | przycisk ACK | `ACK (2.00)`: czerwona świeci ciągle, sygnał milknie; ok. 2:26 lista `zdarzenia 8` – te same zdarzenia idą przez MQTT |
| 2:38 | 7. Serwis | 40 °C, 0 g, przytrzymanie SERWIS 3 s | ok. 2:44 `SERVICE_DONE`, od razu `OK`, „kontrola za ~200 h” |
| 3:00 | koniec | – | podpis „Koniec demonstracji” |

Dźwięk buzzera nie zostanie nagrany standardowym nagraniem ekranu (macOS nie nagrywa dźwięku
systemowego) – alarm widać po lampce i w dzienniku.

## 1. Przygotowanie (ok. 5 minut)

1. Otwórz projekt w Wokwi (pliki z repozytorium – README, sekcja „Jak uruchomić”) i uruchom symulację
   (▶). Poczekaj, aż w monitorze portu pojawiają się co sekundę linie `POSTÓJ | …` lub `PRACA | …`.
2. Karta Wokwi musi być **na wierzchu** (karta w tle spowalnia symulację prawie do zera).
3. Okno przeglądarki zmaksymalizuj. Domyślny podział Wokwi (kod po lewej, symulacja po prawej) jest
   dobry – podpisy pojawią się nad symulacją, w prawej połowie okna. Monitor portu ma być rozwinięty.
4. Otwórz konsolę przeglądarki: Chrome `Cmd+Opt+J` (macOS) / `Ctrl+Shift+J` (Windows, Linux).
   Odłącz ją do osobnego okna, żeby nie trafiła na nagranie: menu **⋮** w konsoli → *Dock side* →
   *Undock into separate window*. Przesuń okno konsoli na bok (lub na drugi monitor).
5. Otwórz plik `demo/rezyser_demo.js`, zaznacz całość i skopiuj. Wklej do konsoli (przy pierwszym
   wklejaniu Chrome poprosi o wpisanie `allow pasting` – wpisz i wklej ponownie). **Jeszcze nie
   naciskaj Enter.**

## 2. Nagrywanie

**macOS:** `Cmd+Shift+5` → *Nagraj zaznaczony fragment* → zaznacz prawą połowę okna przeglądarki
(symulacja, pasek podpisów i monitor portu) → *Opcje*: mikrofon *Brak* (albo wbudowany, jeśli
komentujesz na żywo), opcjonalnie *Pokaż kliknięcia myszą* → **Nagraj**.

**Windows:** `Win+Alt+R` (pasek Xbox Game Bar nagrywa aktywne okno) albo OBS Studio
(*Przechwytywanie ekranu* + przycięcie do prawej połowy).

**Linux:** OBS Studio albo SimpleScreenRecorder; w GNOME `Ctrl+Shift+Alt+R`.

Gdy nagrywanie trwa, przejdź do okna konsoli i naciśnij **Enter**. Skrypt:
1. przez ok. 15 s ustawia warunki początkowe (podpis „PRZYGOTOWANIE”): 40 °C, 0 g, serwis,
   maszyna zatrzymana,
2. odlicza 5 s („START ZA 5 s”…),
3. wykonuje kroki 1–7 (podpis „KROK n/7 · czas”, pasek postępu),
4. po 3:00 wyświetla „Koniec demonstracji”.

Zatrzymaj nagrywanie (macOS: ikona ■ na pasku menu albo `Cmd+Ctrl+Esc`).

## 3. Przycięcie do 3:00

- **QuickTime (macOS):** otwórz plik → *Edycja → Przytnij* (`Cmd+T`) → ustaw początek na pierwszej
  klatce z podpisem „KROK 1/7 · 0:00”, koniec na „Koniec demonstracji” → *Przytnij* →
  *Plik → Eksportuj jako → 1080p*.
- **ffmpeg (dowolny system):** jeśli „KROK 1/7” pojawia się np. w 21. sekundzie nagrania:

  ```bash
  ffmpeg -ss 00:00:21 -i nagranie.mov -t 180 -c:v libx264 -crf 20 -pix_fmt yuv420p -an demo_3min.mp4
  ```

Gotowy plik zapisz obok slajdów (np. `prezentacja/demo_3min.mp4`) i sprawdź, czy otwiera się na
komputerze, na którym będziesz prezentować.

## 4. Odtworzenie na prezentacji

Na slajdzie „Demonstracja na żywo (3 min)” przełącz się na odtwarzacz (QuickTime / VLC, pełny ekran)
i komentuj według tabeli z góry tego dokumentu. Podpisy na nagraniu prowadzą widza same – nawet bez
komentarza widać, co się dzieje.

## 5. Wersja bez skryptu (operator + stoper)

Jeśli nie możesz użyć konsoli przeglądarki, wykonaj kroki ręcznie, patrząc na stoper:

| Czas | Akcja ręczna |
|---|---|
| przed 0:00 | panel DHT22: 40 °C; panel MPU6050: X = 0; w konsoli `serwis`; RUN w lewo (postój) |
| 0:00 | kliknij przełącznik RUN (praca) |
| 0:20 | kliknij MPU6050 → *Acceleration X* o 2 kroki w prawo (0,10 g) |
| 0:36 | w konsoli: `stan` |
| 0:50 | kliknij DHT22 → *Temperature*: zaznacz suwak i trzymaj klawisz `→` – ok. 1 °C co 2–3 s do 56 °C |
| 1:38 | w konsoli: `stan` |
| 2:00 | *Temperature* = 71 °C |
| 2:20 | kliknij przycisk ACK; 2:26 w konsoli `zdarzenia 8` |
| 2:38 | *Temperature* = 40 °C; MPU6050 *Acceleration X* = 0; od 2:41 przytrzymaj SERWIS przez 3 s |

Uwaga: przełączenie panelu z jednego czujnika na drugi wymaga dwóch kliknięć (pierwsze zamyka
otwarty panel).

## 6. Gdy coś nie działa

| Objaw | Co zrobić |
|---|---|
| podpis „BŁĄD … brak linii stanu” | symulacja nie działa albo karta jest w tle – uruchom ▶, wróć na kartę Wokwi, `rezyser.start()` |
| podpis „PRZERWANO – karta Wokwi została ukryta” | okno przeglądarki zostało zasłonięte innym oknem albo przełączono kartę – symulacja wtedy stoi, więc skrypt przerywa pokaz. Odsłoń okno i `rezyser.start()` |
| podpis „BŁĄD … nie udało się otworzyć panelu” | kliknij raz w pusty obszar schematu (zamyka panel), potem `rezyser.start()` |
| w konsoli „UWAGA: przełącznik RUN nie zmienił stanu” | przełącz RUN ręcznie – reszta pokazu działa dalej |
| zdarzenia pojawiają się kilka sekund wcześniej lub później | normalne: odczyt DHT22 co 2,5 s, okno analizy 1 s |
| trzeba przerwać | `rezyser.stop()` – usuwa podpisy i zatrzymuje kroki |
