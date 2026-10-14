# Scenariusze testowe i wyniki

Osiem scenariuszy obejmuje przypadki standardowe, brzegowe i awaryjne oraz wszystkie progi wymagań
(3.0–5.0). Każdy wykonano w symulatorze Wokwi; wyniki to fragmenty dzienników z monitora portu
szeregowego i komunikaty MQTT odebrane zewnętrznym klientem.

## Środowisko

| | |
|---|---|
| Data wykonania | 14.10.2026 |
| Oprogramowanie | WTP4-PdM 0.4.0 (commit `a79f4b0`), arduino-esp32 3.3.7 |
| Platforma | Wokwi, ESP32 DevKitC V4, układ z `diagram.json` |
| Sterowanie | przełącznik RUN, panele czujników Wokwi (suwaki), polecenia konsoli |
| Weryfikacja MQTT | zewnętrzny klient MQTT 3.1.1 subskrybujący `wtp4/pdm/maszyna-01/#` na `broker.emqx.io` |
| Czas maszyny | przyspieszony: 1 s symulacji = 1 h pracy |

Uwaga do czytania dzienników: w znacznikach `[  176.614]` jest czas od startu sterownika w sekundach;
wpisy `[ZDARZENIE #n]` trafiają też do MQTT (temat `event`).

## Podsumowanie

| Nr | Scenariusz | Typ | Wymaganie | Wynik |
|---|---|---|---|---|
| T1 | Uruchomienie i zatrzymanie maszyny – licznik motogodzin | standardowy | 3.5, 4.5 | ✅ |
| T2 | Progi temperatury, alarm i kwitowanie | standardowy | 4.0 | ✅ |
| T3 | Drgania – strefy ISO 10816, alarm, diagnoza | standardowy, brzegowy | 3.0, 4.0, 5.0 | ✅ |
| T4 | Predykcja – trend temperatury wyprzedza próg | predykcyjny | 5.0 | ✅ |
| T5 | Diagnoza wieloparametrowa – łożysko | predykcyjny | 5.0 | ✅ |
| T6 | Przegląd okresowy i potwierdzenie serwisu | standardowy, brzegowy | 5.0 | ✅ |
| T7 | Usterka czujnika DHT22 | awaryjny | 4.0, 4.5 | ✅ |
| T8 | Historia, dziennik zdarzeń i pamięć nieulotna po restarcie | brzegowy | 4.5 | ✅ |

---

## T1. Uruchomienie i zatrzymanie maszyny – licznik motogodzin

| | |
|---|---|
| **Typ** | standardowy |
| **Cel** | czas pracy i liczba uruchomień (3.5); zdarzenia pracy (4.5); odporność wejścia na drgania styków |
| **Warunki początkowe** | maszyna pracuje (RUN włączony), T = 40 °C, drgania 0, wykonany `serwis` |
| **Kroki** | 1. Przełącznik RUN → wyłączony. 2. Po 4 s przełącznik RUN → włączony. |
| **Oczekiwany wynik** | `RUN_STOP` z czasem cyklu, na postoju motogodziny stoją; `RUN_START` z numerem uruchomienia, motogodziny rosną o 1 h/s; po jednym zdarzeniu na przełączenie mimo drgań styków |
| **Wynik** | ✅ zgodny z oczekiwanym |

```
[   149.412] PRACA  | T=40.0°C RH=45% | ... | OK  | mth 147.8, starty 0
[   150.312] [ZDARZENIE #9] RUN_STOP (148.79)
[   150.413] POSTÓJ | T=40.0°C RH=45% | ... | OK  | mth 148.8, starty 0
[   154.436] POSTÓJ | T=40.0°C RH=45% | ... | OK  | mth 148.8, starty 0
[   154.815] [ZDARZENIE #10] RUN_START (1.00)
[   155.440] PRACA  | T=40.0°C RH=45% | ... | OK  | mth 149.4, starty 1
[   157.471] PRACA  | T=40.0°C RH=45% | ... | OK  | mth 151.4, starty 1
[DIAG] ... | zbocza ACK=0 SERWIS=0 RUN=177
```

MQTT: `{"seq":9,"ts":150311,"type":"RUN_STOP","value":148.793}`,
`{"seq":10,"ts":154815,"type":"RUN_START","value":1.000}`; status `online` co 30 s,
telemetria co 5 s (24 wiadomości w 2 min).

**Wnioski:** dwa przełączenia wygenerowały 177 przerwań (drgania styków symulowane przez Wokwi),
a zarejestrowano dokładnie dwa zdarzenia. Na postoju licznik motogodzin stoi (148,8 h).

---

## T2. Progi temperatury, alarm i kwitowanie

| | |
|---|---|
| **Typ** | standardowy |
| **Cel** | progi ostrzeżenia i alarmu, histereza i zwłoka, stany alarmu wg ISA-18.2 (4.0) |
| **Warunki początkowe** | maszyna pracuje, T = 40 °C, status OK |
| **Kroki** | 1. Suwak *Temperature* DHT22 → 61 °C. 2. Po 6 s → 71 °C. 3. Polecenie `ack`. 4. Temperatura → 40 °C. |
| **Oczekiwany wynik** | `TEMP -> OSTRZEZENIE` i status PLAN; `TEMP -> ALARM` i status KONTROLA, czerwona lampka i sygnał; ACK potwierdza alarmy; po spadku temperatury `TEMP -> NORMA`, status obniża się dopiero po 5 krokach |
| **Wynik** | ✅ zgodny z oczekiwanym |

```
[   176.614] [ZDARZENIE #11] TEMP -> OSTRZEZENIE (61.00)
[   177.614] [ZDARZENIE #12] STAN -> PLAN (kontrola za 85 h)
[   181.631] [ZDARZENIE #13] TEMP -> ALARM (71.00)
[   182.652] [ZDARZENIE #14] STAN -> KONTROLA (kontrola za 31 h)
[   185.470] [ALARM] Kwitowanie (konsola): potwierdzono 2 kanał(ów)
[   185.471] [ZDARZENIE #15] ACK (2.00)
[   188.615] [ZDARZENIE #16] TEMP -> NORMA (40.00)
[   193.615] [ZDARZENIE #17] STAN -> PLAN (kontrola za 21 h)
```

MQTT (temat `event`):
`{"seq":11,...,"type":"LEVEL","param":"TEMP","level":"OSTRZEZENIE","value":61.00}`,
`{"seq":13,...,"param":"TEMP","level":"ALARM","value":71.00}`,
`{"seq":14,...,"param":"MAINT","level":"KONTROLA","value":31.05}`, `{"seq":15,...,"type":"ACK","value":2.000}`.

**Wnioski:** każdy próg zadziałał po 2 ocenach (zwłoka), status po 1 s potwierdzenia. ACK objął dwa
kanały (TEMP i MAINT). Po powrocie do 40 °C status nie wrócił do OK, lecz do PLAN – podczas przegrzania
zużycie rosło 4–9 razy szybciej, więc prognoza przeglądu skróciła się do 21 h. To poprawne zachowanie
modelu zużycia.

---

## T3. Drgania – strefy ISO 10816, alarm, diagnoza

| | |
|---|---|
| **Typ** | standardowy; brzegowy (skok w środku okna pomiarowego) |
| **Cel** | pomiar drgań (3.0), progi stref C i D (4.0), diagnoza przyczyny (5.0) |
| **Warunki początkowe** | maszyna pracuje, T = 40 °C, wykonany `serwis`, przyspieszenie X = 0 |
| **Kroki** | 1. Panel MPU6050: *Acceleration X* = +0,05 g. 2. +0,10 g, polecenie `stan`. 3. (wcześniej w tej samej symulacji, ok. 48 s od startu) +0,25 g, `stan`, `ack`, powrót do 0. |
| **Oczekiwany wynik** | 0,05 g → v ≈ 1,6 mm/s, strefa B, OK; 0,10 g → ≈ 3,2 mm/s, strefa C, ostrzeżenie, PLAN, przyczyna `niewywazenie/luz`; 0,25 g → ≈ 7,9 mm/s, strefa D, alarm, KONTROLA |
| **Wynik** | ✅ zgodny z oczekiwanym |

```
[   240.144] PRACA  | T=40.0°C RH=45% | v=1.58mm/s ISO B (a=0.050g CF=1.00) | ... | OK
[   244.182] PRACA  | T=40.0°C RH=45% | v=3.17mm/s ISO C (a=0.100g CF=1.00) | ... | OK
[   244.618] [ZDARZENIE #19] VIB -> OSTRZEZENIE (3.17)
[   245.618] [ZDARZENIE #20] STAN -> PLAN (kontrola za 147 h)
stan:  VIB  3.17  poziom OSTRZEZENIE
       Kondycja: 85% (temp 1.00, drgania 0.66, wilg. 1.00, zuzycie 0.81) | strefa ISO C
       PROGNOZA: kontrola za 55 h (wg: trend drgan) | STAN: PLAN | przyczyna: niewywazenie/luz

[    48.637] PRACA  | ... | v=0.79mm/s ISO A (a=0.025g CF=10.00) | ...      <- okno przejściowe
[    49.637] PRACA  | ... | v=7.93mm/s ISO D (a=0.250g CF=1.00) | ...
[    50.607] [ZDARZENIE #4] VIB -> ALARM (7.93)
[    51.607] [ZDARZENIE #5] STAN -> KONTROLA (kontrola za 16 h)
stan:  VIB  7.93  poziom ALARM  alarm NIEPOTWIERDZ.
       Kondycja: 30% (temp 1.00, drgania 0.20, wilg. 1.00, zuzycie 0.00) | strefa ISO D
       PROGNOZA: kontrola za TERAZ (wg: zuzycie) | STAN: KONTROLA | przyczyna: niewywazenie/luz
[    69.485] [ZDARZENIE #6] ACK (2.00)
[    70.608] [ZDARZENIE #7] VIB -> NORMA (0.00)
```

**Wnioski:** wartości zgodne z obliczeniem v = a · g / (2π · 49,2 Hz) (0,25 g → 7,93 mm/s).
Przypadek brzegowy: skok wypadł w ostatniej próbce okna, więc okno przejściowe dało a_RMS = 0,25/10 g
i współczynnik szczytu 10 – poprawny wynik dla jednej próbki na 100, który nie wywołał alarmu
(zwłoka 2 ocen). Silne drgania (k_V ≈ 23) przyspieszyły zużycie – stąd „kontrola TERAZ”.

---

## T4. Predykcja – trend temperatury wyprzedza próg

| | |
|---|---|
| **Typ** | predykcyjny |
| **Cel** | wskazanie terminu kontroli **przed** przekroczeniem progu (5.0) |
| **Warunki początkowe** | maszyna pracuje, X = 0, T = 40 °C, wykonany `serwis` (okna trendów wyzerowane) |
| **Kroki** | Temperatura rośnie o 1 °C co 2,5 s od 40 do 58 °C (skrypt rampy z README). |
| **Oczekiwany wynik** | po zebraniu ≥ 15 próbek prognoza wg `trend temp.` i maleje; PLAN, gdy prognoza ≤ 48 h – przed progiem ostrzeżenia 60 °C; brak zdarzeń `TEMP` |
| **Wynik** | ✅ zgodny z oczekiwanym |

```
[   330.984] T=45.0°C | HI 98% zużycie 10% x1.2 | kontrola za 75 h (trend temp.) | OK
[   337.045] T=48.0°C | HI 97% zużycie 15% x1.4 | kontrola za 62 h (trend temp.) | OK
[   343.096] T=50.0°C | HI 95% zużycie 21% x1.6 | kontrola za 53 h (trend temp.) | OK
[   346.114] T=51.0°C | HI 94% zużycie 24% x1.8 | kontrola za 50 h (trend temp.) | OK
[   348.635] [ZDARZENIE #25] STAN -> PLAN (kontrola za 47 h)
[   351.152] T=53.0°C | HI 91% zużycie 30% x2.0 | kontrola za 44 h (trend temp.) | PLAN
[   363.238] T=58.0°C | HI 81% zużycie 48% x2.8 | kontrola za 32 h (trend temp.) | PLAN
```

MQTT: `{"seq":25,"ts":348635,"type":"LEVEL","param":"MAINT","level":"PLAN","value":46.51}`.

**Wnioski:** system wskazał potrzebę przeglądu przy ok. 52 °C – 8 °C przed progiem ostrzeżenia –
i podał, że wynika to z trendu temperatury. Próg 60 °C nie został przekroczony, więc nie było żadnego
alarmu parametru: to właśnie odróżnia predykcję od zwykłego progu.

---

## T5. Diagnoza wieloparametrowa – łożysko

| | |
|---|---|
| **Typ** | predykcyjny |
| **Cel** | analiza kilku parametrów jednocześnie (5.0): ta sama wartość drgań, różna diagnoza zależnie od temperatury |
| **Warunki początkowe** | maszyna pracuje, drgania +0,10 g (ostrzeżenie VIB, przyczyna `niewywazenie/luz` – zob. T3), wykonany `serwis` |
| **Kroki** | Temperatura rośnie o 1 °C co 2,5 s od 40 °C; polecenie `stan` przy 46 °C. |
| **Oczekiwany wynik** | po wykryciu rosnącego trendu temperatury przyczyna zmienia się na `lozysko/smarowanie`, choć temperatura jest daleko od progu |
| **Wynik** | ✅ zgodny z oczekiwanym |

```
[   272.871] [ZDARZENIE #21] SERVICE_DONE (37.29)
[   274.618] [ZDARZENIE #22] STAN -> PLAN (kontrola za 136 h)     <- ostrzeżenie drgań trwa
stan:  TEMP  46.00  poziom NORMA
       VIB    3.17  poziom OSTRZEZENIE
       Trend temp.: +0.346 C/h (R2 0.96) -> alarm za 72 h
       PROGNOZA: kontrola za 72 h (wg: trend temp.) | STAN: PLAN | przyczyna: lozysko/smarowanie
```

**Wnioski:** przy tych samych drganiach (3,17 mm/s) diagnoza zmieniła się z `niewywazenie/luz` (T3)
na `lozysko/smarowanie`, bo jednocześnie wiarygodnie (R² = 0,96) rośnie temperatura. To korelacja
dwóch parametrów, a nie dwa niezależne progi.

---

## T6. Przegląd okresowy i potwierdzenie serwisu

| | |
|---|---|
| **Typ** | standardowy; brzegowy (stan tuż po serwisie) |
| **Cel** | zużycie eksploatacyjne → PLAN / KONTROLA, potwierdzenie serwisu, brak fałszywego statusu po serwisie (5.0) |
| **Warunki początkowe** | maszyna pracuje, T = 40 °C, X = 0, wykonany `serwis` |
| **Kroki** | 1. `zuzycie 85`. 2. Po 4 s `zuzycie 100`. 3. Po 4 s `serwis`. |
| **Oczekiwany wynik** | PLAN po potwierdzeniu w 2 krokach; KONTROLA po 2 krokach; po serwisie od razu OK, zużycie ~0 %, prognoza ~200 h |
| **Wynik** | ✅ zgodny z oczekiwanym |

```
[   390.434] [KONSOLA] Ustawiono zużycie 85% (170.0 h równoważnych)
[   390.456] PRACA  | ... | HI 98% zużycie 6% x2.3 | kontrola za 81 h (zuzycie) | OK
[   391.624] [ZDARZENIE #27] STAN -> PLAN (kontrola za 14 h)
[   394.501] [KONSOLA] Ustawiono zużycie 100% (200.0 h równoważnych)
[   395.624] [ZDARZENIE #28] STAN -> KONTROLA (kontrola za 0 h)
[   396.490] PRACA  | ... | HI 43% zużycie 101% x1.7 | kontrola za TERAZ (zuzycie) | KONTROLA [ALARM - wymagane ACK]
[   398.594] [SERWIS] Potwierdzono (konsola): zużycie przed serwisem 102%, praca od poprzedniego 16.2 h
[   398.595] [ZDARZENIE #29] SERVICE_DONE (102.00)
[   399.508] PRACA  | ... | HI 100% zużycie 1% x1.0 | kontrola za 199 h (zuzycie) | OK
```

MQTT: zdarzenia `seq` 27–29 (`MAINT PLAN 13.54`, `MAINT KONTROLA 0.00`, `SERVICE_DONE 102.000`).

**Wnioski:** podwyższenie statusu wymaga 2 kroków (pierwsza linia po poleceniu jest jeszcze OK).
Zaraz po serwisie status to OK z prognozą 199 h. W wersji z Etapu 3 przez ok. 9 s pojawiał się w tym
miejscu fałszywy PLAN – poprawka jest teraz pokryta testem jednostkowym.

---

## T7. Usterka czujnika DHT22

| | |
|---|---|
| **Typ** | awaryjny (wstrzyknięcie usterki) |
| **Cel** | wykrycie usterki czujnika, praca w trybie ograniczonym, zgłoszenie zdarzenia (4.0, 4.5) |
| **Warunki początkowe** | z `diagram.json` usunięta linia `["dht1:SDA", "esp:16", …]` (przewód danych DHT22 → ESP32); nowa symulacja |
| **Kroki** | 1. Start symulacji. 2. Po 10 s polecenie `stan`, odczyt OLED. 3. Przywrócenie linii, ponowny start. |
| **Oczekiwany wynik** | po 3 nieudanych odczytach (≈ 7,5 s) `SENSOR_FAULT (2)`, status PLAN, przyczyna `usterka czujnika`; T i RH jako `---`; brak fałszywych alarmów temperatury i wilgotności; po naprawie poprawne odczyty |
| **Wynik** | ✅ zgodny z oczekiwanym |

```
[     1.574] [NVS] Brak zapisanych liczników – start od zera
[     6.623] [ZDARZENIE #2] SENSOR_FAULT (2.00)
[     8.590] [ZDARZENIE #3] STAN -> PLAN (kontrola za 200 h)
stan:  TEMP   ---  poziom NORMA
       HUM    ---  poziom NORMA
       Kondycja: 100% (temp ---, drgania 1.00, wilg. ---, zuzycie 1.00) | strefa ISO A
       PROGNOZA: kontrola za 200 h (wg: zuzycie) | STAN: PLAN | przyczyna: usterka czujnika
OLED:  POSTOJ PLAN / T ---  RH --- / V 0.00mm/s ISO A / HI 100% Zuz. 0% / Kontrola za 200 h
po przywróceniu przewodu:  POSTÓJ | T=40.0°C RH=45% | ... | OK
```

**Wnioski:** usterka wykryta po 3 odczytach (6,6 s od startu). Indeks kondycji liczony jest
z dostępnych parametrów, a niedostępne nie generują fałszywych alarmów. Przyczyna statusu jest jawnie
wskazana.

---

## T8. Historia, dziennik zdarzeń i pamięć nieulotna po restarcie

| | |
|---|---|
| **Typ** | brzegowy (utrata zasilania / restart sterownika) |
| **Cel** | rejestr historii pomiarów i zdarzeń (4.5), zachowanie liczników po restarcie |
| **Warunki początkowe** | symulacja po scenariuszach T1–T6 (ok. 430 s pracy) |
| **Kroki** | 1. `historia 6`. 2. `zdarzenia 8`. 3. `restart`. |
| **Oczekiwany wynik** | tabela ostatnich rekordów co 5 s i lista zdarzeń; po restarcie liczniki odtworzone z NVS (zdarzenie `RESTORED`), praca maszyny wykryta bez nowego uruchomienia |
| **Wynik** | ✅ zgodny z oczekiwanym (z jedną obserwacją – poniżej) |

```
HISTORIA (ostatnie 6 z 84 rekordow, co 5 s):
   czas[s]   mth[h]   T[C]  RH[%]  V[mm/s]  HI[%]  zuz[%]  praca  stan
       396    390.3   40.0     45     0.00     42     101    tak  KONTROLA (alarm niepotw.)
       401    395.4   40.0     45     0.00     99       1    tak  OK
       ...
ZDARZENIA (ostatnie 8 z 29):
  #22        274.617 s  STAN -> PLAN (kontrola za 136 h)
  ...
  #29        398.595 s  SERVICE_DONE (102.00)

[   431.768] PRACA  | ... | mth 425.7, starty 1 | MQTT online
[   432.450] [SYSTEM] Restart sterownika...
rst:0xc (SW_CPU_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
[   434.096] [NVS] Odtworzono: 426.4 mth, od serwisu 33.8 h, zużycie 34.0 h, starty 1, serwisy 7
[   434.098] [ZDARZENIE #2] RESTORED (426.41)
[   434.108] [RUN] Maszyna pracuje w chwili startu sterownika
[   434.201] PRACA  | T=---°C RH=---% | ...
[   437.156] PRACA  | T=40.0°C RH=45% | ... | HI 93% zużycie 19% x1.0 | kontrola za 163 h (zuzycie) | OK
```

**Wnioski:** historia obejmuje 84 rekordy (7 min), a dziennik – wszystkie zdarzenia z numerami. Po
restarcie odtworzono motogodziny, czas od serwisu, zużycie i liczniki. Uruchomienie trwającej pracy nie
zostało policzone jako nowy start. Obserwacja: pierwszy odczyt DHT22 po restarcie programowym się nie
udał – przez ok. 3 s widać `---`. Usterka nie została zgłoszona, bo wymaga 3 kolejnych błędów –
zgodnie z założeniem odporności na pojedyncze błędy.

---

## Weryfikacja uzupełniająca

Poza scenariuszami logikę predykcyjną sprawdzają testy jednostkowe (84 asercje, 0 błędów), a poprawność
obliczeń na platformie – monitor integralności FPU (0 błędów w 34 658 kontrolach). Szczegóły
i pomiar przed poprawką: [DOKUMENTACJA.md, rozdz. 7](DOKUMENTACJA.md#7-testy-i-wyniki).
