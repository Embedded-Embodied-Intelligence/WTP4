# WTP4 – Inteligentny system utrzymania ruchu maszyn

Projekt zaliczeniowy z przedmiotu **Wyzwania Technologiczne Przemysłu 4.0**.
System IoT na **ESP32** (symulator [Wokwi](https://wokwi.com)) monitoruje pracę maszyny
– temperaturę, drgania, wilgotność i czas pracy – analizuje te parametry łącznie
i **prognozuje, kiedy maszyna powinna zostać poddana kontroli**.
Dane trafiają lokalnie na wyświetlacz OLED, kolumnę sygnalizacyjną i port szeregowy,
a zdalnie – przez **MQTT** – do systemu nadrzędnego.

> Ten plik jest „dziennikiem” projektu: aktualizujemy go po każdym etapie,
> żeby instrukcje i ustalenia nie ginęły w rozmowach.

> **Pierwszy raz w tym repozytorium?** Zacznij od [RUNBOOK.md](RUNBOOK.md) – prowadzi od zera:
> uruchomienie, każda kontrolka i dioda, każda linia dziennika, przewodnik po kodzie.

## Spis treści
1. [Stan projektu](#stan-projektu)
2. [Struktura repozytorium](#struktura-repozytorium)
3. [Sprzęt i połączenia](#sprzęt-i-połączenia)
4. [Jak uruchomić projekt w Wokwi – krok po kroku](#jak-uruchomić-projekt-w-wokwi--krok-po-kroku)
5. [Jak działa analiza predykcyjna – w skrócie](#jak-działa-analiza-predykcyjna--w-skrócie)
6. [Jak testować – co kliknąć i czego się spodziewać](#jak-testować--co-kliknąć-i-czego-się-spodziewać)
7. [Polecenia konsoli](#polecenia-konsoli)
8. [Testy jednostkowe na komputerze](#testy-jednostkowe-na-komputerze)
9. [Podgląd danych MQTT](#podgląd-danych-mqtt)
10. [Znane osobliwości symulatora](#znane-osobliwości-symulatora)
11. [Dziennik decyzji projektowych](#dziennik-decyzji-projektowych)

---

## Stan projektu

| Etap | Zakres | Stan |
|---|---|---|
| 1 | Architektura sprzętowa: `diagram.json`, `libraries.txt` | ✅ gotowy, sprawdzony w Wokwi |
| 2 | Rdzeń oprogramowania: `main.cpp` (przerwania, czujniki, HMI, MQTT) | ✅ gotowy, sprawdzony w Wokwi i przez zewnętrznego klienta MQTT |
| 3 | Logika predykcyjna `predictive.h` (zużycie, trendy, diagnoza wieloparametrowa, prognoza terminu kontroli), historia i dziennik zdarzeń, pamięć NVS, konsola, komentarze Doxygen | ✅ gotowy: 84 asercje testów jednostkowych, scenariusze sprawdzone w Wokwi, telemetria sprawdzona zewnętrznym klientem MQTT |
| 4 | `DOKUMENTACJA.md`, `TESTY.md` (8 scenariuszy), `PREZENTACJA.md`; poprawka platformowa (akwizycja bez FPU) | ✅ gotowy: 8/8 scenariuszy zaliczonych w Wokwi na wersji 0.4.0 |
| – | Materiały końcowe: slajdy (`prezentacja/`: PDF, PPTX, HTML), skrypt demonstracji i plan B (`demo/`), `RUNBOOK.md`, `WERYFIKACJA.md` | ✅ gotowe |

Wymagania na ocenę 5.0 (z wytycznych przedmiotu): monitorowanie kilku parametrów, progi alarmowe,
historia pomiarów i zdarzeń, **analiza kilku parametrów jednocześnie i wskazanie, kiedy maszyna
powinna zostać poddana kontroli** – plus kod z przerwaniami i oszczędnym gospodarowaniem pamięcią,
pełna dokumentacja, ≥ 8 scenariuszy testowych i ok. 10-minutowa prezentacja.

## Struktura repozytorium

| Plik | Do czego służy |
|---|---|
| `diagram.json` | schemat połączeń dla Wokwi (części, przewody) |
| `libraries.txt` | lista bibliotek Arduino z przypiętymi wersjami (Wokwi instaluje je sam) |
| `sketch.ino` | **pusty** punkt wejścia – Wokwi wymaga pliku `.ino`, ale cały program jest w `main.cpp` |
| `main.cpp` | aplikacja (C++, FreeRTOS): akwizycja, przerwania, HMI, MQTT, historia i zdarzenia, NVS, konsola |
| `predictive.h` | logika predykcyjna – czyste C++17 bez zależności od Arduino (dzięki temu testowana na komputerze) |
| `test/test_predictive.cpp` | testy jednostkowe logiki predykcyjnej |
| `DOKUMENTACJA.md` | dokumentacja techniczna: opis, schematy, diagramy przepływu danych i algorytmów, testy, wyzwania, rozwój |
| `TESTY.md` | 8 scenariuszy testowych z wynikami z Wokwi |
| `PREZENTACJA.md` | scenariusz 10-minutowej prezentacji z demonstracją na żywo |
| `docs/schemat_wokwi.png` | zrzut układu z Wokwi (schemat połączeń) |
| `RUNBOOK.md` | nauka od zera: uruchomienie, każda kontrolka, dziennik, konsola, MQTT, algorytm, przewodnik po kodzie |
| `WERYFIKACJA.md` | zgodność projektu z wytycznymi i założeniami początkowymi (macierz wymagań z dowodami) |
| `prezentacja/` | slajdy gotowe do użycia (`slajdy.pdf`, `slajdy.pptx`, `slajdy.html`), źródło Marp `slajdy.md`, wykresy i diagramy, skrypt `zbuduj.sh` |
| `demo/` | `rezyser_demo.js` – automatyczna 3-minutowa demonstracja w Wokwi z podpisami; `INSTRUKCJA_NAGRANIA.md` – nagranie planu B |
| `README.md` | ten plik – stan projektu i instrukcje |
| `Wytyczne Projektowe - ….md` | wytyczne prowadzącego (lokalnie, celowo **nie** w repozytorium – wpisane do `.gitignore`) |

## Sprzęt i połączenia

Mikrokontroler: **ESP32 DevKitC V4**. Wszystkie elementy zasilane z 3,3 V.

| Element | Pin(y) ESP32 | Uwagi |
|---|---|---|
| MPU6050 – drgania (akcelerometr) | SDA 21, SCL 22 | magistrala I2C nr 0 (`Wire`), adres 0x68 |
| OLED SSD1306 128×64 | SDA 33, SCL 32 | osobna magistrala I2C nr 1 (`Wire1`), adres 0x3C |
| DHT22 – temperatura i wilgotność | 16 | rezystor podciągający 10 kΩ do 3,3 V |
| Przycisk ACK (niebieski) – kwitowanie | 19 | przerwanie, klawisz **A** |
| Przycisk SERWIS (czarny) – potwierdzenie serwisu | 18 | przerwanie, przytrzymanie **3 s**, klawisz **S** |
| Przełącznik RUN – sygnał „maszyna pracuje” | 17 | przerwanie; suwak w prawo = praca |
| LED czerwona / żółta / zielona | 25 / 26 / 27 | przez 220 Ω; alarm / zaplanuj przegląd / OK |
| Buzzer | 13 | sygnał PWM |

## Jak uruchomić projekt w Wokwi – krok po kroku

Wokwi działa w przeglądarce. Projekt to zestaw plików widocznych jako **zakładki nad edytorem
kodu** (po lewej), a po prawej jest symulacja. **Źródłem prawdy jest to repozytorium** – do Wokwi
pliki się kopiuje (zaznacz wszystko w pliku → skopiuj → wklej w odpowiednią zakładkę).

1. Otwórz **https://wokwi.com/projects/new/esp32**. Powstanie nowy projekt z zakładkami
   `sketch.ino`, `diagram.json` i `Library Manager`.
2. **`diagram.json`** – kliknij zakładkę, w edytorze zaznacz wszystko (`Cmd+A` / `Ctrl+A`)
   i wklej zawartość pliku `diagram.json` z repozytorium. Po prawej pojawi się cały układ.
3. **`libraries.txt`** – obok zakładek jest mała strzałka **▾**. Kliknij ją → **New file...** →
   w polu nazwy wpisz dokładnie `libraries.txt` → **Create** → wklej zawartość pliku.
   - Uwaga: okno może mieć wstępnie wpisaną nazwę innego pliku – wyczyść pole przed wpisaniem.
   - Kontrola: zakładka **Library Manager** powinna pokazać 5 bibliotek.
4. **`main.cpp`** – tak samo: **▾** → **New file...** → `main.cpp` → wklej zawartość pliku.
5. **`predictive.h`** – **▾** → **New file...** → `predictive.h` → wklej zawartość pliku.
   Nazwa musi być dokładnie taka – `main.cpp` dołącza ten plik.
6. **`sketch.ino`** – kliknij zakładkę, zaznacz wszystko i wklej zawartość `sketch.ino`
   z repozytorium (sam komentarz).
   - Dlaczego: Wokwi wymaga pliku `.ino`, ale `setup()` i `loop()` są w `main.cpp`. Gdyby
     w `sketch.ino` zostały domyślne `setup()`/`loop()`, kompilacja zgłosi podwójną definicję.
7. Kliknij zielony przycisk **▶ (Start)** nad symulacją. Pierwsza kompilacja trwa **1–2 minuty**
   (kolejka serwera Wokwi – widać licznik „Compiling project...”).
8. (Opcjonalnie, zalecane przed prezentacją) zaloguj się w Wokwi i kliknij **SAVE** – projekt
   dostanie stały link.

**Co powinno się stać po starcie:**
- przez ok. 1,5 s świecą wszystkie trzy lampki i słychać krótki sygnał (test lampek),
- OLED co 4 s zmienia ekran: **stan** (praca/postój, status, temperatura, wilgotność, drgania,
  kondycja, zużycie, motogodziny, prognoza), **prognoza** (za ile godzin kontrola, według czego,
  przyczyna, trendy) i **trend kondycji** (wykres z ostatnich 10 min, linie 70 % i 40 %),
- w **monitorze portu szeregowego** (panel pod symulacją): `[NVS] Brak zapisanych liczników – start
  od zera`, potem co sekundę linia stanu, a po kilku sekundach `[SIEĆ] WiFi połączone`
  i `[MQTT] Połączono`,
- zielona lampka „pulsuje” – maszyna stoi, stan OK.

> Karta z Wokwi musi być **widoczna** (nie zminimalizowana) – inaczej przeglądarka
> spowalnia symulację prawie do zera.

## Jak działa analiza predykcyjna – w skrócie

> Pełny opis algorytmu z diagramem przepływu danych będzie w `DOKUMENTACJA.md` (Etap 4).

**Przyspieszony czas.** 1 s symulacji = 1 h pracy maszyny (x3600), więc zużycie i trendy widać
w ciągu minut. Interwał przeglądu w warunkach nominalnych to 200 h – ok. 3 min 20 s pracy w symulacji.

Co sekundę (krok = 1 h pracy) silnik predykcyjny:

1. **Całkuje zużycie** (reguła Palmgrena–Minera) – każda godzina pracy zużywa `k = k_T · k_V`
   godzin „równoważnych”:
   - `k_T = 2^((T − 40 °C) / 10 °C)` – reguła 10 °C: każde +10 °C podwaja tempo starzenia smaru i izolacji,
   - `k_V = (v / 2,8 mm/s)³` powyżej 2,8 mm/s – trwałość łożysk L10 maleje z sześcianem obciążenia,
   - każdy rozruch dolicza 0,5 h.
2. **Wyznacza trendy** temperatury i drgań – regresja liniowa w oknie 60 ostatnich kroków
   (aktualizacja O(1)) i czas do osiągnięcia progu alarmu. Trend jest brany pod uwagę tylko,
   gdy jest wiarygodny: ≥ 15 próbek, R² ≥ 0,6, wzrost ≥ 0,2 °C/h (drgania ≥ 0,02 mm/s/h).
3. **Sprawdza progi** – z histerezą i opóźnieniem (2 kolejne oceny); alarmy wg ISA-18.2:

   | Parametr | Ostrzeżenie | Alarm | Histereza |
   |---|---|---|---|
   | Temperatura obudowy | ≥ 60 °C | ≥ 70 °C | 2 °C |
   | Drgania v_rms (ISO 10816-1, kl. II) | ≥ 2,8 mm/s (strefa C) | ≥ 7,1 mm/s (strefa D) | 0,3 mm/s |
   | Wilgotność | ≥ 80 % | ≥ 90 % | 3 % |

4. **Liczy indeks kondycji HI** (0–100 %): średnia ważona ocen – drgania 35 %, temperatura 30 %,
   zużycie 25 %, wilgotność 10 % – ograniczona przez najgorszą ocenę + 30 pkt, żeby jeden
   krytyczny parametr nie „zginął” w średniej.
5. **Prognozuje termin kontroli** = najkrótszy z trzech czasów: do przeglądu wg zużycia,
   do alarmu temperatury wg trendu, do alarmu drgań wg trendu.
6. **Diagnozuje przyczynę** z korelacji parametrów (w kolejności priorytetu):

   | Objawy | Przyczyna (log, OLED) |
   |---|---|
   | usterka czujnika | `usterka czujnika` |
   | drgania ≥ progu ostrzeżenia **i** wysoka lub rosnąca temperatura | `lozysko/smarowanie` |
   | wysoka temperatura (lub jej trend) | `przegrzewanie` |
   | drgania (lub ich trend) przy stabilnej temperaturze | `niewywazenie/luz` |
   | wilgotność ≥ 80 % | `wilgoc/korozja` |
   | zużycie ≥ 80 % lub przegląd za ≤ 48 h | `przeglad okresowy` |

7. **Ustala status utrzymania:**

   | Status | Kiedy | Lampka |
   |---|---|---|
   | `OK` | brak zastrzeżeń | zielona: ciągła (praca) / pulsująca (postój) |
   | `PLAN` – zaplanuj przegląd | ostrzeżenie parametru, usterka czujnika, zużycie ≥ 80 %, prognoza ≤ 48 h lub HI < 70 % | żółta |
   | `KONTROLA` – wymagana kontrola | alarm parametru, zużycie ≥ 100 %, prognoza ≤ 8 h lub HI < 40 % | czerwona |

   Podwyższenie statusu wymaga potwierdzenia w 2 kolejnych krokach (pojedynczy zakłócony pomiar
   nie wywoła alarmu), obniżenie – w 5 krokach (bez „migotania” na granicy progu).

**Czerwona lampka i buzzer** (alarmy wg ISA-18.2):
- miga szybko + „pip-pip” co 2 s – alarm aktywny, niepotwierdzony → naciśnij ACK,
- świeci ciągle – alarm aktywny, potwierdzony,
- miga wolno – alarm ustąpił, ale nie został potwierdzony (też wymaga ACK),
- zgaszona – brak alarmów.

**Historia i pamięć:** co 5 s rekord historii (120 rekordów po 16 B = 10 min), ostatnie 32 zdarzenia
w RAM; liczniki (motogodziny, czas od serwisu, zużycie, starty, serwisy, alarmy) zapisywane
w pamięci nieulotnej NVS co 60 s oraz przy zatrzymaniu maszyny i serwisie (z sumą kontrolną CRC-16).

## Jak testować – co kliknąć i czego się spodziewać

Elementy sterujące są na schemacie (prawa strona). Skróty klawiszowe **A** i **S** działają,
gdy fokus jest na schemacie – najpierw kliknij w puste miejsce schematu. Polecenia konsoli
wpisuje się w pole tekstowe na dole monitora portu szeregowego i zatwierdza **Enterem**
(lista: [Polecenia konsoli](#polecenia-konsoli)).

> Czas jest przyspieszony: pracująca maszyna zużywa się o 1 h na sekundę. Po ok. 2,5 min pracy
> pojawi się `PLAN` (przegląd okresowy), po ok. 3 min `KONTROLA` – to zamierzone. `serwis` zeruje zużycie.

| Co sprawdzamy | Co zrobić | Czego się spodziewać |
|---|---|---|
| Uruchomienie maszyny | kliknij **przełącznik suwakowy** (mały element pod przyciskiem SERWIS) – suwak w **prawo** | `RUN_START`, OLED `PRACA`, zielona lampka świeci ciągle, motogodziny rosną o 1 h na sekundę |
| Zatrzymanie maszyny | kliknij przełącznik ponownie – suwak w **lewo** | `RUN_STOP` z czasem cyklu [h], zielona lampka pulsuje (krótki błysk co 2 s) |
| Kwitowanie (ACK) | **niebieski** przycisk, klawisz **A** lub polecenie `ack` | `ACK (n)` – n = liczba potwierdzonych kanałów; czerwona przestaje migać (świeci ciągle, jeśli alarm trwa, gaśnie, jeśli już ustąpił), buzzer milknie |
| Serwis | **przytrzymaj czarny** przycisk (lub **S**) ≥ 3 s albo wpisz `serwis` | `SERVICE_DONE (zużycie przed serwisem w %)`, zużycie 0 %, od razu `OK` i „kontrola za ~200 h” |
| Przegląd okresowy | wpisz `zuzycie 85`, potem `zuzycie 100` | po 2 s `STAN -> PLAN` (żółta), potem `STAN -> KONTROLA` (czerwona miga, sygnał) |
| Temperatura | kliknij **DHT22** (biały czujnik na dole) → suwak *Temperature* na ≥ 60 °C, potem ≥ 70 °C | `TEMP -> OSTRZEZENIE` i `PLAN` (przyczyna `przegrzewanie`), potem `TEMP -> ALARM` i `KONTROLA`; tempo zużycia rośnie (x2 na każde +10 °C) |
| Trend temperatury (predykcja) | przy **pracującej** maszynie podnoś temperaturę od 40 °C stopniowo, ok. 1 °C co 2–3 s (równomiernie – skryptem pod tabelą) | po ok. 15 s prognoza przełącza się na `(trend temp.)` i maleje; przy ok. 55–57 °C `STAN -> PLAN` – **zanim** temperatura przekroczy próg ostrzeżenia 60 °C (przy przyspieszonym czasie prognoza z zużycia maleje równie szybko, więc w linii stanu może być wskazane `(zuzycie)`) |
| Drgania | kliknij moduł **MPU6050** (niebieska płytka u góry) → przy **pracującej** maszynie przesuń *Acceleration X* o 1, 2 lub 5 kroków (krok = 0,05 g) | +0,05 g: v ≈ 1,6 mm/s, strefa B (OK); +0,10 g: ≈ 3,2 mm/s, strefa C, `VIB -> OSTRZEZENIE`, przyczyna `niewywazenie/luz`; +0,25 g: ≈ 7,9 mm/s, strefa D, `VIB -> ALARM`, `KONTROLA` |
| Diagnoza wieloparametrowa | drgania +0,10 g, a potem podnieś temperaturę | przyczyna zmienia się z `niewywazenie/luz` na `lozysko/smarowanie` |
| Wilgotność | suwak *Humidity* DHT22 na ≥ 80 % | `HUM -> OSTRZEZENIE`, `PLAN`, przyczyna `wilgoc/korozja` |
| Usterka czujnika | w `diagram.json` usuń linię `["dht1:SDA", "esp:16", …]` (przewód danych DHT22 → ESP32) i uruchom symulację ponownie | po ok. 6–7 s `SENSOR_FAULT (2.00)`, potem `STAN -> PLAN`; czerwona miga szybko + sygnał; OLED `T --- RH ---`, ekran prognozy: `usterka czujnika`. **Po teście przywróć linię.** |
| Pamięć nieulotna | wpisz `restart` | restart sterownika, `[NVS] Odtworzono: …`, zdarzenie `RESTORED` – liczniki zachowane |
| Historia i zdarzenia | wpisz `historia 6`, `zdarzenia 8` | tabela ostatnich rekordów (co 5 s) i lista ostatnich zdarzeń |

Na postoju przesunięcie suwaka MPU6050 jest traktowane jak zmiana położenia czujnika
(nowy wektor grawitacji) i po kilku sekundach przestaje być widoczne jako drgania –
dlatego drgania testuje się przy pracującej maszynie.

<details>
<summary><b>Skrypt: równomierna rampa temperatury (konsola przeglądarki)</b></summary>

Suwakiem trudno przesuwać temperaturę równo o 1 °C. Do powtarzalnej demonstracji trendu:

1. Uruchom symulację, włącz maszynę (RUN) i kliknij DHT22, żeby otworzyć panel z suwakami.
2. Otwórz konsolę przeglądarki (Chrome: `Cmd+Opt+J` na macOS, `Ctrl+Shift+J` na Windows/Linux).
   Przy pierwszym wklejaniu Chrome może poprosić o wpisanie `allow pasting`.
3. Wklej i zatwierdź Enterem:

```js
// Rampa DHT22: od 40 °C, co 2,5 s o +1 °C, do 58 °C
(() => {
  const s = [...document.querySelectorAll('input[type=range]')].find(e => e.min === '-40' && e.max === '80');
  if (!s) return 'Najpierw kliknij DHT22 (panel z suwakami musi być otwarty)';
  const set = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
  let t = 40;
  const id = setInterval(() => {
    set.call(s, String(t));
    s.dispatchEvent(new Event('input', { bubbles: true }));
    s.dispatchEvent(new Event('change', { bubbles: true }));
    if (++t > 58) clearInterval(id);
  }, 2500);
  return 'Rampa uruchomiona';
})();
```
</details>

**Jak czytać linię stanu w monitorze:**

```
[  40.727] PRACA  | T=72.0°C RH=45% | v=0.00mm/s ISO A (a=0.000g CF=0.00) | HI 52% zużycie 75% x5.6 | kontrola za 9 h (zuzycie) | KONTROLA [ALARM - wymagane ACK] | mth 39.1, starty 0 | MQTT online
```

- `PRACA`/`POSTÓJ` – stan sygnału RUN,
- `T`, `RH` – temperatura i wilgotność z DHT22 (`---` = brak odczytu),
- `v` – prędkość drgań (wartość skuteczna, mm/s) i strefa ISO 10816 (A–D); w nawiasie
  przyspieszenie skuteczne `a` i współczynnik szczytu `CF` (okno 1 s),
- `HI` – indeks kondycji maszyny (100 % = bardzo dobry stan),
- `zużycie 75% x5.6` – wykorzystana część interwału przeglądu i bieżące tempo zużycia
  (x1.0 = warunki nominalne),
- `kontrola za 9 h (zuzycie)` – prognozowany czas pracy do kontroli i czynnik, który go wyznaczył
  (`zuzycie`, `trend temp.`, `trend drgan`); `TERAZ` = kontrola wymagana już teraz,
- `OK`/`PLAN`/`KONTROLA` – status utrzymania; `[ALARM - wymagane ACK]` – jest alarm niepotwierdzony,
- `mth` – motogodziny (czas pracy maszyny), `starty` – liczba uruchomień,
- co 30 s pojawiają się dwie linie `[DIAG]` – wolna pamięć, zapas stosu zadań, liczba próbek,
  pominięte takty próbkowania, liczba zboczy z przycisków (widać drgania styków), stan historii
  i MQTT oraz wynik monitora integralności FPU (`FPU: błędy 0 / kontrole N` – zob. niżej).

## Polecenia konsoli

| Polecenie | Działanie |
|---|---|
| `pomoc` (`help`) | lista poleceń |
| `stan` | pełny raport: wartości i progi, stany alarmów, kondycja, zużycie, trendy, prognoza i przyczyna |
| `historia [n]` | ostatnie n rekordów historii (domyślnie 10, maks. 120) |
| `zdarzenia [n]` | ostatnie n zdarzeń (domyślnie 10, maks. 32) |
| `ack` | kwitowanie alarmów (jak przycisk ACK) |
| `serwis` | potwierdzenie serwisu (jak przytrzymanie przycisku SERWIS) |
| `zuzycie <proc>` | ustawienie zużycia, np. `zuzycie 85` (demonstracja i testy) |
| `zapisz` | natychmiastowy zapis liczników do NVS |
| `restart` | zapis liczników i restart sterownika (test pamięci nieulotnej) |

## Testy jednostkowe na komputerze

Logika predykcyjna (`predictive.h`) nie zależy od Arduino, więc można ją testować bez ESP32.
W katalogu głównym repozytorium:

```bash
c++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -fsanitize=address,undefined \
    -o test_predictive test/test_predictive.cpp && ./test_predictive
```

Oczekiwany wynik: `Wynik: 84 asercji spelnionych, 0 niespelnionych` (kod wyjścia 0).
Kompilator: na macOS `clang` z Xcode Command Line Tools (`xcode-select --install`), na Linuksie `g++`.
Flagi `-fsanitize` dodatkowo wykrywają błędy pamięci i niezdefiniowane zachowania.

Zakres: bufor cykliczny, trend O(1) (porównanie z klasyczną regresją), kanały alarmowe (opóźnienie,
histereza, stany ISA-18.2), model zużycia, indeks kondycji oraz scenariusze silnika – praca nominalna,
przegląd okresowy, trend temperatury, łożysko, niewyważenie, wilgotność, usterka czujnika, przegląd
po przegrzaniu, odporność na pojedynczy zakłócony krok i antyoscylacja.

## Podgląd danych MQTT

MQTT to protokół „publikuj–subskrybuj”: urządzenie wysyła dane do serwera (brokera),
a dowolny klient może je odbierać.

- Broker: **`broker.emqx.io`** (zapasowy: `test.mosquitto.org`)
- Tematy (`#` = wszystkie naraz: `wtp4/pdm/maszyna-01/#`):

| Temat | Zawartość |
|---|---|
| `wtp4/pdm/maszyna-01/status` | `online` / `offline` (zachowywany; `offline` publikuje broker po zerwaniu połączenia) |
| `wtp4/pdm/maszyna-01/telemetry` | JSON co 5 s (37 pól, ok. 440 B) – opis poniżej |
| `wtp4/pdm/maszyna-01/event` | JSON przy każdym zdarzeniu, np. `{"seq":5,"ts":110614,"type":"LEVEL","param":"TEMP","level":"ALARM","value":71.00}` |
| `wtp4/pdm/maszyna-01/cmd` | polecenia do urządzenia: `ping`, `status`, `ack` (kwitowanie zdalne; serwisu celowo nie da się potwierdzić zdalnie) |

Pola telemetrii:

| Grupa | Pola |
|---|---|
| pomiary | `t`, `rh`, `dht_ok`, `a_rms`, `a_peak`, `cf`, `v_rms`, `zone`, `ax`/`ay`/`az`, `mpu_t`, `mpu_ok` |
| praca | `run` (0/1), `run_h` (motogodziny), `svc_h` (od serwisu), `starts` |
| analiza | `hi` (kondycja %), `wear` (zużycie %), `eq_h` (h równoważne), `rate` (tempo zużycia), `state` (OK/PLAN/KONTROLA), `cause` (przyczyna), `insp_h` (prognoza do kontroli [h]), `drv` (czynnik prognozy), `t_tr`/`t_r2`, `v_tr`/`v_r2` (nachylenia trendów i R²) |
| alarmy | `alarms` (maska kanałów w alarmie: bit 0 TEMP, 1 VIB, 2 HUM, 3 MAINT, 4 SENSOR), `unack` (czy jest alarm niepotwierdzony) |
| diagnostyka | `seq`, `ts`, `faults`, `ovr` (pominięte takty próbkowania), `rssi`, `heap` |

Typy zdarzeń: `BOOT`, `RESTORED`, `RUN_START`, `RUN_STOP`, `ACK`, `SERVICE_DONE`, `SENSOR_FAULT`,
`SENSOR_OK` oraz `LEVEL` – zmiana poziomu kanału (`param`: TEMP/VIB/HUM/MAINT, `level`:
NORMA/OSTRZEZENIE/ALARM, a dla MAINT: OK/PLAN/KONTROLA).

**Podgląd w przeglądarce (MQTTX Web):**
1. Wejdź na https://mqttx.app/web-client i kliknij **New Connection**.
2. Wypełnij: *Name* – dowolna, *Host* – wybierz `wss://` i wpisz `broker.emqx.io`,
   *Port* – `8084`, *Path* – `/mqtt`. Kliknij **Connect**.
3. Kliknij **New Subscription**, w polu *Topic* wpisz `wtp4/pdm/maszyna-01/#` → **Confirm**.
4. Wiadomości pojawią się po prawej (telemetria co 5 s).
5. Aby wysłać polecenie: na dole okna ustaw temat `wtp4/pdm/maszyna-01/cmd`, treść `ping` → wyślij.

**Podgląd w terminalu** (jeśli masz Mosquitto):
`mosquitto_sub -h broker.emqx.io -t 'wtp4/pdm/maszyna-01/#' -v`

## Znane osobliwości symulatora

- **DHT22 na GPIO4** zawsze zwraca `TIMEOUT` (ten sam kod działa na GPIO15/16) – dlatego GPIO16.
- **Przełącznik RUN** – jeśli wygląda na włączony, a log pokazuje `POSTÓJ`, przełącz go dwa razy.
  Zmiana `diagram.json` tworzy elementy od nowa – przełącznik wraca wtedy do pozycji „wyłączony”.
- **Restart symulacji (Stop → Start) czyści pamięć flash** – NVS startuje od zera
  (`[NVS] Brak zapisanych liczników`). Zachowanie liczników sprawdza się poleceniem `restart`
  (restart programowy w tej samej symulacji).
- **Suwaki MPU6050** zmieniają przyspieszenie co **0,05 g** (zakres ±2 g) – to najmniejsza
  możliwa do zadania amplituda drgań.
- **Drgania styków** są symulowane: jedno kliknięcie przycisku daje 40–100 przerwań; program
  zamienia je na jedno zdarzenie (widać to w linii `[DIAG]`).
- Po zmianie kodu monitor portu pokazuje **stary log**, dopóki nie skończy się kompilacja
  (na darmowym koncie kompilacja czeka w kolejce, zwykle 40–60 s).
- **Rejestry FPU nie są zachowywane przy przełączaniu zadań na tym samym rdzeniu.** Gdy zadanie
  o wyższym priorytecie, które liczy na liczbach zmiennoprzecinkowych, wywłaszczy inne takie zadanie
  na tym samym rdzeniu, przerwane obliczenie daje błędny wynik. Test wzorcowy: 11 475 z 11 475 obliczeń
  błędnych; gdy zadanie przerywające liczy na liczbach całkowitych albo działa na drugim rdzeniu –
  0 błędów. To była przyczyna jednorazowego fałszywego alarmu z Etapu 3 („kontrola za 1 h (trend
  drgan)” przy zatrzymanej maszynie). Rozwiązanie: zadanie akwizycji liczy wyłącznie na liczbach
  całkowitych, a odczyt DHT22 i obliczenia zmiennoprzecinkowe wykonuje sterownik – na rdzeniu 1
  z FPU korzysta tylko jedno zadanie. Monitor integralności FPU (linia `[DIAG]`): przed zmianą
  105 błędów w 270 s, po zmianie 0 błędów w 34 658 kontrolach (390 s).
- Pierwszy odczyt DHT22 po **restarcie programowym** (`restart`) bywa nieudany – przez ok. 2,5 s
  widać `T=---`; usterka nie jest zgłaszana, bo wymaga 3 kolejnych błędów.

## Dziennik decyzji projektowych

| Decyzja | Uzasadnienie |
|---|---|
| Dwie magistrale I2C (czujnik i wyświetlacz osobno) | odświeżanie OLED (~25 ms) nie zaburza próbkowania drgań; zawieszony wyświetlacz nie blokuje czujnika |
| Pin INT MPU6050 niepodłączony | Wokwi go nie symuluje – takt próbkowania daje przerwanie timera sprzętowego (100 Hz) |
| Własny sterownik MPU6050 zamiast biblioteki | jeden odczyt 14 B, pełna kontrola konfiguracji (±4 g, filtr 44 Hz), mniej pamięci |
| FreeRTOS: akwizycja (rdzeń 1), sterownik (`loop`), komunikacja (rdzeń 0) | blokujące operacje sieciowe nie wpływają na pomiary |
| Zadanie akwizycji bez FPU (arytmetyka stałoprzecinkowa Q8, sumy w `uint64`); DHT22 i obliczenia zmiennoprzecinkowe w sterowniku | Wokwi nie zachowuje rejestrów FPU przy wywłaszczeniu (zmierzone) – na każdym rdzeniu jeden użytkownik FPU; na sprzęcie mniej przełączeń kontekstu FPU |
| Monitor integralności FPU w `[DIAG]` | samodiagnostyka platformy: wykrył problem przed poprawką (105 błędów / 270 s), po poprawce 0 |
| Brak `delay()` i alokacji pamięci w pętli pracy | wymaganie projektu; bufory statyczne, brak `String` |
| SERWIS wymaga przytrzymania 3 s; brak zdalnego „serwisu” przez MQTT | ochrona przed przypadkowym/zdalnym wyzerowaniem licznika |
| Status `online` ponawiany co 30 s | zachowany komunikat `offline` (LWT) przerwanej wcześniej sesji potrafi przyjść po `online` nowej |
| Nazwa `word` zakazana w kodzie | w Arduino to makro – przechwyciło funkcję pomocniczą i zerowało odczyty MPU6050 |
| Logika predykcyjna w osobnym pliku `predictive.h`, bez zależności od Arduino | testy jednostkowe na komputerze (z sanitizerami); wyraźny podział: sprzęt i komunikacja vs. algorytm |
| Fizyczny model zużycia (Palmgren–Miner, reguła 10 °C, L10 ~ obciążenie³) zamiast uczenia maszynowego | działa bez danych uczących, każdy wynik da się wyjaśnić, kilka parametrów konfiguracyjnych |
| Prognoza = minimum z trzech źródeł (zużycie, trend temperatury, trend drgań) | wskazuje najbliższe zagrożenie i podaje, co je wyznaczyło |
| Trend tylko przy ≥ 15 próbkach i R² ≥ 0,6 | szum pomiarowy nie generuje fałszywych prognoz |
| Status: podwyższenie po 2 krokach, obniżenie po 5 | brak alarmów ulotnych (pojedynczy zakłócony pomiar) i „migotania” na granicy progu (ISA-18.2) |
| Po serwisie filtry EWMA (tempo zużycia, kondycja) startują od nowa | prognoza zaraz po przeglądzie odpowiada bieżącym warunkom (wcześniej przez ok. 9 s widniał fałszywy `PLAN`) |
| Czas przyspieszony x3600 | zużycie i trendy widać w minutach – potrzebne do demonstracji; we wdrożeniu `kTimeScale = 1` |
| Liczniki w NVS co 60 s oraz przy zatrzymaniu i serwisie, z CRC-16 | kompromis między zużyciem pamięci flash a utratą danych przy zaniku zasilania |
| Rekordy historii po 16 B (stałoprzecinkowo), bufory statyczne | 120 rekordów = 1,9 KB RAM; brak fragmentacji sterty |

Licencja: MIT (plik `LICENSE`).
