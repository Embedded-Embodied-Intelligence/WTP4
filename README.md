# WTP4 – Inteligentny system utrzymania ruchu maszyn

Projekt zaliczeniowy z przedmiotu **Wyzwania Technologiczne Przemysłu 4.0**.
System IoT na **ESP32** (symulator [Wokwi](https://wokwi.com)) monitoruje pracę maszyny
– temperaturę, drgania, wilgotność i czas pracy – i informuje o konieczności kontroli lub serwisu.
Dane trafiają lokalnie na wyświetlacz OLED, kolumnę sygnalizacyjną i port szeregowy,
a zdalnie – przez **MQTT** – do systemu nadrzędnego.

> Ten plik jest „dziennikiem” projektu: aktualizujemy go po każdym etapie,
> żeby instrukcje i ustalenia nie ginęły w rozmowach.

## Spis treści
1. [Stan projektu](#stan-projektu)
2. [Struktura repozytorium](#struktura-repozytorium)
3. [Sprzęt i połączenia](#sprzęt-i-połączenia)
4. [Jak uruchomić projekt w Wokwi – krok po kroku](#jak-uruchomić-projekt-w-wokwi--krok-po-kroku)
5. [Jak testować – co kliknąć i czego się spodziewać](#jak-testować--co-kliknąć-i-czego-się-spodziewać)
6. [Podgląd danych MQTT](#podgląd-danych-mqtt)
7. [Znane osobliwości symulatora](#znane-osobliwości-symulatora)
8. [Dziennik decyzji projektowych](#dziennik-decyzji-projektowych)

---

## Stan projektu

| Etap | Zakres | Stan |
|---|---|---|
| 1 | Architektura sprzętowa: `diagram.json`, `libraries.txt` | ✅ gotowy, sprawdzony w Wokwi |
| 2 | Rdzeń oprogramowania: `main.cpp` (przerwania, czujniki, HMI, MQTT) | ✅ gotowy, sprawdzony w Wokwi i przez zewnętrznego klienta MQTT |
| 3 | Logika predykcyjna (analiza wielu parametrów, prognoza terminu kontroli) + komentarze Doxygen | ⏳ w trakcie |
| 4 | `DOKUMENTACJA.md`, `TESTY.md` (8 scenariuszy), `PREZENTACJA.md` | ⏳ |

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
| `main.cpp` | aplikacja (C++, FreeRTOS): akwizycja, przerwania, HMI, MQTT |
| `README.md` | ten plik – stan projektu i instrukcje |
| `Wytyczne Projektowe - ….md` | wytyczne prowadzącego (lokalnie, celowo **nie** w repozytorium publicznym) |

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
| LED czerwona / żółta / zielona | 25 / 26 / 27 | przez 220 Ω |
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
5. **`sketch.ino`** – kliknij zakładkę, zaznacz wszystko i wklej zawartość `sketch.ino`
   z repozytorium (sam komentarz).
   - Dlaczego: Wokwi wymaga pliku `.ino`, ale `setup()` i `loop()` są w `main.cpp`. Gdyby
     w `sketch.ino` zostały domyślne `setup()`/`loop()`, kompilacja zgłosi podwójną definicję.
6. Kliknij zielony przycisk **▶ (Start)** nad symulacją. Pierwsza kompilacja trwa **1–2 minuty**
   (kolejka serwera Wokwi – widać licznik „Compiling project...”).
7. (Opcjonalnie, zalecane przed prezentacją) zaloguj się w Wokwi i kliknij **SAVE** – projekt
   dostanie stały link.

**Co powinno się stać po starcie:**
- przez ok. 1,5 s świecą wszystkie trzy lampki i słychać krótki sygnał (test lampek),
- OLED pokazuje stan maszyny, temperaturę, drgania i czas pracy,
- w **monitorze portu szeregowego** (panel pod symulacją) co sekundę pojawia się linia stanu,
  a po kilku sekundach komunikaty `[SIEĆ] WiFi połączone` i `[MQTT] Połączono`.

> Karta z Wokwi musi być **widoczna** (nie zminimalizowana) – inaczej przeglądarka
> spowalnia symulację prawie do zera.

## Jak testować – co kliknąć i czego się spodziewać

Elementy sterujące są na schemacie (prawa strona). Skróty klawiszowe **A** i **S** działają,
gdy fokus jest na schemacie – najpierw kliknij w puste miejsce schematu.

| Co sprawdzamy | Co zrobić | Czego się spodziewać |
|---|---|---|
| Uruchomienie maszyny | kliknij **przełącznik suwakowy** (mały element pod przyciskiem SERWIS) – suwak w **prawo** | zielona lampka świeci ciągle, OLED: `PRACA`, czas pracy rośnie, w logu `RUN_START` |
| Zatrzymanie maszyny | kliknij przełącznik ponownie – suwak w **lewo** | zielona lampka „pulsuje” (krótki błysk co 2 s), OLED: `POSTOJ`, w logu `RUN_STOP` z czasem cyklu [s] |
| Kwitowanie (ACK) | kliknij **niebieski** przycisk lub naciśnij **A** | krótki sygnał, w logu `ACK` (w Etapie 3 kwituje alarmy) |
| Potwierdzenie serwisu | **przytrzymaj czarny** przycisk (lub klawisz **S**) przez **≥ 3 s** | po wciśnięciu krótki „pip”, po 3 s podwójny sygnał, w logu `SERVICE_DONE`, licznik „od serwisu” zeruje się |
| Drgania | w trakcie symulacji **kliknij moduł MPU6050** (niebieska płytka u góry) – otworzy się panel z suwakami; przy **pracującej** maszynie przesuń *Acceleration X* o jeden krok (+0,05 g) | po ~1 s: `a_rms ≈ 0.050g`, `Vrms ≈ 3.2 mm/s` (OLED i log); powrót suwaka = drgania ~0 |
| Temperatura i wilgotność | kliknij **DHT22** (biały czujnik na dole) – suwaki *Temperature* i *Humidity* | po ≤ 2,5 s nowe wartości na OLED i w logu |
| Usterka czujnika | w `diagram.json` usuń przewód `dht1:SDA` → restart symulacji | po ~7,5 s czerwona lampka miga szybko, w logu `SENSOR_FAULT (2)`, na OLED `DHT ERR` |

**Jak czytać linię stanu w monitorze:**

```
[  37.652] PRACA  | T=40.0°C RH=45% | a_rms=0.050g pk=0.050g CF=1.00 v_rms=3.15mm/s | praca 0:00:36 (od serwisu 0:00:36) starty=1 | MPU OK DHT OK | MQTT online
```

- `PRACA`/`POSTÓJ` – stan sygnału RUN,
- `a_rms`, `pk`, `CF` – wartość skuteczna, szczytowa i współczynnik szczytu drgań (okno 1 s),
- `v_rms` – prędkość drgań w mm/s (miara z normy ISO 10816),
- `praca` / `od serwisu` – licznik czasu pracy (motogodziny) i czas od ostatniego serwisu,
- co 30 s pojawia się linia `[DIAG]` – wolna pamięć, zapas stosu zadań, liczba próbek,
  liczba zboczy z przycisków (widać drgania styków).

## Podgląd danych MQTT

MQTT to protokół „publikuj–subskrybuj”: urządzenie wysyła dane do serwera (brokera),
a dowolny klient może je odbierać.

- Broker: **`broker.emqx.io`** (zapasowy: `test.mosquitto.org`)
- Tematy (`#` = wszystkie naraz: `wtp4/pdm/maszyna-01/#`):

| Temat | Zawartość |
|---|---|
| `wtp4/pdm/maszyna-01/status` | `online` / `offline` (zachowywany; `offline` publikuje broker po zerwaniu połączenia) |
| `wtp4/pdm/maszyna-01/telemetry` | JSON co 5 s: `t`, `rh`, `a_rms`, `v_rms`, `ax/ay/az`, `run_h`, `svc_h`, `starts`, `faults`… |
| `wtp4/pdm/maszyna-01/event` | JSON przy zdarzeniu: `{"seq":2,"ts":54612,"type":"RUN_STOP","value":53.122}` |
| `wtp4/pdm/maszyna-01/cmd` | polecenia do urządzenia: `ping`, `status` |

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
- **Przełącznik RUN po restarcie** symulacji może *wyglądać* na włączony, choć symulacja startuje
  w pozycji „wyłączony” – wtedy przełącz go dwa razy.
- **Suwaki MPU6050** zmieniają przyspieszenie co **0,05 g** (zakres ±2 g) – to najmniejsza
  możliwa do zadania amplituda drgań.
- **Drgania styków** są symulowane: jedno kliknięcie przycisku daje 40–100 przerwań; program
  zamienia je na jedno zdarzenie (widać to w linii `[DIAG]`).
- Po zmianie kodu monitor portu pokazuje **stary log**, dopóki nie skończy się kompilacja.

## Dziennik decyzji projektowych

| Decyzja | Uzasadnienie |
|---|---|
| Dwie magistrale I2C (czujnik i wyświetlacz osobno) | odświeżanie OLED (~25 ms) nie zaburza próbkowania drgań; zawieszony wyświetlacz nie blokuje czujnika |
| Pin INT MPU6050 niepodłączony | Wokwi go nie symuluje – takt próbkowania daje przerwanie timera sprzętowego (100 Hz) |
| Własny sterownik MPU6050 zamiast biblioteki | jeden odczyt 14 B, pełna kontrola konfiguracji (±4 g, filtr 44 Hz), mniej pamięci |
| FreeRTOS: akwizycja (rdzeń 1), sterownik (`loop`), komunikacja (rdzeń 0) | blokujące operacje sieciowe nie wpływają na pomiary |
| Brak `delay()` i alokacji pamięci w pętli pracy | wymaganie projektu; bufory statyczne, brak `String` |
| SERWIS wymaga przytrzymania 3 s; brak zdalnego „serwisu” przez MQTT | ochrona przed przypadkowym/zdalnym wyzerowaniem licznika |
| Status `online` ponawiany co 30 s | zachowany komunikat `offline` (LWT) przerwanej wcześniej sesji potrafi przyjść po `online` nowej |
| Nazwa `word` zakazana w kodzie | w Arduino to makro – przechwyciło funkcję pomocniczą i zerowało odczyty MPU6050 |

Licencja: MIT (plik `LICENSE`).
