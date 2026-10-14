# RUNBOOK – projekt od zera, krok po kroku

> **Dla kogo:** dla osoby, która pierwszy raz otwiera to repozytorium i nie zna ESP32, Wokwi, MQTT
> ani języka C++. Niczego nie trzeba instalować – wystarczy przeglądarka internetowa.
>
> **Ile czasu:** uruchomienie ok. 15 minut; przeczytanie całości z ćwiczeniami ok. 3 godziny.
>
> **Jak czytać:** rozdziały 1–5 wystarczą, żeby uruchomić projekt i go obsługiwać. Rozdziały 6–10
> uczą czytać wszystko, co widać na ekranie. Rozdziały 11–13 tłumaczą kod – plik po pliku, funkcja
> po funkcji, z omówieniem kolejnych linijek. Na końcu: rozwiązywanie problemów i ściąga.

## Spis treści

1. [O co chodzi – w pięciu zdaniach](#1-o-co-chodzi--w-pięciu-zdaniach)
2. [Słowniczek](#2-słowniczek)
3. [Mapa repozytorium](#3-mapa-repozytorium)
4. [Uruchomienie krok po kroku](#4-uruchomienie-krok-po-kroku)
5. [Panel sterowania – każda kontrolka i dioda](#5-panel-sterowania--każda-kontrolka-i-dioda)
6. [Monitor portu szeregowego – jak czytać każdą linię](#6-monitor-portu-szeregowego--jak-czytać-każdą-linię)
7. [Konsola – każde polecenie](#7-konsola--każde-polecenie)
8. [MQTT – dane w Internecie](#8-mqtt--dane-w-internecie)
9. [Jak „myśli” system – algorytm po ludzku i z liczbami](#9-jak-myśli-system--algorytm-po-ludzku-i-z-liczbami)
10. [Ćwiczenia – osiem doświadczeń](#10-ćwiczenia--osiem-doświadczeń)
11. [Kurs przyspieszony: jak czytać kod](#11-kurs-przyspieszony-jak-czytać-kod)
12. [Przewodnik po kodzie – plik po pliku](#12-przewodnik-po-kodzie--plik-po-pliku)
13. [Jak samemu coś zmienić](#13-jak-samemu-coś-zmienić)
14. [Rozwiązywanie problemów](#14-rozwiązywanie-problemów)
15. [Ściąga](#15-ściąga)

---

## 1. O co chodzi – w pięciu zdaniach

W fabryce pracuje pompa. Jeśli zepsuje się nagle, staje cała linia produkcyjna – i to kosztuje
najwięcej. Ten projekt to „elektroniczny mechanik”, który przez cały czas **słucha** pompy (drgania),
**dotyka** jej obudowy (temperatura), sprawdza **powietrze** wokół (wilgotność) i **liczy godziny
pracy**. Z tych danych wylicza, **za ile godzin** pompę powinien obejrzeć człowiek, i mówi **dlaczego**
(np. „łożysko / smarowanie”). Wynik pokazuje lampkami jak sygnalizator (zielona – OK, żółta – zaplanuj
przegląd, czerwona – alarm), na małym ekranie, w tekstowym dzienniku i wysyła przez Internet (MQTT).

**Analogia z samochodem.** Licznik kilometrów to *motogodziny*. Lampka „check engine” to *alarm*.
Komputer, który mówi „wymiana oleju za 1200 km”, to *prognoza*. Ten projekt robi to samo dla maszyny
– i uwzględnia, że jazda „na wysokich obrotach” (gorąca, drgająca maszyna) zużywa ją szybciej.

**Co jest prawdziwe, a co symulowane.** Nie potrzebujesz żadnej elektroniki. Symulator **Wokwi**
w przeglądarce udaje płytkę ESP32 i czujniki, a to, że maszyna „się grzeje” albo „drga”, ustawiasz
suwakami. Program w środku jest prawdziwy – ten sam kod działałby na fizycznej płytce.

**Czas jest przyspieszony.** 1 sekunda symulacji = 1 godzina pracy maszyny. Dzięki temu w kilka minut
widać to, co w zakładzie trwa tygodnie. Pamiętaj o tym, gdy zobaczysz, że „zużycie” szybko rośnie.

---

## 2. Słowniczek

| Pojęcie | Co to znaczy po ludzku | Gdzie to zobaczysz |
|---|---|---|
| **mikrokontroler** | mały komputer w jednym układzie scalonym, który steruje urządzeniem | ESP32 na środku schematu |
| **ESP32** | popularny mikrokontroler z Wi-Fi i dwoma rdzeniami (dwa „procesory” w jednym) | schemat, `main.cpp` |
| **Wokwi** | symulator elektroniki w przeglądarce | https://wokwi.com |
| **czujnik** | element, który zamienia zjawisko fizyczne (temperaturę, ruch) na liczby | MPU6050, DHT22 |
| **MPU6050** | czujnik ruchu: akcelerometr (przyspieszenie) i żyroskop (obrót); tu – czujnik drgań | niebieska płytka u góry |
| **akcelerometr** | mierzy przyspieszenie; drgająca maszyna to szybko zmieniające się przyspieszenie | panel MPU6050: *Acceleration* |
| **g** | jednostka przyspieszenia: 1 g = przyspieszenie ziemskie (9,81 m/s²) | linia stanu: `a=0.100g` |
| **drgania, mm/s** | jak mocno maszyna „trzęsie się”; w przemyśle mierzy się prędkość drgań w milimetrach na sekundę | `v=3.17mm/s` |
| **RMS (wartość skuteczna)** | „średnia siła” sygnału, który ciągle się zmienia | `a_rms`, `v_rms` |
| **współczynnik szczytu (CF)** | ile razy największy skok jest większy od średniej; duże CF = pojedyncze uderzenia | `CF=1.00` |
| **ISO 10816, strefy A–D** | norma oceny drgań: A – nowa maszyna, B – dobra, C – do obserwacji, D – grozi uszkodzeniem | `ISO A`, OLED `ISO C` |
| **DHT22** | tani czujnik temperatury i wilgotności | biały czujnik w prawym dolnym rogu |
| **wilgotność względna (RH)** | ile pary wodnej jest w powietrzu (%); wysoka sprzyja korozji | `RH=45%` |
| **OLED** | mały wyświetlacz 128×64 punkty | prostokąt u góry po lewej |
| **LED** | dioda świecąca – lampka sygnalizacyjna | czerwona, żółta, zielona po lewej |
| **buzzer** | brzęczyk – wydaje dźwięki | czarny krążek po lewej na dole |
| **przełącznik RUN** | informuje, czy maszyna pracuje (w fabryce: styk stycznika silnika) | mały suwak pod przyciskiem SERWIS |
| **GPIO / pin** | „nóżka” mikrokontrolera, do której podłącza się elementy; ma numer, np. GPIO16 | `diagram.json`, tabela pinów |
| **I2C** | dwuprzewodowa „magistrala” do rozmowy z czujnikami (linie SDA – dane, SCL – zegar) | żółte i niebieskie przewody |
| **przerwanie (ISR)** | sygnał, który natychmiast przerywa program, żeby obsłużyć ważne zdarzenie (np. naciśnięcie przycisku) | `IRAM_ATTR` w kodzie |
| **FreeRTOS** | „mini-system operacyjny” – pozwala, by kilka części programu (zadań) działało jakby równocześnie | `xTaskCreatePinnedToCore` |
| **zadanie (task)** | osobna część programu z własną pętlą, np. pomiar, sterownik, komunikacja | rozdz. 11.3 |
| **rdzeń** | jeden z dwóch „procesorów” ESP32; zadania przypisuje się do rdzenia 0 lub 1 | rozdz. 12.2 |
| **FPU** | część procesora do liczb z przecinkiem (`float`) | monitor FPU w `[DIAG]` |
| **monitor portu szeregowego** | okienko z tekstem, który wypisuje program (dziennik) | panel pod symulacją |
| **konsola** | pole do wpisywania poleceń dla programu (na dole monitora) | `stan`, `serwis`… |
| **MQTT** | protokół wysyłania wiadomości przez Internet „przez pocztę” (broker) | rozdz. 8 |
| **broker** | serwer pośredniczący w MQTT („urząd pocztowy”) | `broker.emqx.io` |
| **temat (topic)** | „adres skrzynki” w MQTT | `wtp4/pdm/maszyna-01/telemetry` |
| **JSON** | zapis danych tekstem: `{"nazwa":wartość}` | telemetria MQTT |
| **telemetria** | regularnie wysyłane pomiary | co 5 s przez MQTT |
| **zdarzenie** | zapis, że coś się stało (start, alarm, serwis) | `[ZDARZENIE #5] …` |
| **próg ostrzeżenia / alarmu** | granica wartości, po której przekroczeniu system ostrzega / alarmuje | 60 °C / 70 °C |
| **histereza** | „bufor” przy powrocie: alarm 70 °C znika dopiero poniżej 68 °C (jak w termostacie) | rozdz. 9.3 |
| **zwłoka** | alarm włącza się dopiero, gdy przekroczenie trwa 2 kolejne pomiary (odporność na pojedyncze skoki) | rozdz. 9.3 |
| **kwitowanie (ACK)** | potwierdzenie przez operatora: „widzę ten alarm” | niebieski przycisk ACK |
| **ISA-18.2** | norma zarządzania alarmami w przemyśle (stany: niepotwierdzony, potwierdzony…) | czerwona lampka |
| **motogodziny** | łączny czas pracy maszyny w godzinach | `mth 39.1` |
| **zużycie, interwał przeglądu** | ile z 200 h „przebiegu” do przeglądu już zużyto (%) | `zużycie 75%` |
| **tempo zużycia** | ile godzin interwału zużywa godzina pracy (x1.0 = normalnie, x4 = cztery razy szybciej) | `x5.6` |
| **trend** | kierunek zmian: czy wartość rośnie i jak szybko | `trend temp.` |
| **R²** | jak dobrze punkty układają się w linię: 0 – chaos, 1 – idealna linia | `R2 0.96` |
| **prognoza** | ile godzin pracy zostało do kontroli | `kontrola za 47 h` |
| **indeks kondycji (HI)** | ogólna „ocena zdrowia” maszyny 0–100 % | `HI 85%` |
| **status OK / PLAN / KONTROLA** | ocena: wszystko dobrze / zaplanuj przegląd / wymagana kontrola | OLED, lampki |
| **NVS** | pamięć, która nie znika po wyłączeniu zasilania (jak pendrive w ESP32) | `[NVS] Odtworzono…` |
| **CRC** | suma kontrolna – wykrywa uszkodzone dane | rozdz. 12.2, sekcja 7 |
| **EWMA** | średnia „z pamięcią”: nowa wartość zmienia wynik tylko trochę (wygładzanie) | tempo zużycia, HI |
| **stałoprzecinkowy (Q8)** | liczby z przecinkiem zapisane jako całkowite pomnożone przez 256 | akwizycja drgań |
| **Doxygen** | sposób pisania komentarzy w kodzie (`/** @brief … */`), z których da się wygenerować dokumentację | cały kod |
| **test jednostkowy** | mały program sprawdzający, czy funkcja daje oczekiwany wynik | `test/test_predictive.cpp` |

---

## 3. Mapa repozytorium

| Plik / folder | Co to jest | Kiedy zajrzeć |
|---|---|---|
| `README.md` | stan projektu, szybka instrukcja, najważniejsze informacje | na początek |
| `RUNBOOK.md` | ten dokument – nauka od zera | gdy chcesz wszystko zrozumieć |
| `DOKUMENTACJA.md` | dokumentacja techniczna: architektura, wzory, diagramy, wyzwania | do zaliczenia, do pytań „dlaczego” |
| `TESTY.md` | 8 scenariuszy testowych z wynikami | żeby zobaczyć, co i jak sprawdzono |
| `PREZENTACJA.md` | scenariusz 10-minutowej prezentacji | przed wystąpieniem |
| `WERYFIKACJA.md` | zgodność projektu z wytycznymi i założeniami | żeby sprawdzić, czy niczego nie brakuje |
| `prezentacja/` | gotowe slajdy (`slajdy.pdf`, `.pptx`, `.html`) i ich źródło (`slajdy.md`) | na prezentację |
| `demo/` | skrypt automatycznej demonstracji i instrukcja nagrania wideo | plan B na prezentację |
| `main.cpp` | **program ESP32** (ok. 1930 linii): czujniki, przerwania, ekran, lampki, MQTT | rozdz. 12.2 |
| `predictive.h` | **„mózg”**: progi, trendy, zużycie, diagnoza, prognoza (ok. 890 linii) | rozdz. 12.3 |
| `sketch.ino` | pusty plik wymagany przez Wokwi (sam komentarz) | rozdz. 12.1 |
| `diagram.json` | schemat układu dla Wokwi: elementy i przewody | rozdz. 12.5 |
| `libraries.txt` | lista bibliotek, które Wokwi instaluje samo | rozdz. 12.6 |
| `test/test_predictive.cpp` | testy jednostkowe „mózgu” (84 sprawdzenia) | rozdz. 12.4 |
| `docs/schemat_wokwi.png` | obrazek schematu | rozdz. 5.1 |
| `LICENSE` | licencja MIT – na jakich zasadach można używać kodu | – |
| `.gitignore` | lista plików, których Git ma nie zapisywać | – |

---

## 4. Uruchomienie krok po kroku

Po każdym kroku jest „✅ Powinieneś zobaczyć” – jeśli widzisz coś innego, zajrzyj do
[rozdziału 14](#14-rozwiązywanie-problemów).

**Krok 0 – czego potrzebujesz.** Komputer z przeglądarką Chrome, Edge albo Firefox i dostęp do
Internetu. Pliki projektu: albo otwarte na GitHubie (przycisk *Raw* pokazuje czysty tekst pliku),
albo pobrane (*Code → Download ZIP* i rozpakowane).

**Krok 1 – otwórz Wokwi.** Wejdź na **https://wokwi.com/projects/new/esp32**.
✅ Po lewej edytor kodu z zakładkami `sketch.ino`, `diagram.json`, `Library Manager`; po prawej
obrazek płytki ESP32 i zielony przycisk ▶.

**Krok 2 – schemat (`diagram.json`).** Kliknij zakładkę `diagram.json`, kliknij w tekst, zaznacz
wszystko (`Cmd+A` na Macu, `Ctrl+A` na Windows/Linux) i wklej zawartość pliku `diagram.json`
z repozytorium (najpierw skopiuj ją: otwórz plik, zaznacz wszystko, `Cmd+C` / `Ctrl+C`).
✅ Po prawej pojawia się cały układ: ekran, czujniki, trzy lampki, buzzer, dwa przyciski,
przełącznik i czujnik DHT22 połączone kolorowymi przewodami (porównaj z rys. w rozdz. 5.1).

**Krok 3 – biblioteki (`libraries.txt`).** Obok zakładek jest mała strzałka **▾**. Kliknij ją →
**New file...** → w polu nazwy wpisz dokładnie `libraries.txt` → **Create** → wklej zawartość pliku.
Uwaga: okno potrafi mieć wpisaną nazwę innego pliku – najpierw wyczyść pole.
✅ Zakładka **Library Manager** pokazuje 5 bibliotek (PubSubClient, DHT sensor library for ESPx,
Adafruit SSD1306, Adafruit GFX Library, Adafruit BusIO).

**Krok 4 – program (`main.cpp`).** Tak samo: **▾** → **New file...** → `main.cpp` → wklej.
✅ Plik ma ok. 1930 linii i kończy się linią `void loop() { g_controller.tick(); }`.

**Krok 5 – „mózg” (`predictive.h`).** **▾** → **New file...** → `predictive.h` → wklej.
Nazwa musi być dokładnie taka, bo `main.cpp` dołącza ten plik poleceniem `#include "predictive.h"`.
✅ Plik zaczyna się komentarzem `@file predictive.h`.

**Krok 6 – pusty `sketch.ino`.** Kliknij zakładkę `sketch.ino`, zaznacz wszystko i wklej zawartość
`sketch.ino` z repozytorium (sam komentarz). Dlaczego: Wokwi wymaga pliku `.ino`, ale funkcje
`setup()` i `loop()` są już w `main.cpp` – gdyby zostały też w `sketch.ino`, kompilator zgłosiłby,
że są zdefiniowane dwa razy.
✅ W `sketch.ino` nie ma już słów `setup` ani `loop` poza komentarzem.

**Krok 7 – start.** Kliknij zielony **▶**. Najpierw trwa kompilacja (zamiana kodu na program dla
procesora) – na darmowym koncie 1–2 minuty, widać licznik „Compiling project...”.
✅ Zegar symulacji (prawy górny róg) zaczyna liczyć. Przez 1,5 s świecą wszystkie trzy lampki
i słychać krótki dźwięk (test lampek). Na ekranie OLED pojawia się stan maszyny. W monitorze portu
(panel pod schematem) przewija się dziennik startu.

**Krok 8 – sprawdź, czy wszystko działa.**
- [ ] w monitorze portu co sekundę pojawia się linia zaczynająca się od `POSTÓJ | T=40.0°C`,
- [ ] po kilku sekundach pojawia się `[MQTT] Połączono`,
- [ ] zielona lampka co 2 sekundy krótko błyska (maszyna stoi, wszystko OK),
- [ ] ekran OLED co 4 sekundy zmienia widok (stan → prognoza → wykres).

**Krok 9 (opcjonalnie) – zapis.** Zaloguj się w Wokwi i kliknij **SAVE** – projekt dostanie stały
link i nie trzeba będzie wklejać plików ponownie.

> **Zasada nr 1:** karta Wokwi musi być **widoczna**. Gdy przełączysz się na inną kartę albo
> zminimalizujesz okno, przeglądarka prawie zatrzymuje symulację.

---

## 5. Panel sterowania – każda kontrolka i dioda

### 5.1 Mapa schematu

![Schemat układu w Wokwi](docs/schemat_wokwi.png)

| Nr | Element | Gdzie na schemacie | Co udaje w fabryce | Jak go używać |
|---|---|---|---|---|
| 1 | **ESP32** | środek | sterownik maszyny (szafa sterownicza) | nic – to „mózg” |
| 2 | **OLED** | u góry po lewej | panel operatora | tylko patrzysz (rozdz. 5.6) |
| 3 | **MPU6050** | u góry po prawej (niebieska płytka) | czujnik drgań przykręcony do łożyska | klik → suwaki (rozdz. 5.5) |
| 4–6 | **lampki czerwona, żółta, zielona** | po lewej, od góry | kolumna sygnalizacyjna nad maszyną | tylko patrzysz (rozdz. 5.2) |
| 7 | **rezystory 220 Ω** | obok lampek | ograniczają prąd diod (żeby się nie spaliły) | – |
| 8 | **buzzer** | po lewej na dole (czarny krążek) | syrena alarmowa | tylko słuchasz (rozdz. 5.3) |
| 9 | **ACK** (niebieski przycisk) | po prawej | przycisk „potwierdzam alarm” | klik albo klawisz **A** |
| 10 | **SERWIS** (czarny przycisk) | pod ACK | przycisk serwisanta „przegląd wykonany” | przytrzymaj 3 s albo klawisz **S** |
| 11 | **przełącznik RUN** | mały suwak pod SERWIS | sygnał „silnik pracuje” | klik: w prawo = praca, w lewo = postój |
| 12 | **DHT22** | po prawej na dole (biały) | czujnik temperatury obudowy i wilgotności | klik → suwaki (rozdz. 5.5) |
| 13 | **rezystor 10 kΩ** | pod DHT22 | „podciąga” linię danych czujnika do 3,3 V – wymaganie DHT22 | – |

**Kolory przewodów:** czerwony – zasilanie 3,3 V; czarny – masa (GND, „minus”); żółty i niebieski –
magistrale I2C (zegar SCL i dane SDA); zielony – przyciski i przełącznik; fioletowy – lampki i buzzer;
pomarańczowy – dane czujnika DHT22.

### 5.2 Lampki – co znaczy każdy sposób świecenia

| Lampka | Jak świeci | Co to znaczy | Co zrobić |
|---|---|---|---|
| wszystkie trzy | świecą 1,5 s zaraz po starcie | **test lampek** – sprawdzenie, czy żadna nie jest przepalona | nic |
| 🟢 zielona | świeci ciągle | status **OK**, maszyna **pracuje** | nic |
| 🟢 zielona | krótki błysk co 2 s („bicie serca”) | status **OK**, maszyna **stoi** | nic |
| 🟢 zielona | zgaszona | status nie jest OK (jest PLAN albo KONTROLA) | patrz żółta i czerwona |
| 🟡 żółta | świeci ciągle | status **PLAN** – zaplanuj przegląd (ostrzeżenie, zużycie ≥ 80 %, prognoza ≤ 48 h, usterka czujnika, HI < 70 %) | sprawdź przyczynę (`stan`, ekran prognozy) |
| 🔴 czerwona | miga szybko (4 razy na sekundę) + „pip-pip” | **alarm aktywny, niepotwierdzony** | naciśnij **ACK** |
| 🔴 czerwona | świeci ciągle | alarm **potwierdzony**, ale nadal aktywny | usuń przyczynę (np. obniż temperaturę) |
| 🔴 czerwona | miga wolno (raz na sekundę) | alarm **ustąpił**, ale nikt go nie potwierdził | naciśnij **ACK** |
| 🔴 czerwona | zgaszona | brak alarmów | – |

Status **KONTROLA** zawsze zapala czerwoną: sam stan „wymagana kontrola” jest traktowany jak alarm
(kanał MAINT), więc trzeba go potwierdzić przyciskiem ACK.

### 5.3 Dźwięki buzzera

| Kiedy | Dźwięk |
|---|---|
| start sterownika | jeden ton (2 kHz, 0,2 s) |
| alarm niepotwierdzony | „pip-pip” (2 × 0,09 s, 2,4 kHz) co 2 s – aż do ACK |
| naciśnięcie ACK | krótki ton (2,5 kHz) |
| wciśnięcie SERWIS | bardzo krótkie „tyk” – uzbrojenie potwierdzenia |
| SERWIS przytrzymany 3 s | podwójny ton – serwis potwierdzony |

Wokwi gra dźwięk buzzera w przeglądarce (głośność ustawiona nisko). Jeśli nic nie słychać, kliknij
raz w stronę – przeglądarki blokują dźwięk do pierwszego kliknięcia.

### 5.4 Przyciski i przełącznik

**ACK (niebieski).** Potwierdza wszystkie aktualne alarmy (wszystkie „kanały”). Działa też klawisz
**A** (najpierw kliknij w puste miejsce schematu), polecenie konsoli `ack` i polecenie MQTT `ack`.
W dzienniku: `[ALARM] Kwitowanie (przycisk): potwierdzono 2 kanał(ów)` i zdarzenie `ACK (2.00)`.
ACK nie usuwa przyczyny – czerwona przestaje migać i milknie sygnał, ale świeci, dopóki problem trwa.

**SERWIS (czarny).** Oznacza „przegląd wykonany”. Wciśnięcie uzbraja (dziennik:
`[SERWIS] Przytrzymaj 3 s, aby potwierdzić wykonanie serwisu`), przytrzymanie przez 3 s potwierdza,
puszczenie wcześniej anuluje (ochrona przed przypadkowym naciśnięciem). Potwierdzenie zeruje zużycie,
okna trendów i licznik „od serwisu”, a status wraca do OK. Klawisz **S** (trzymaj) i polecenie
konsoli `serwis` działają tak samo. **Przez MQTT nie da się** potwierdzić serwisu – celowo, bo
zerowanie licznika powinno wymagać obecności przy maszynie.

**Przełącznik RUN.** Suwak w **prawo** = maszyna pracuje, w **lewo** = stoi. Zmiana daje zdarzenie
`RUN_START` (z numerem uruchomienia) albo `RUN_STOP` (z czasem cyklu w godzinach). Ważne skutki:
motogodziny i zużycie rosną **tylko w pracy**; trendy zbierają próbki **tylko w pracy**; na postoju
program „uczy się”, gdzie jest dół (grawitacja), a w pracy zamraża to ustawienie – dlatego drgania
symuluje się przy pracującej maszynie.

### 5.5 Panele czujników (suwaki)

Kliknij czujnik na schemacie – nad schematem otworzy się panel z suwakami. Jeśli otwarty jest panel
innego czujnika, pierwsze kliknięcie go zamknie – kliknij drugi raz.

**DHT22** (temperatura i wilgotność):

| Suwak | Zakres | Co się stanie |
|---|---|---|
| *Temperature* | −40…80 °C, krok 0,1 | ≥ 60 °C → ostrzeżenie `TEMP` i PLAN; ≥ 70 °C → alarm `TEMP` i KONTROLA; powrót poniżej 58 / 68 °C (histereza 2 °C). Każde +10 °C powyżej 40 °C **podwaja** tempo zużycia. |
| *Humidity* | 0…100 %, krok 0,5 | ≥ 80 % → ostrzeżenie `HUM` i PLAN (przyczyna wilgoć/korozja); ≥ 90 % → alarm; histereza 3 %. |

Program czyta DHT22 co 2,5 s, więc zmiana pojawia się w dzienniku z opóźnieniem do 3 s.

**MPU6050** (drgania):

| Suwak | Zakres | Znaczenie w projekcie |
|---|---|---|
| *Acceleration X / Y / Z* | −2…2 g, krok 0,05 g | **to są drgania**: odchylenie od położenia spoczynkowego. Z = 1 g na starcie to grawitacja (czujnik leży poziomo). |
| *Rotation X / Y / Z* | °/s | nieużywane w analizie |
| *Temperature* | °C | temperatura wnętrza czujnika – tylko w telemetrii (`mpu_t`) |

Jak symulować drgania: **przy pracującej maszynie** przesuń *Acceleration X* o kilka kroków. Program
traktuje stałe odchylenie jako drgania o tej amplitudzie (1 g ≈ 31,7 mm/s):

| X | prędkość drgań | strefa ISO 10816 | skutek |
|---|---|---|---|
| +0,05 g | ≈ 1,6 mm/s | B | OK |
| +0,10 g | ≈ 3,2 mm/s | C | ostrzeżenie `VIB`, PLAN, przyczyna niewyważenie/luz |
| +0,20 g | ≈ 6,3 mm/s | C | ostrzeżenie, zużycie rośnie ok. 12 razy szybciej |
| +0,25 g | ≈ 7,9 mm/s | D | alarm `VIB`, KONTROLA |

Na postoju to samo przesunięcie po kilku sekundach „znika” – program uznaje je za przechylenie
czujnika, a nie drgania.

### 5.6 Ekran OLED – trzy widoki co 4 sekundy

**Widok 1 – stan maszyny**
```
PRACA  OK       W M      ← praca/postój · status · W = Wi-Fi połączone · M = MQTT połączone ("-" = brak)
───────────────────
T  40.0C   RH  45%       ← temperatura i wilgotność ("T  ---" = usterka czujnika)
V  0.00mm/s  ISO A       ← prędkość drgań i strefa ISO 10816
HI 100%   Zuz.   0%      ← indeks kondycji i zużycie (% interwału przeglądu)
Mth    0.0h  St 0        ← motogodziny i liczba uruchomień
Kontrola za 200 h        ← prognoza ("TERAZ" = już teraz, ">9999 h" = brak prognozy)
```

**Widok 2 – prognoza**
```
PROGNOZA [OK]            ← status w nawiasie
───────────────────
Kontrola za 200 h        ← prognozowany czas pracy do kontroli
Wg: zuzycie              ← co wyznaczyło prognozę: zuzycie / trend temp. / trend drgan
Przyczyna:
praca prawidlowa         ← diagnoza (rozdz. 9.6)
dT +0.00C/h R2 0.00      ← trend temperatury: zmiana na godzinę pracy i wiarygodność R²
dV +0.00/h  R2 0.00      ← trend drgań (mm/s na godzinę) i jego R²
```

**Widok 3 – trend kondycji**
```
Kondycja 100%  10min     ← aktualny indeks kondycji; okno 10 minut
. . . . . . . . . . . .  ← linia 70 % (poniżej – PLAN)
. . . . . . . . . . . .  ← linia 40 % (poniżej – KONTROLA)
            ‾‾‾‾‾‾‾‾‾‾‾  ← wykres HI: jeden punkt co 5 s, najnowsze po prawej
```

---

## 6. Monitor portu szeregowego – jak czytać każdą linię

Monitor portu to panel tekstowy pod schematem: wszystko, co program „mówi”. Każda linia programu
zaczyna się znacznikiem czasu `[  176.614]` – to sekundy (z dokładnością do milisekund) od startu
sterownika. Przy czasie przyspieszonym 1 s = 1 h pracy, więc znacznik czasu to też mniej więcej
godzina pracy maszyny (jeśli pracowała od startu).

### 6.1 Start – linia po linii

```
rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
... (kilka linii load:..., entry ...)
[     1.493] ==================================================
[     1.493]  WTP4 - Inteligentny system utrzymania ruchu maszyn
[     1.494]  WTP4-PdM 0.4.0 (Etap 4) | arduino-esp32 3.3.7 | CPU 240 MHz
[     1.504]  Czas maszyny przyspieszony x3600 (1 s = 1.0 h pracy)
[     1.570] [HMI] OLED: OK (I2C1)
[     1.570] [ZDARZENIE #1] BOOT (0.00)
[     1.574] [NVS] Brak zapisanych liczników – start od zera
[     1.574] [KONSOLA] Wpisz "pomoc", aby zobaczyć listę poleceń
[     1.587] [AKW] MPU6050: OK (±4 g, ±500 °/s, DLPF 44 Hz)
[     1.589] [SYSTEM] Gotowy: próbkowanie 100 Hz, okno drgań 100 próbek, analiza co 1 s
[     1.637] POSTÓJ | T=40.0°C RH=45% | ... | kontrola za >9999 h (-) | OK  | mth 0.0, starty 0 | MQTT offline
[     1.884] [SIEĆ] Łączenie z WiFi "Wokwi-GUEST"...
[     3.185] [SIEĆ] WiFi połączone, IP 10.10.0.2, RSSI -80 dBm
[     3.185] [MQTT] Łączenie z broker.emqx.io:1883 jako wtp4-pdm-5ca9eea3...
[     3.689] [MQTT] Połączono. Telemetria: wtp4/pdm/maszyna-01/telemetry
```

| Linia | Znaczenie |
|---|---|
| `rst:0x1 (POWERON_RESET)` i `load:…` | komunikaty wbudowanego programu startowego ESP32 (przed naszym kodem); `SW_CPU_RESET` = restart programowy |
| ramka `=====` z nazwą i wersją | nasz program wystartował: wersja oprogramowania, wersja bibliotek ESP32, taktowanie 240 MHz |
| `Czas maszyny przyspieszony x3600` | przypomnienie: 1 s = 1 h pracy |
| `[HMI] OLED: OK (I2C1)` | wyświetlacz odpowiedział na drugiej magistrali I2C (`BRAK ODPOWIEDZI` = problem z OLED) |
| `[ZDARZENIE #1] BOOT (0.00)` | pierwsze zdarzenie w dzienniku: start sterownika |
| `[NVS] Brak zapisanych liczników – start od zera` | w pamięci nieulotnej nic nie ma (nowa symulacja); po restarcie programowym zobaczysz `[NVS] Odtworzono: …` |
| `[AKW] MPU6050: OK (…)` | czujnik drgań odpowiada i jest skonfigurowany (zakres ±4 g, filtr 44 Hz) |
| `[SYSTEM] Gotowy: …` | wszystkie zadania działają: 100 pomiarów drgań na sekundę, analiza co 1 s |
| pierwsza linia stanu z `>9999 h (-)` | jeszcze przed pierwszą analizą – prognozy nie ma; od następnej sekundy pojawi się „200 h” |
| `[SIEĆ] …` | łączenie z siecią Wi-Fi symulatora; RSSI to siła sygnału (im bliżej 0, tym lepiej) |
| `[MQTT] Łączenie … / Połączono` | połączenie z serwerem MQTT; od teraz dane idą do Internetu |

### 6.2 Linia stanu (co 1 sekundę) – pole po polu

```
[  40.727] PRACA  | T=72.0°C RH=45% | v=0.00mm/s ISO A (a=0.000g CF=0.00) | HI 52% zużycie 75% x5.6 | kontrola za 9 h (zuzycie) | KONTROLA [ALARM - wymagane ACK] | mth 39.1, starty 0 | MQTT online
```

| Fragment | Znaczenie |
|---|---|
| `[  40.727]` | czas od startu sterownika [s] |
| `PRACA` / `POSTÓJ` | stan przełącznika RUN |
| `T=72.0°C RH=45%` | temperatura i wilgotność z DHT22; `---` = brak odczytu |
| `v=0.00mm/s` | prędkość drgań (wartość skuteczna z ostatniej sekundy) |
| `ISO A` | strefa drgań wg ISO 10816 (A < 1,12; B < 2,8; C < 7,1; D ≥ 7,1 mm/s) |
| `(a=0.000g CF=0.00)` | przyspieszenie drgań (g) i współczynnik szczytu |
| `HI 52%` | indeks kondycji |
| `zużycie 75%` | ile z 200-godzinnego interwału przeglądu już zużyto (może przekroczyć 100 %) |
| `x5.6` | tempo zużycia: godzina pracy zużywa teraz 5,6 h interwału |
| `kontrola za 9 h (zuzycie)` | prognoza i jej źródło: `zuzycie`, `trend temp.`, `trend drgan` (`-` = brak) |
| `KONTROLA` | status: `OK`, `PLAN` albo `KONTROLA` |
| `[ALARM - wymagane ACK]` | jest co najmniej jeden niepotwierdzony alarm (aktywny albo już ustąpiony) |
| `mth 39.1, starty 0` | motogodziny i liczba uruchomień |
| `MQTT online` | czy jest połączenie z brokerem MQTT |

### 6.3 Zdarzenia `[ZDARZENIE #n]`

Każde zdarzenie ma kolejny numer `#n`, trafia do dziennika w pamięci (ostatnie 32) i przez MQTT.

| Zdarzenie | Przykład | Kiedy | Liczba w nawiasie |
|---|---|---|---|
| `BOOT` | `BOOT (0.00)` | start sterownika | zawsze 0 |
| `RESTORED` | `RESTORED (426.41)` | po starcie odczytano liczniki z NVS | motogodziny |
| `RUN_START` | `RUN_START (1.00)` | przełącznik RUN → praca | numer uruchomienia |
| `RUN_STOP` | `RUN_STOP (148.79)` | przełącznik RUN → postój | czas cyklu pracy [h] |
| `ACK` | `ACK (2.00)` | kwitowanie | liczba potwierdzonych kanałów |
| `SERVICE_DONE` | `SERVICE_DONE (102.00)` | potwierdzony serwis | zużycie przed serwisem [%] |
| `SENSOR_FAULT` | `SENSOR_FAULT (2.00)` | usterka czujnika | 1 = MPU6050, 2 = DHT22 |
| `SENSOR_OK` | `SENSOR_OK (2.00)` | czujnik znów działa | jw. |
| zmiana poziomu parametru | `TEMP -> ALARM (71.00)` | parametr zmienił poziom: `NORMA`, `OSTRZEZENIE`, `ALARM` | wartość parametru (°C, mm/s, %) |
| zmiana statusu | `STAN -> PLAN (kontrola za 47 h)` | status utrzymania: `OK`, `PLAN`, `KONTROLA` | prognoza w chwili zmiany |

Nazwy parametrów: `TEMP` – temperatura, `VIB` – drgania, `HUM` – wilgotność, `STAN` (w MQTT `MAINT`)
– status utrzymania.

### 6.4 Diagnostyka `[DIAG]` (co 30 s)

```
[ 390.061] [DIAG] sterta 198464 B (min 193304 B) | wolny stos: acq 2900 B, comms 3136 B, loop 5740 B | próbki 38800, pominięte takty 1 | zbocza ACK=0 SERWIS=0 RUN=177
[ 390.073] [DIAG] historia 78/120, zdarzenia 26/32 | MQTT wysłane N, utracone 0 | FPU: błędy 0 / kontrole 34658
```
(`N` – liczba wysłanych wiadomości, zależna od tego, jak długo było połączenie z brokerem.)

| Pole | Znaczenie | Normalnie |
|---|---|---|
| `sterta 198464 B (min …)` | wolna pamięć RAM teraz i najmniej w historii | ok. 190–200 KB, nie maleje |
| `wolny stos: acq / comms / loop` | zapas pamięci stosu każdego zadania | > 1 KB (0 = przepełnienie) |
| `próbki` | liczba pomiarów drgań od startu | ≈ 100 × sekundy |
| `pominięte takty` | ile razy pomiar się spóźnił o cały okres | 0–1 (1 przy starcie) |
| `zbocza ACK/SERWIS/RUN` | ile przerwań wygenerowały przyciski (drgania styków!) | dziesiątki na jedno kliknięcie – to normalne |
| `historia 78/120` | rekordy historii / pojemność | rośnie do 120 |
| `zdarzenia 29/32` | zdarzenia w dzienniku / pojemność | do 32 (najstarsze są nadpisywane) |
| `MQTT wysłane / utracone` | wiadomości wysłane / zdarzenia, które nie zmieściły się w kolejce | utracone = 0 |
| `FPU: błędy / kontrole` | samokontrola obliczeń zmiennoprzecinkowych (rozdz. 12.2, sekcja 3) | błędy = 0 |

### 6.5 Pozostałe komunikaty

| Komunikat | Znaczenie |
|---|---|
| `[SERWIS] Przytrzymaj 3 s, aby potwierdzić…` | wciśnięto SERWIS – trzymaj dalej |
| `[SERWIS] Potwierdzono (przycisk/konsola): zużycie przed serwisem X%, praca od poprzedniego Y h` | serwis przyjęty |
| `[ALARM] Kwitowanie (przycisk/konsola/MQTT): potwierdzono N kanał(ów)` | ACK z podanego źródła |
| `[KONSOLA] Ustawiono zużycie X% (…)` | wynik polecenia `zuzycie` |
| `[KONSOLA] Nieznane polecenie "…"` | literówka w poleceniu – wpisz `pomoc` |
| `[NVS] Odtworzono: …` | po restarcie odczytano liczniki z pamięci nieulotnej |
| `[NVS] Zapisano liczniki` / `[NVS] Błąd zapisu` | wynik `zapisz` / problem z pamięcią flash |
| `[SYSTEM] Restart sterownika...` | polecenie `restart` |
| `[RUN] Maszyna pracuje w chwili startu sterownika` | RUN był włączony podczas startu – nie liczy się jako nowe uruchomienie |
| `[SIEĆ] Utracono WiFi` | brak sieci – pomiary i lampki działają dalej, MQTT czeka |
| `[MQTT] Błąd połączenia (stan N), ponowienie za X ms` | nieudane połączenie; kolejne próby co 1, 2, 4… do 30 s, na zmianę z brokerem zapasowym. Kody: −4 przekroczony czas, −3 utracone połączenie, −2 nie udało się połączyć, −1 rozłączono, 1–5 broker odmówił |
| `[MQTT] Polecenie: "ping"` | przyszło polecenie z Internetu |
| `[BŁĄD] …` | problem przy starcie (brak pamięci na kolejki, nie wystartowało zadanie) – nie powinien wystąpić |

---

## 7. Konsola – każde polecenie

Polecenia wpisuje się w pole tekstowe **na dole monitora portu** i zatwierdza **Enterem**. Wielkość
liter nie ma znaczenia.

| Polecenie | Co robi |
|---|---|
| `pomoc` (albo `help`) | wypisuje listę poleceń |
| `stan` | pełny raport (niżej) |
| `historia` / `historia 6` | ostatnie 10 / 6 rekordów historii (maks. 120) |
| `zdarzenia` / `zdarzenia 8` | ostatnie 10 / 8 zdarzeń (maks. 32) |
| `ack` | kwitowanie alarmów (jak przycisk ACK) |
| `serwis` | potwierdzenie serwisu (jak przytrzymanie SERWIS) |
| `zuzycie 85` | ustawia zużycie na 85 % (0–150 %) – do testów i demonstracji |
| `zapisz` | natychmiast zapisuje liczniki do NVS |
| `restart` | zapisuje liczniki i restartuje sterownik – test pamięci nieulotnej |

**Raport `stan` – linia po linii**
```
================ RAPORT STANU MASZYNY ================
Praca: TAK | motogodziny 66.6 h | od serwisu 42.7 h | starty 0 | serwisy 1 | alarmy 2
  TEMP      40.00  poziom NORMA       alarm -            progi 60.0 / 70.0
  VIB        7.93  poziom ALARM       alarm NIEPOTWIERDZ. progi 2.8 / 7.1
  HUM       45.00  poziom NORMA       alarm -            progi 80.0 / 90.0
Kondycja: 30% (temp 1.00, drgania 0.20, wilg. 1.00, zuzycie 0.00) | strefa ISO D
Zuzycie: 455.8 h rownowaznych = 228% interwalu 200 h | tempo x19.79
Trend temp.: +0.000 C/h (R2 0.00) -> alarm za >9999 h | trend drgan: +0.273 mm/s/h (R2 0.75) -> alarm za >9999 h
PROGNOZA: kontrola za TERAZ (wg: zuzycie) | STAN: KONTROLA | przyczyna: niewywazenie/luz
======================================================
```

| Linia | Jak czytać |
|---|---|
| `Praca: …` | pracuje?; motogodziny; godziny od ostatniego serwisu; uruchomienia; serwisy; alarmy od początku |
| `TEMP / VIB / HUM` | dla każdego parametru: wartość, **poziom** (NORMA/OSTRZEZENIE/ALARM), **stan alarmu** (`-` brak, `NIEPOTWIERDZ.`, `POTWIERDZONY`, `USTAPIL-NIEP.`) i progi ostrzeżenie/alarm |
| `Kondycja: 30% (…)` | indeks kondycji i oceny cząstkowe 0–1 (1 = bardzo dobrze, 0 = krytycznie; `---` = czujnik niedostępny) oraz strefa ISO |
| `Zuzycie: …` | zużycie w godzinach równoważnych, w % interwału i bieżące tempo |
| `Trend temp. … trend drgan …` | nachylenie trendu (jednostka na godzinę pracy), R² i czas do progu alarmu według trendu (`>9999 h` = trend niewiarygodny, płaski albo próg już przekroczony) |
| `PROGNOZA: …` | najkrótszy z czasów, jego źródło, status i przyczyna |

**`historia 6`**
```
HISTORIA (ostatnie 6 z 84 rekordow, co 5 s):
   czas[s]   mth[h]   T[C]  RH[%]  V[mm/s]  HI[%]  zuz[%]  praca  stan
       396    390.3   40.0     45     0.00     42     101    tak  KONTROLA (alarm niepotw.)
```
Kolumny: czas od startu; motogodziny; temperatura; wilgotność; drgania; indeks kondycji; zużycie;
czy pracowała; status (+ informacja o niepotwierdzonym alarmie).

**`zdarzenia 8`**
```
ZDARZENIA (ostatnie 8 z 29):
  #22        274.617 s  STAN -> PLAN (kontrola za 136 h)
```
Kolumny: numer zdarzenia; czas od startu; opis (jak w tabeli 6.3).

**`restart`** – po restarcie zobaczysz `rst:0xc (SW_CPU_RESET)`, `[NVS] Odtworzono: …` i zdarzenie
`RESTORED`: liczniki przetrwały. Uwaga: przycisk Stop/Start w Wokwi to „nowe urządzenie” – pamięć
flash jest czyszczona, więc wtedy liczniki startują od zera.

---

## 8. MQTT – dane w Internecie

**Analogia z pocztą.** Broker (`broker.emqx.io`) to urząd pocztowy. *Temat* to adres skrzynki.
Urządzenie *publikuje* (wysyła list) do tematu, a każdy, kto *zasubskrybował* temat, dostaje kopię.
Wiadomość *retained* czeka w skrzynce na nowych subskrybentów. *LWT* („ostatnia wola”) to list, który
urząd wyśle sam, gdy urządzenie nagle zniknie – u nas `offline`.

**Jak podejrzeć dane (MQTTX Web, bez instalacji):**
1. Wejdź na https://mqttx.app/web-client → **New Connection**.
2. *Name* – dowolna; *Host* – `wss://` + `broker.emqx.io`; *Port* – `8084`; *Path* – `/mqtt` → **Connect**.
3. **New Subscription** → *Topic*: `wtp4/pdm/maszyna-01/#` (`#` = wszystkie tematy pod tym adresem) → **Confirm**.
4. Co 5 s przyjdzie telemetria; zdarzenia – na bieżąco.
5. Polecenie: na dole ustaw temat `wtp4/pdm/maszyna-01/cmd`, treść `ping` → wyślij; w temacie
   `event` przyjdzie `{"type":"PONG"}`.

W terminalu (z zainstalowanym Mosquitto): `mosquitto_sub -h broker.emqx.io -t 'wtp4/pdm/maszyna-01/#' -v`.

| Temat | Kierunek | Treść |
|---|---|---|
| `…/status` | urządzenie → | `online` / `offline` (retained) |
| `…/telemetry` | urządzenie → | JSON co 5 s (tabela niżej) |
| `…/event` | urządzenie → | JSON przy każdym zdarzeniu |
| `…/cmd` | → urządzenie | `ping` (odpowiedź PONG), `status` (natychmiastowa telemetria), `ack` (kwitowanie) |

**Pola telemetrii (37):**

| Pole | Znaczenie | Pole | Znaczenie |
|---|---|---|---|
| `seq` | numer migawki pomiarowej | `ts` | czas migawki [ms od startu] |
| `run` | 1 = pracuje | `t`, `rh` | temperatura [°C], wilgotność [%] (`null` = brak) |
| `dht_ok` | 1 = DHT22 sprawny | `a_rms`, `a_peak` | przyspieszenie drgań skuteczne i szczytowe [g] |
| `cf` | współczynnik szczytu | `v_rms` | prędkość drgań [mm/s] |
| `zone` | strefa ISO (A–D) | `ax`, `ay`, `az` | ostatnia próbka przyspieszenia [g] |
| `mpu_t` | temperatura wnętrza MPU6050 [°C] | `mpu_ok` | 1 = MPU6050 sprawny |
| `run_h` | motogodziny | `svc_h` | godziny od serwisu |
| `starts` | liczba uruchomień | `hi` | indeks kondycji [%] |
| `wear` | zużycie [% interwału] | `eq_h` | zużycie [h równoważne] |
| `rate` | tempo zużycia | `state` | `OK` / `PLAN` / `KONTROLA` |
| `cause` | przyczyna: `NONE`, `SENSOR`, `BEARING`, `THERMAL`, `MECHANICAL`, `ENVIRONMENT`, `WEAR` | `insp_h` | prognoza [h] (`null` = brak) |
| `drv` | źródło prognozy | `t_tr`, `t_r2` | trend temperatury [°C/h] i jego R² |
| `v_tr`, `v_r2` | trend drgań [mm/s/h] i jego R² | `alarms` | maska kanałów w alarmie: 1 TEMP, 2 VIB, 4 HUM, 8 MAINT, 16 SENSOR (sumują się) |
| `unack` | 1 = jest niepotwierdzony alarm | `faults` | maska usterek: 1 MPU6050, 2 DHT22 |
| `ovr` | pominięte takty pomiaru | `rssi` | siła sygnału Wi-Fi [dBm] |
| `heap` | wolna pamięć RAM [B] | | |

**Zdarzenie (przykład):** `{"seq":13,"ts":181631,"type":"LEVEL","param":"TEMP","level":"ALARM","value":71.00}`
– numer, czas, typ, parametr (`TEMP`, `VIB`, `HUM`, `MAINT`), nowy poziom i wartość.

> Broker jest publiczny – każdy może czytać te tematy. To w porządku dla symulacji; w fabryce używa się
> prywatnego brokera z hasłem i szyfrowaniem (TLS).

---

## 9. Jak „myśli” system – algorytm po ludzku i z liczbami

Co sekundę (= co godzinę pracy) „mózg” (`predictive.h`) wykonuje 7 kroków.

### 9.1 Zużycie – „licznik przebiegu, który biegnie szybciej przy ostrej jeździe”

Każda godzina pracy zużywa `k` godzin z 200-godzinnego interwału przeglądu:
- **temperatura:** `k_T = 2^((T − 40) / 10)` – każde +10 °C powyżej 40 °C podwaja zużycie
  (smar i izolacja starzeją się szybciej; reguła 10 °C),
- **drgania:** `k_V = (v / 2,8)³` powyżej 2,8 mm/s – łożyska zużywają się z trzecią potęgą obciążenia
  (norma ISO 281, trwałość L10),
- `k = k_T · k_V`; każdy rozruch dolicza 0,5 h.

**Przykład:** T = 60 °C, v = 3,5 mm/s → `k_T = 2² = 4`, `k_V = (3,5/2,8)³ ≈ 1,95`, `k ≈ 7,8`. Godzina
pracy „kosztuje” 7,8 h interwału – przegląd będzie potrzebny prawie 8 razy szybciej.
Czas do przeglądu = (200 h − zużyte h) / tempo.

### 9.2 Trendy – „patrzę, jak szybko rośnie termometr”

Z 60 ostatnich pomiarów (60 h pracy) program rysuje najlepiej dopasowaną prostą (regresja liniowa)
i liczy **nachylenie** oraz **R²** (czy punkty leżą na prostej). Jeśli R² ≥ 0,6, jest co najmniej
15 pomiarów i temperatura rośnie co najmniej 0,2 °C/h, trend uznaje się za wiarygodny i liczy czas
do progu alarmu.

**Przykład:** temperatura 52 °C rośnie 0,4 °C/h → do 70 °C zostało (70 − 52) / 0,4 = **45 h** → to
mniej niż 48 h, więc status PLAN – zanim temperatura przekroczy jakikolwiek próg.

### 9.3 Progi i alarmy

| Parametr | Ostrzeżenie | Alarm | Powrót (histereza) |
|---|---|---|---|
| temperatura | ≥ 60 °C | ≥ 70 °C | 2 °C niżej |
| drgania | ≥ 2,8 mm/s (strefa C) | ≥ 7,1 mm/s (strefa D) | 0,3 mm/s niżej |
| wilgotność | ≥ 80 % | ≥ 90 % | 3 % niżej |

- **Zwłoka:** podwyższenie poziomu wymaga **2 kolejnych pomiarów** powyżej progu – pojedynczy skok nie
  wywoła alarmu.
- **Histereza:** jak w termostacie – alarm 70 °C znika dopiero poniżej 68 °C, żeby nie „migotał”, gdy
  temperatura krąży wokół 70.
- **Stany alarmu (ISA-18.2):**

| Stan | Czerwona lampka | Przejście |
|---|---|---|
| brak alarmu | zgaszona | przekroczenie progu alarmu → *niepotwierdzony* |
| niepotwierdzony | miga szybko + dźwięk | ACK → *potwierdzony*; spadek poniżej progu → *ustąpił, niepotwierdzony* |
| potwierdzony | świeci | spadek poniżej progu → brak alarmu |
| ustąpił, niepotwierdzony | miga wolno | ACK → brak alarmu |

### 9.4 Indeks kondycji HI – „ocena zdrowia jak średnia ocen z wagami”

Każdy parametr dostaje ocenę 0–1 (1 – bardzo dobrze):
- temperatura: do 50 °C → 1; 60 °C → 0,6; 70 °C → 0,2; 80 °C → 0,
- drgania: do 1,12 mm/s → 1; 2,8 → 0,7; 7,1 → 0,25; 11,2 → 0,
- wilgotność: do 70 % → 1; 80 % → 0,6; 90 % → 0,2; 100 % → 0,
- zużycie: 1 − (zużyte / 200 h).

Wagi: drgania 35 %, temperatura 30 %, zużycie 25 %, wilgotność 10 %. Wynik nie może być wyższy niż
**najgorsza ocena + 0,3** – jeden bardzo zły parametr nie „schowa się” w średniej.

**Przykład:** T = 65 °C (ocena 0,4), v = 1 mm/s (1,0), RH = 45 % (1,0), zużycie 50 % (0,5):
średnia ważona = 0,3·0,4 + 0,35·1 + 0,1·1 + 0,25·0,5 = 0,695; ograniczenie 0,4 + 0,3 = 0,7 →
**HI ≈ 70 %** – na granicy PLAN.

### 9.5 Prognoza – „liczy się najbliższe zagrożenie”

Prognoza = **najkrótszy** z trzech czasów: do przeglądu wg zużycia, do alarmu temperatury wg trendu,
do alarmu drgań wg trendu. Obok wartości zapisuje się, co ją wyznaczyło (`zuzycie`, `trend temp.`,
`trend drgan`).

### 9.6 Diagnoza – „co jest prawdopodobną przyczyną”

| Kolejność | Objawy | Przyczyna |
|---|---|---|
| 1 | usterka czujnika | usterka czujnika |
| 2 | drgania ≥ ostrzeżenia **i** temperatura ≥ ostrzeżenia lub rośnie | łożysko / smarowanie |
| 3 | temperatura ≥ ostrzeżenia | przegrzewanie |
| 4 | drgania ≥ ostrzeżenia | niewyważenie / luz |
| 5–7 | to samo, ale tylko z trendów (czas do alarmu ≤ 48 h) | łożysko / przegrzewanie / niewyważenie |
| 8 | wilgotność ≥ ostrzeżenia | wilgoć / korozja |
| 9 | zużycie ≥ 80 % lub przegląd za ≤ 48 h | przegląd okresowy |

Dlaczego tak: drgania razem z rosnącą temperaturą to typowy objaw łożyska (tarcie grzeje); same
drgania przy stałej temperaturze to raczej niewyważenie albo luz; sama temperatura – chłodzenie albo
przeciążenie.

### 9.7 Status – OK / PLAN / KONTROLA

| Status | Wystarczy jeden warunek |
|---|---|
| **KONTROLA** | alarm parametru · zużycie ≥ 100 % · prognoza ≤ 8 h · HI < 40 % |
| **PLAN** | ostrzeżenie parametru · usterka czujnika · zużycie ≥ 80 % · prognoza ≤ 48 h · HI < 70 % |
| **OK** | żaden z powyższych |

Status rośnie dopiero, gdy warunek utrzyma się **2 kolejne sekundy**, a maleje dopiero po **5**
sekundach – dzięki temu nie skacze tam i z powrotem.

### 9.8 Dlaczego czasem „KONTROLA” pojawia się bez żadnego alarmu

Bo przy wysokiej temperaturze i drganiach zużycie biegnie kilka razy szybciej, a czas jest
przyspieszony. Po minucie pracy w 55 °C z drganiami 3 mm/s prognoza spada do zera – system żąda
kontroli, choć żaden parametr nie przekroczył progu alarmu. To nie błąd, tylko istota predykcji.

---

## 10. Ćwiczenia – osiem doświadczeń

Każde ćwiczenie: **zrób → obserwuj → zrozum**. Wyniki z naszych testów są w [TESTY.md](TESTY.md).
Przed każdym ćwiczeniem dobrze jest wpisać `serwis` (czysty start) i ustawić 40 °C oraz X = 0.

1. **Praca i postój.** Przełącz RUN w prawo, poczekaj 10 s, w lewo. → `RUN_START (1.00)`, motogodziny
   rosną o 1 h/s; `RUN_STOP` z czasem cyklu; na postoju licznik stoi. → Czas pracy liczy się tylko
   w pracy; jedno kliknięcie = jedno zdarzenie, choć przycisk „drga” (`[DIAG] zbocza RUN=…`).
2. **Progi temperatury.** Ustaw 61 °C, potem 71 °C, naciśnij ACK, wróć do 40 °C. → ostrzeżenie →
   alarm → potwierdzenie → powrót. → Progi, zwłoka, stany ISA-18.2 (rozdz. 9.3).
3. **Strefy drgań.** Przy pracy: X = 0,05; 0,10; 0,25 g; wpisz `stan`. → strefy B, C, D, przyczyna
   niewyważenie/luz. → Przeliczenie g → mm/s i progi ISO 10816.
4. **Predykcja z trendu.** `serwis`, potem podnoś temperaturę o 1 °C co 2–3 s od 40 °C (albo skrypt
   rampy z README). → prognoza `(trend temp.)` maleje, PLAN ok. 52 °C. → Ostrzeżenie przed progiem.
5. **Łożysko.** Drgania 0,10 g, potem rampa temperatury, `stan`. → przyczyna zmienia się na
   łożysko/smarowanie. → Korelacja kilku parametrów.
6. **Przegląd okresowy.** `zuzycie 85`, potem `zuzycie 100`, potem `serwis`. → PLAN, KONTROLA, OK.
   → Model zużycia i potwierdzanie zmian statusu.
7. **Usterka czujnika.** Usuń z `diagram.json` linię `["dht1:SDA", "esp:16", …]`, uruchom symulację
   ponownie, wpisz `stan`; potem przywróć linię. → `SENSOR_FAULT (2.00)`, `---` zamiast temperatury.
   → Wykrywanie usterek i praca w trybie ograniczonym.
8. **Pamięć nieulotna.** `historia 6`, `zdarzenia 8`, `restart`. → `[NVS] Odtworzono…`. → Historia
   i liczniki przetrwały restart.

---

## 11. Kurs przyspieszony: jak czytać kod

Ten rozdział to „alfabet”: każda konstrukcja, która pojawia się w `main.cpp` i `predictive.h`,
z przykładem z projektu. Z nim przeczytasz każdą linijkę.

### 11.1 Komentarze

```cpp
// komentarz do końca linii
/* komentarz w kilku liniach */
/** @brief Krótki opis funkcji. @param x co to za argument @return co funkcja zwraca */
float aRmsG = 0.0f;  ///< opis pola, napisany po prawej stronie
```
`/** … */` i `///<` to komentarze **Doxygen** – opisują, *po co* jest dany fragment. Zawsze czytaj je
pierwsze: mówią więcej niż sam kod.

### 11.2 Typy danych

| Typ | Co przechowuje | Przykład z projektu |
|---|---|---|
| `bool` | prawda/fałsz (`true`/`false`) | `bool running = false;` |
| `char` | jeden znak, np. `'A'` | `char zone = 'A';` |
| `uint8_t` | liczba całkowita 0…255 (1 bajt) | `uint8_t faults` |
| `int16_t` | −32 768…32 767 (2 bajty) | surowy odczyt czujnika `int16_t acc[3]` |
| `uint16_t` | 0…65 535 | `uint16_t count` |
| `uint32_t` / `int32_t` | 0…4,29 mld / ±2,1 mld (4 bajty) | czas w ms `uint32_t nowMs` |
| `uint64_t` / `int64_t` | bardzo duże liczby całkowite (8 bajtów) | czas w µs, sumy okna drgań |
| `size_t` | rozmiar / indeks w tablicy | `size_t count()` |
| `float` | liczba z przecinkiem, ok. 7 cyfr | `float tempC` |
| `double` | liczba z przecinkiem, ok. 15 cyfr | motogodziny `double runTotalH` |

`0.0f` – litera `f` oznacza stałą typu `float`. `NAN` („not a number”) oznacza „brak wartości”,
`INFINITY` – nieskończoność (u nas: „brak prognozy”). `u` po liczbie (`1000000UL`) – liczba bez znaku.

### 11.3 Stałe, zmienne i słowa kluczowe

| Zapis | Znaczenie |
|---|---|
| `const` | wartość, której nie wolno zmienić (`const uint8_t pin_`) |
| `constexpr` | stała liczona już w czasie kompilacji (`constexpr uint32_t kSampleRateHz = 100;`) |
| `static` | w klasie: wspólne dla wszystkich obiektów; poza klasą: widoczne tylko w tym pliku |
| `volatile` | „nie optymalizuj – wartość może się zmienić niespodziewanie” (monitor FPU) |
| `auto` | kompilator sam dobierze typ |
| `nullptr` | „wskaźnik donikąd” |
| `k…` (np. `kTimeScale`) | konwencja nazw: **stałe** zaczynają się od `k` |
| `…_` (np. `pin_`) | konwencja nazw: **pola klasy** kończą się podkreśleniem |

### 11.4 Struktury i klasy

```cpp
struct Event {             // struktura = paczka danych
  uint32_t seq;
  float value;
};

class DebouncedInput {     // klasa = dane + funkcje (metody)
 public:                   // dostępne z zewnątrz
  DebouncedInput(uint8_t pin, const char* name) : pin_(pin), name_(name) {}  // konstruktor
  bool active() const { return stable_; }   // metoda; "const" = nie zmienia obiektu
 private:                  // dostępne tylko wewnątrz klasy
  const uint8_t pin_;
  bool stable_ = false;    // wartość początkowa
};
```
`: pin_(pin), name_(name)` to **lista inicjalizacyjna** – ustawia pola przy tworzeniu obiektu.
Obiekt tworzy się jak zmienną: `static MachineController g_controller;`, a metodę woła kropką:
`g_controller.tick();`.

### 11.5 Referencje, wskaźniki, `.` i `->`

| Zapis | Znaczenie |
|---|---|
| `const SystemSnapshot& s` | **referencja** – „inna nazwa” tego samego obiektu, bez kopiowania |
| `const MqttLink* link_` | **wskaźnik** – adres obiektu w pamięci |
| `obiekt.metoda()` | wywołanie na obiekcie |
| `wskaznik->metoda()` | wywołanie przez wskaźnik (`link_->wifiUp()`) |
| `this` | wskaźnik na „ten” obiekt, w którym jesteśmy |

### 11.6 Warunki, pętle, operatory

```cpp
if (warunek) { … } else { … }
for (uint8_t i = 0; i < 3; ++i) { … }   // i = 0, 1, 2
switch (pattern) { case Pattern::On: return true; default: return false; }
x = warunek ? a : b;                     // jeśli warunek, to a, w przeciwnym razie b
```

| Operator | Znaczenie | Przykład |
|---|---|---|
| `&&`, `\|\|`, `!` | i, lub, nie | `if (!ok_) return;` |
| `%` | reszta z dzielenia | `(nowMs / 4000) % 3` → 0, 1, 2 – numer ekranu |
| `<<`, `>>` | przesunięcie bitów (×2, ÷2) | `(raw[i] << 8) \| raw[i + 1]` – sklejenie dwóch bajtów |
| `\|`, `&`, `^` | operacje na bitach: OR, AND, XOR | `faults & kFaultDht` – „czy bit DHT ustawiony?” |
| `++x`, `x += 2` | zwiększ o 1, zwiększ o 2 | `++starts_` |

### 11.7 `enum class`, `namespace`, `::`

```cpp
enum class Level : uint8_t { kNormal = 0, kWarning = 1, kAlarm = 2 };  // lista nazwanych wartości
namespace cfg { constexpr uint32_t kSampleRateHz = 100; }              // "szuflada" na nazwy
cfg::kSampleRateHz    pdm::Level::kAlarm    DebouncedInput::onEdge     // :: = "z szuflady / z klasy"
```
W projekcie są trzy „szuflady”: `cfg` (konfiguracja), `rtos` (obiekty FreeRTOS), `pdm`
(predictive maintenance – cały „mózg” w `predictive.h`).

### 11.8 Szablony, lambdy, rzutowania, preprocesor

| Zapis | Znaczenie | Przykład |
|---|---|---|
| `RingBuffer<HistoryRecord, 120>` | **szablon**: ten sam kod dla różnych typów i rozmiarów | bufor 120 rekordów historii |
| `[&raw](uint8_t i) { … }` | **lambda** – mała funkcja w miejscu użycia | `be16` w `Mpu6050::read` |
| `static_cast<float>(x)` | **rzutowanie** – zamiana typu | liczba całkowita → float |
| `reinterpret_cast<const uint8_t*>(&c)` | „potraktuj te bajty jako tablicę bajtów” | liczenie CRC struktury |
| `#include "predictive.h"` | dołącz inny plik | początek `main.cpp` |
| `#pragma once` | dołącz ten plik tylko raz | początek `predictive.h` |
| `#if ESP_ARDUINO_VERSION_MAJOR >= 3` … `#endif` | kod kompilowany tylko dla danej wersji | `SampleTimer::begin` |
| `static_assert(sizeof(Event) == 16, "…")` | sprawdzenie w czasie kompilacji | rozmiary struktur |

### 11.9 Arduino – funkcje, które spotkasz

| Funkcja | Co robi |
|---|---|
| `setup()` | wykonuje się raz po starcie |
| `loop()` | wykonuje się w kółko |
| `pinMode(pin, INPUT_PULLUP / OUTPUT)` | ustawia nóżkę jako wejście z rezystorem podciągającym / wyjście |
| `digitalRead(pin)`, `digitalWrite(pin, HIGH/LOW)` | odczyt / ustawienie stanu nóżki |
| `millis()`, `micros()` | czas od startu w milisekundach / mikrosekundach |
| `Serial.write`, `Serial.read`, `Serial.available` | wysyłanie i odbiór tekstu przez port szeregowy (monitor) |
| `Wire.beginTransmission / write / endTransmission / requestFrom / read` | rozmowa przez I2C |
| `tone(pin, Hz)`, `noTone(pin)` | dźwięk buzzera on/off |
| `attachInterruptArg(pin, funkcja, arg, CHANGE)` | wywołaj funkcję przy każdej zmianie stanu nóżki |

### 11.10 FreeRTOS – wielozadaniowość

| Funkcja | Co robi | W projekcie |
|---|---|---|
| `xTaskCreatePinnedToCore(funkcja, "nazwa", stos, arg, priorytet, &uchwyt, rdzeń)` | tworzy zadanie na wskazanym rdzeniu | akwizycja (rdzeń 1, priorytet 3), komunikacja (rdzeń 0) |
| `ulTaskNotifyTake(pdTRUE, czas)` | „śpij, aż ktoś mnie obudzi albo minie czas”; zwraca, ile razy budzono | początek pętli zadań |
| `vTaskNotifyGiveFromISR(zadanie, &woken)` | obudź zadanie z przerwania | ISR timera, ISR przycisków |
| `portYIELD_FROM_ISR(woken)` | jeśli obudzone zadanie jest ważniejsze – przełącz od razu | koniec ISR |
| `xQueueCreate(n, rozmiar)` | kolejka na n elementów | skrzynki i kolejki |
| `xQueueOverwrite` / `xQueuePeek` | „skrzynka”: wpisz najnowsze (nadpisz) / podejrzyj bez zabierania | migawki stanu |
| `xQueueSend` / `xQueueReceive` | kolejka FIFO: dodaj na koniec / zabierz z początku | zdarzenia, polecenia |
| `pdMS_TO_TICKS(ms)` | zamiana ms na „tyknięcia” zegara systemu | czasy oczekiwania |
| `esp_task_wdt_add`, `esp_task_wdt_reset`, `enableLoopWDT` | **watchdog** – restartuje układ, gdy zadanie się zawiesi | akwizycja, `loop()` |
| `IRAM_ATTR` | umieść funkcję w szybkiej pamięci RAM – wymagane dla przerwań | ISR |
| `std::atomic<T>` (`load`, `store`, `exchange`, `fetch_add`) | zmienna bezpieczna przy dostępie z przerwania i zadania | liczniki zboczy, stan RUN |

### 11.11 Formaty `printf` / `snprintf`

| Format | Znaczenie | Przykład wyniku |
|---|---|---|
| `%d`, `%u`, `%lu` | liczba całkowita (ze znakiem / bez / długa bez znaku) | `42` |
| `%.1f`, `%.2f` | liczba z przecinkiem z 1 / 2 miejscami | `40.0`, `3.17` |
| `%+.3f` | ze znakiem `+` / `−` | `+0.346` |
| `%5.1f` | szerokość 5 znaków | ` 40.0` |
| `%s`, `%-6s` | tekst; wyrównany do lewej na 6 znakach | `PRACA ` |
| `%c` | jeden znak | `A` |
| `%%` | znak procentu | `%` |
| `%.*f` | liczba miejsc po przecinku podana jako argument | `formatValue` |

### 11.12 Podróż jednego pomiaru – od czujnika do Internetu

1. **Timer** co 10 ms wywołuje przerwanie `SampleTimer::onAlarm` – ono tylko budzi zadanie akwizycji.
2. **Zadanie akwizycji** (`AcquisitionService::run`) czyta 14 bajtów z MPU6050 (`Mpu6050::read`)
   i dodaje próbkę do sumy okna (`VibrationAccumulator::addSample`) – na liczbach całkowitych.
3. Po 100 próbkach (1 s) zadanie wkłada sumy okna do skrzynki (`xQueueOverwrite`) i budzi sterownik.
4. **Sterownik** (`MachineController::handleSensors`) wyjmuje migawkę, liczy RMS i mm/s
   (`computeVibrationFeatures`) i woła `analyze()`.
5. `analyze()` podaje pomiary **silnikowi** (`pdm::PredictiveEngine::step`): zużycie, trendy, progi,
   kondycja, prognoza, diagnoza, status.
6. Zmiany poziomów stają się **zdarzeniami** (`push`): wpis do dziennika, linia `[ZDARZENIE #n]`,
   kolejka do MQTT.
7. Sterownik odświeża **lampki** (`refreshIndicators`), **ekran** (`render`), **linię stanu**
   (`printStatus`) i co 5 s **historię** (`recordHistory`).
8. **Zadanie komunikacji** (`MqttLink::run`) zabiera zdarzenie z kolejki i publikuje je; co 5 s
   podgląda stan systemu i wysyła **telemetrię** JSON do brokera.

---

## 12. Przewodnik po kodzie – plik po pliku

Numery linii dotyczą wersji 0.4.0 z repozytorium (mogą się przesunąć po zmianach – szukaj wtedy po
nazwie funkcji, np. `Cmd+F` → `void tick`).

### 12.0 Indeks – gdzie co jest

| Co | Plik : linia |
|---|---|
| konfiguracja `cfg` (wszystkie stałe) | `main.cpp:60` |
| typy danych (`VibrationFeatures` … `HistoryRecord`) | `main.cpp:151–268` |
| nazwy zdarzeń `toString`, obiekty `rtos` | `main.cpp:274`, `main.cpp:290` |
| `logMsg`, `printLine`, `formatValue`, `jsonNum`, `formatForecast` | `main.cpp:310–368` |
| `FpuIntegrityMonitor` | `main.cpp:379` |
| `DebouncedInput` (przyciski, RUN) + ISR | `main.cpp:416`, ISR `472–483` |
| `SampleTimer` (timer 100 Hz) + ISR | `main.cpp:489`, ISR `522` |
| `Mpu6050` (sterownik czujnika drgań) | `main.cpp:537` |
| `ClimateSensor` (DHT22) | `main.cpp:615` |
| `VibrationAccumulator`, `computeVibrationFeatures` | `main.cpp:656`, `main.cpp:702` |
| `RunHourMeter` (motogodziny) | `main.cpp:721` |
| `PersistedCounters`, `CounterStore` (NVS) | `main.cpp:787`, `main.cpp:811` |
| `Indicators` (lampki, buzzer) | `main.cpp:858` |
| `StatusDisplay` (OLED) | `main.cpp:947` |
| `MqttLink` (Wi-Fi, MQTT) | `main.cpp:1061` |
| `AcquisitionService` (zadanie akwizycji) | `main.cpp:1258` |
| `MachineController` (sterownik) | `main.cpp:1341` – `begin` 1352, `tick` 1379, `handleInputs` 1425, `analyze` 1535, `execute` 1711, `printStatus` 1837 |
| obiekty globalne, `setup()`, `loop()` | `main.cpp:1901–1930` |
| `clampf`, `minf`, `interpolate` | `predictive.h:33–55` |
| `RingBuffer` | `predictive.h:67` |
| `LinearTrend` (trendy) | `predictive.h:119` |
| `Level`, `AlarmState`, `Thresholds`, `AlarmChannel` | `predictive.h:245–342` |
| `WearConfig`, `WearModel` (zużycie) | `predictive.h:349`, `predictive.h:374` |
| `Health`, `computeHealth`, `isoZone` | `predictive.h:448–500` |
| `Param`, `Cause`, `Driver`, `EngineConfig`, `EngineInput`, `EngineOutput`, `LevelChange` | `predictive.h:506–580` |
| `PredictiveEngine` – `step` 615, `serviceDone` 724, `diagnose` 786, `rawStatus` 805 | `predictive.h:595` |
| nazwy tekstowe `paramCode` … `driverText` | `predictive.h:837–890` |

### 12.1 `sketch.ino`

Sam komentarz. Arduino i Wokwi wymagają pliku `.ino`, ale kompilator dołącza też wszystkie pliki
`.cpp` z katalogu – a `setup()` i `loop()` są w `main.cpp`. Pusty `.ino` = brak podwójnej definicji.

### 12.2 `main.cpp` – sekcja po sekcji

**Nagłówek pliku (linie 1–36).** Komentarz `@file` opisuje: który wymóg oceny realizuje który
fragment, rysunek architektury (kto z kim rozmawia) i zasady (brak `delay()`, brak alokacji pamięci
w pętli, krótkie przerwania, jeden użytkownik FPU na rdzeń).

**Dołączane pliki (linie 38–55).**

| `#include` | Po co |
|---|---|
| `<Arduino.h>` | podstawowe funkcje Arduino |
| `<Wire.h>` | magistrala I2C (MPU6050, OLED) |
| `<WiFi.h>` | sieć Wi-Fi |
| `<Preferences.h>` | pamięć nieulotna NVS |
| `<PubSubClient.h>` | klient MQTT |
| `<DHTesp.h>` | czujnik DHT22 |
| `<Adafruit_GFX.h>`, `<Adafruit_SSD1306.h>` | grafika i sterownik OLED |
| `<esp_task_wdt.h>`, `<esp_timer.h>` | watchdog zadań, 64-bitowy zegar µs |
| `<atomic>`, `<ctype.h>`, `<math.h>`, `<stdarg.h>`, `<stdlib.h>`, `<string.h>` | biblioteki standardowe: zmienne atomowe, znaki, matematyka, zmienna liczba argumentów, konwersje, teksty |
| `"predictive.h"` | nasz „mózg” |

**Sekcja 1 – konfiguracja `cfg` (linie 56–143).** Wszystkie „pokrętła” w jednym miejscu.

| Stała | Wartość | Znaczenie |
|---|---|---|
| `kFirmware` | `"WTP4-PdM 0.4.0 (Etap 4)"` | wersja w dzienniku startu |
| `kPinI2c0Sda/Scl` | 21 / 22 | magistrala czujnika drgań |
| `kPinI2c1Sda/Scl` | 33 / 32 | magistrala wyświetlacza |
| `kPinDht` | 16 | dane DHT22 (GPIO4 w Wokwi nie działa) |
| `kPinBtnAck`, `kPinBtnService`, `kPinRun` | 19, 18, 17 | przyciski i przełącznik |
| `kPinLedRed/Yellow/Green` | 25, 26, 27 | lampki |
| `kPinBuzzer` | 13 | buzzer |
| `kI2cClockHz` | 400 000 | szybkość I2C (400 kHz) |
| `kI2cTimeoutMs` | 10 | po ilu ms przerwać zawieszoną transmisję I2C |
| `kMpuAddress`, `kOledAddress` | 0x68, 0x3C | adresy urządzeń na I2C |
| `kSampleRateHz` | 100 | ile razy na sekundę mierzyć drgania |
| `kWindowSamples` | 100 | długość okna drgań (1 s) |
| `kDhtPeriodMs` | 2500 | odczyt DHT22 co 2,5 s |
| `kDhtFaultStreak` | 3 | po ilu błędach z rzędu DHT22 jest „uszkodzony” |
| `kMpuFaultStreak` | 10 | to samo dla MPU6050 |
| `kMpuRetryMs` | 1000 | co ile próbować ożywić MPU6050 |
| `kGravityTauS` | 2 s | jak szybko na postoju „uczyć się” grawitacji |
| `kDominantFreqHz` | 49,2 Hz | częstotliwość obrotów pompy (2950 obr/min) – do przeliczenia g → mm/s |
| `kStandardGravity` | 9,80665 | 1 g w m/s² |
| `kTimeScale` | 3600 | przyspieszenie czasu (1 s = 1 h) |
| `kAnalysisPeriodS`, `kStepHours` | 1 s, 1 h | krok analizy w sekundach i w godzinach maszyny |
| `kHistoryPeriodMs`, `kHistorySize` | 5000, 120 | historia: rekord co 5 s, 120 rekordów |
| `kEventLogSize` | 32 | ile zdarzeń pamiętać |
| `kPersistPeriodMs` | 60 000 | zapis liczników do NVS co minutę |
| `kNvsNamespace` | `"wtp4pdm"` | nazwa „katalogu” w NVS |
| `kDebounceUs` | 30 000 | 30 ms ciszy = koniec drgań styków |
| `kServiceHoldMs` | 3000 | jak długo trzymać SERWIS |
| `kControllerTickMs` | 10 | sterownik budzi się najpóźniej co 10 ms |
| `kDisplayPeriodMs`, `kScreenPeriodMs` | 500, 4000 | odświeżanie OLED, zmiana ekranu |
| `kStatusPeriodMs`, `kDiagPeriodMs` | 1000, 30 000 | linia stanu co 1 s, diagnostyka co 30 s |
| `kLampTestMs` | 1500 | test lampek po starcie |
| `kAlarmBeepPeriodMs` | 2000 | „pip-pip” co 2 s |
| `kWifiSsid`, `kWifiPass`, `kWifiChannel` | `Wokwi-GUEST`, puste, 6 | sieć symulatora |
| `kWifiRetryMs` | 15 000 | ponowienie Wi-Fi |
| `kMqttHosts`, `kMqttPort` | emqx, mosquitto; 1883 | brokery MQTT (główny, zapasowy) |
| `kTopicBase` | `wtp4/pdm/maszyna-01` | początek nazw tematów |
| `kTelemetryPeriodMs`, `kStatusRefreshMs` | 5000, 30 000 | telemetria co 5 s, „online” co 30 s |
| `kMqttBackoffMinMs/MaxMs` | 1000 / 30 000 | odstępy ponowień MQTT (podwajane) |
| `kMqttBufferSize`, `kMqttKeepAliveS` | 768, 30 | bufor wiadomości, „podtrzymanie” połączenia |
| `kAcqStack`, `kAcqPriority` | 4096 B, 3 | stos i priorytet zadania akwizycji |
| `kCommsStack`, `kCommsPriority` | 6144 B, 1 | stos i priorytet zadania komunikacji |
| `kEventQueueLen`, `kCommandQueueLen` | 16, 4 | długości kolejek |

**Sekcja 2 – typy danych (linie 145–299).**
- `VibrationFeatures` – wyniki jednego okna drgań: `aRmsG`, `aPeakG`, `crest`, `vRmsMmS`.
- `ClimateReading` – odczyt DHT22: `tempC`, `humidityPct`, `valid` (czy jest poprawny odczyt),
  `errorStreak` (ile błędów z rzędu).
- `VibrationWindow` – **liczby całkowite** z zadania akwizycji: `sumSq` (suma kwadratów),
  `peakSq` (największy kwadrat), `count` (liczba próbek).
- `AcqSnapshot` – „paczka” z akwizycji do sterownika (tylko liczby całkowite): numery, okno drgań,
  ostatnia surowa próbka, stan MPU6050, liczniki próbek i pominiętych taktów.
- `SensorSnapshot` – te same dane, ale **w jednostkach fizycznych** (g, °C, mm/s) – liczy je sterownik.
- `FaultBit` – maska usterek: 1 = MPU6050, 2 = DHT22.
- `SystemSnapshot` – **cały stan systemu** dla wyświetlacza i MQTT: czujniki, praca, liczniki, wynik
  analizy (status, przyczyna, prognoza, HI, zużycie, trendy, alarmy).
- `EventType` + `Event` – typy zdarzeń i jedno zdarzenie (16 bajtów: numer, czas, typ, parametr,
  poziom, wartość).
- `HistoryRecord` – rekord historii w 16 bajtach: liczby zapisane jako całkowite (temperatura ×10,
  drgania ×100, motogodziny ×10), a pole `flags` mieści trzy informacje w bitach: bit 0 – praca,
  bity 1–2 – status, bit 3 – niepotwierdzony alarm. `static_assert` pilnuje rozmiaru 16 B.
- `RemoteCommand` – polecenia z MQTT do sterownika (na razie tylko `Ack`).
- `toString()` – zamienia typ zdarzenia na tekst (`RUN_START` itd.).
- `namespace rtos` – uchwyty kolejek i zadań FreeRTOS oraz `machineRunning` (stan RUN dla akwizycji).

**Sekcja 3 – narzędzia (linie 301–403).**

`logMsg` – wypisanie linii z czasem:
```cpp
char line[256];                                    // bufor na stosie – bez alokacji pamięci
const uint32_t ms = millis();                      // czas od startu
int n = snprintf(line, sizeof(line), "[%6lu.%03lu] ", ms / 1000UL, ms % 1000UL);  // "[  12.345] "
va_start(args, fmt); vsnprintf(line + n, …, fmt, args); va_end(args);            // dopisz treść
line[n++] = '\n';                                  // koniec linii
Serial.write(line, n);                             // jeden zapis = linie z różnych zadań się nie mieszają
```
`printLine` – to samo bez czasu (raporty konsoli). `formatValue` – liczba albo `---` dla NaN.
`jsonNum` – liczba albo `null` (JSON nie zna NaN). `formatForecast` – `TERAZ` dla ≤ 0,5 h,
`>9999 h` dla braku prognozy, w przeciwnym razie np. `47 h`.

`FpuIntegrityMonitor::check` – samokontrola obliczeń:
```cpp
const float v = seed_;                              // 0.25 (volatile – kompilator nie "oszuka")
float s0 = 0, s1 = 0, s2 = 0, s3 = 0;               // cztery sumy w rejestrach FPU
for (i = 0; i < 1024; ++i) { s0 += v; s1 += v*2; s2 += v*4; s3 += v*8; }
++checks_;
if (s0 != 256 || s1 != 512 || s2 != 1024 || s3 != 2048) ++errors_;  // wynik musi być dokładny
```
Jeśli inne zadanie popsułoby rejestry FPU w trakcie pętli, suma wyszłaby inna – licznik błędów to
pokazuje w `[DIAG]`. Szczegóły problemu: DOKUMENTACJA.md, rozdz. 8.2.

**Sekcja 4 – przerwania (linie 405–526).**

`DebouncedInput` – przycisk lub przełącznik z eliminacją drgań styków:
```cpp
void begin() {
  pinMode(pin_, INPUT_PULLUP);       // wejście z rezystorem do 3,3 V: puszczony = 1, wciśnięty = 0
  stable_ = readActive();            // stan początkowy
  attachInterruptArg(…, &DebouncedInput::isrThunk, this, CHANGE);  // przerwanie przy każdej zmianie
}
bool update(nowUs, nowMs) {          // woła sterownik w każdym takcie
  if (pending_ == 0) return false;                         // nie było zboczy
  if (nowUs - lastEdgeUs_ < kDebounceUs) return false;     // styki jeszcze drgają – czekaj 30 ms ciszy
  pending_ = 0;
  const bool level = readActive();                         // stan po uspokojeniu
  if (level == stable_) return false;                      // to było tylko zakłócenie
  stable_ = level; changedAtMs_ = nowMs; return true;      // nowy, pewny stan
}
```
Procedura przerwania `onEdge` (musi być bardzo krótka):
```cpp
lastEdgeUs_.store(micros());         // zapamiętaj czas zbocza
edges_.fetch_add(1);                 // policz zbocze (diagnostyka)
if (pending_.exchange(1) == 0 && notifyTask_ != nullptr) {   // tylko pierwsze zbocze serii…
  vTaskNotifyGiveFromISR(notifyTask_, &woken);               // …budzi sterownik
  portYIELD_FROM_ISR(woken);                                 // przełącz od razu, jeśli trzeba
}
```
`IRAM_ATTR` umieszcza kod przerwania w szybkiej pamięci RAM. Funkcje przerwań są zdefiniowane
**poza** klasą, bo kompilator dla ESP32 zgłaszał błąd konsolidacji przy wersji wewnątrz klasy.

`SampleTimer::begin` – timer sprzętowy 1 MHz (tyka co 1 µs) z alarmem co 10 000 µs (100 Hz),
z automatycznym powtarzaniem. `#if ESP_ARDUINO_VERSION_MAJOR >= 3` wybiera właściwe funkcje dla
nowszej i starszej wersji bibliotek. `onAlarm` tylko budzi zadanie akwizycji.

**Sekcja 5 – sterowniki czujników (linie 528–640).**

`Mpu6050::begin` – zapis rejestrów (rejestr = komórka ustawień w czujniku):

| Rejestr | Wartość | Znaczenie |
|---|---|---|
| `WHO_AM_I` (0x75) | odczyt 0x68 lub 0x98 | „czy to na pewno MPU6050?” |
| `PWR_MGMT_1` (0x6B) | 0x01 | wybudzenie, stabilny zegar |
| `CONFIG` (0x1A) | 0x03 | filtr ok. 44 Hz (usuwa szybsze zakłócenia przed próbkowaniem 100 Hz) |
| `SMPLRT_DIV` (0x19) | 9 | 1000 Hz / (1 + 9) = 100 próbek na sekundę |
| `GYRO_CONFIG` (0x1B) | 0x08 | żyroskop ±500 °/s |
| `ACCEL_CONFIG` (0x1C) | 0x08 | akcelerometr ±4 g → 8192 jednostek na 1 g |

`Mpu6050::read` – 14 bajtów jednym transferem i sklejanie par bajtów w liczby 16-bitowe
(`be16(i) = (raw[i] << 8) | raw[i + 1]`); zwraca **surowe** wartości – przeliczenie na g robi
sterownik (`accelG(raw) = raw / 8192`). Nazwa `be16` zamiast `word`, bo `word` jest w Arduino
zarezerwowanym makrem (zepsuło to kiedyś wszystkie odczyty). `readRegs`/`writeReg` – zapis i odczyt
rejestrów przez I2C (`endTransmission(false)` = „nie zwalniaj magistrali”, zaraz czytamy).

`ClimateSensor::poll` – odczyt DHT22; poprawny odczyt zeruje licznik błędów, błędny go zwiększa,
a po 3 błędach z rzędu `valid = false` (usterka).

**Sekcja 6 – przetwarzanie sygnałów i czas pracy (linie 642–780).**

`VibrationAccumulator::addSample` – serce pomiaru drgań, **tylko liczby całkowite**:
```cpp
if (!refInit_) { ref_[i] = raw[i] * 256; refInit_ = true; }   // pierwsza próbka = punkt odniesienia
for (i = 0; i < 3; ++i) {                                       // osie X, Y, Z
  const int32_t x = raw[i] * 256;                               // format Q8: 1/256 jednostki
  if (!machineRunning) ref_[i] += (x - ref_[i]) / 200;          // na postoju: powoli dochodź do grawitacji
  const int64_t d = x - ref_[i];                                // składowa dynamiczna = drgania
  d2 += d * d;                                                  // kwadrat długości wektora drgań
}
acc_.sumSq += d2;                                               // suma kwadratów w oknie
if (d2 > acc_.peakSq) acc_.peakSq = d2;                         // największy kwadrat (szczyt)
if (++acc_.count < 100) return false;                           // okno jeszcze niepełne
window_ = acc_; acc_ = VibrationWindow(); return true;          // okno 1 s gotowe
```
**Przykład:** suwak X = +0,25 g → surowo 2048 → `x = 524 288`; punkt odniesienia X = 0 →
`d = 524 288`; po 100 próbkach `sumSq / 100 = 524 288²`, pierwiastek = 524 288, podzielone przez
8192 × 256 → **0,25 g**.

`computeVibrationFeatures` (sterownik, liczby z przecinkiem): `aRmsG = √(sumSq/count) / (8192·256)`,
`aPeakG` analogicznie ze `peakSq`, `crest = peak/RMS`, `vRmsMmS = aRmsG × 31,72` (bo
9,80665 × 1000 / (2π × 49,2) ≈ 31,72 mm/s na 1 g).

`RunHourMeter` – motogodziny: przelicza mikrosekundy na godziny maszyny
(`toHours(us) = us × 3600 / 3,6·10⁹`); `setRunning(true)` liczy uruchomienie, `setRunning(false)`
dopisuje odcinek pracy i zwraca czas cyklu; `resetService` zeruje licznik „od serwisu”;
`restore` wczytuje wartości z NVS.

**Sekcja 7 – pamięć nieulotna (linie 782–846).**

`PersistedCounters` – 40 bajtów: znacznik `PDM3` (czy to w ogóle nasze dane), wersja, CRC,
motogodziny, godziny od serwisu, zużycie, starty, serwisy, alarmy.
`CounterStore::save` – liczy CRC z całego bloku i zapisuje go pod kluczem `"cnt"`.
`CounterStore::load` – odczytuje, sprawdza znacznik, wersję i CRC; jeśli cokolwiek się nie zgadza,
dane są odrzucane (lepiej zacząć od zera niż od śmieci). `crc16` – suma kontrolna CRC-16/CCITT:
dla każdego bajtu 8 kroków „przesuń i ewentualnie XOR z 0x1021”.

**Sekcja 8 – lampki, buzzer, OLED (linie 848–1045).**

`Indicators::evaluate` – wzorce świecenia z samego zegara:
```cpp
case Pattern::Blink:     return ((nowMs / 500) & 1) != 0;   // co 500 ms zmiana → 1 Hz
case Pattern::FastBlink: return ((nowMs / 125) & 1) != 0;   // co 125 ms → 4 Hz
case Pattern::Heartbeat: return (nowMs % 2000) < 120;       // 120 ms świecenia na 2 s
```
`Indicators::update` – w każdym takcie ustala, które lampki mają świecić (podczas testu lampek –
wszystkie), zapisuje do nóżek tylko przy zmianie i co 2 s planuje „pip-pip”, jeśli alarm jest
niepotwierdzony. `updateBuzzer` to mała maszyna stanów: włącz ton → odczekaj → wyłącz → odczekaj →
kolejny sygnał – bez `delay()`.

`StatusDisplay::render` – co 0,5 s rysuje jeden z trzech ekranów (`(nowMs / 4000) % 3` daje 0, 1, 2
na zmianę) i wysyła 1 KB obrazu przez I2C1. `renderStatus`, `renderForecast`, `renderTrend` składają
napisy funkcją `snprintf` i wypisują je w wierszach co ok. 10 pikseli (opis pól: rozdz. 5.6).
`renderTrend` rysuje kropki linii 70 % i 40 % co 4 piksele, a wykres HI łączy kolejne rekordy
historii odcinkami (`drawLine`).

**Sekcja 9 – komunikacja MQTT (linie 1047–1243).**

`MqttLink::run` – pętla zadania na rdzeniu 0:
```cpp
WiFi.mode(WIFI_STA); startWifi(millis());                         // tryb klienta, łączenie
for (;;) {                                                        // pętla nieskończona
  if (xQueueReceive(eventQueue, &event, 50 ms)) publishEvent(event);  // czekaj na zdarzenie maks. 50 ms
  maintain(nowMs);                                                // pilnuj Wi-Fi i MQTT
  if (mqtt_.connected()) { mqtt_.loop(); co 30 s publish("online", retained); }
  if (forceTelemetry_ || minęło 5 s) publishTelemetry(najnowszy stan ze skrzynki);
}
```
`maintain` – maszyna stanów: brak Wi-Fi → ponów co 15 s; Wi-Fi jest, MQTT nie → połącz
z ostatnią wolą (`"offline"`, retained); po sukcesie wyślij `"online"`, zasubskrybuj `…/cmd`;
po porażce wydłuż odstęp (1 → 2 → 4 … → 30 s) i spróbuj brokera zapasowego.
`publishTelemetry` – jeden `snprintf` składa 37 pól JSON w statycznym buforze 768 B.
`publishEvent` – JSON zdarzenia (z `param` i `level` dla zmian poziomu).
`onMessage` – polecenia: `ping` → odpowiedź `PONG`, `status` → natychmiastowa telemetria, `ack` →
wkłada polecenie do kolejki (wykona je sterownik – tylko on zmienia stan alarmów).

**Sekcja 10 – zadanie akwizycji (linie 1245–1335).**
```cpp
for (;;) {
  const uint32_t ticks = ulTaskNotifyTake(pdTRUE, 100 ms);   // śpij do taktu timera
  if (wdt) esp_task_wdt_reset();                              // "żyję" dla watchdoga
  if (ticks == 0) continue;                                   // timer milczy – spróbuj znów
  if (ticks > 1) snap_.overruns += ticks - 1;                 // spóźniliśmy się o cały takt
  if (snap_.mpuOk) {
    if (mpu_.read(sample)) {                                  // 14 bajtów z czujnika
      …zapisz surową próbkę…; ++snap_.samples;
      if (vib_.addSample(sample.acc, machineRunning)) {      // okno 1 s gotowe?
        snap_.window = vib_.lastWindow(); ++snap_.windows; publish = true;
      }
    } else if (++mpuErrors >= 10) { snap_.mpuOk = false; publish = true; }  // usterka czujnika
  } else if (minęła 1 s) { snap_.mpuOk = mpu_.begin(); ++snap_.windows; publish = true; }  // próba ożywienia
  if (publish) { xQueueOverwrite(sensorMailbox, &snap_); xTaskNotifyGive(controllerTask); }
}
```
Zadanie celowo **nie liczy na liczbach z przecinkiem** – w Wokwi zadanie o wyższym priorytecie,
które używa FPU, psuje obliczenia przerwanego zadania na tym samym rdzeniu (zmierzone; patrz
DOKUMENTACJA.md, rozdz. 7.3 i 8.2).

**Sekcja 11 – sterownik `MachineController` (linie 1337–1897).**

`begin` – po kolei: zapamiętaj uchwyt własnego zadania (do budzenia z przerwań); uruchom przyciski
i RUN; ustal początkowy stan pracy; test lampek; uruchom I2C1 i OLED; przygotuj DHT22; zdarzenie
`BOOT`; odczytaj liczniki z NVS; wypisz zachętę do konsoli.

`tick` – jedna iteracja pętli sterownika (co ≤ 10 ms):
```cpp
ulTaskNotifyTake(pdTRUE, 10 ms);       // śpij do zdarzenia albo maks. 10 ms
handleRemoteCommands(now64);           // polecenia z MQTT (ack)
pollConsole(now64);                    // znaki z konsoli → polecenia
handleInputs(nowUs, nowMs, now64);     // przyciski i RUN
pollClimate(nowMs);                    // DHT22 co 2,5 s
handleSensors(now64);                  // nowe okno drgań → analiza
refreshIndicators(); indicators_.update(nowMs);   // lampki i buzzer
fpuMonitor_.check();                   // samokontrola FPU
co 0,5 s: odśwież ekran · co 1 s: linia stanu · co 5 s: historia · co 60 s: zapis NVS · co 30 s: [DIAG]
```
Sprawdzenie „czy minęło X ms” ma zawsze postać `if (nowMs - lastXMs_ >= okres)` – taki zapis działa
poprawnie nawet po przepełnieniu licznika `millis()` (po 49 dniach).

`handleInputs` – ACK naciśnięty → `acknowledge("przycisk")`; SERWIS wciśnięty → uzbrojenie (krótki
sygnał), trzymany 3 s → `confirmService`; RUN zmieniony → licznik motogodzin, informacja dla akwizycji
(`machineRunning`), zdarzenie `RUN_START` (+ `engine_.onMachineStart()`: zużycie rozruchu i czyszczenie
trendów) albo `RUN_STOP` (+ natychmiastowy zapis NVS).

`acknowledge` – `engine_.acknowledgeAll()`, dziennik, zdarzenie `ACK`, sygnał.
`confirmService` – zapamiętaj zużycie, wyzeruj licznik „od serwisu” i silnik
(`engine_.serviceDone()`), zwiększ licznik serwisów, zdarzenie `SERVICE_DONE`, sygnał, zapis NVS.

`pollClimate` – co 2,5 s `climate_.poll()`, kopia odczytu do stanu systemu, `updateFaults()`.
`handleSensors` – nowa paczka z akwizycji → przelicz na jednostki fizyczne, `updateFaults()`; jeśli
zamknęło się nowe okno – `computeVibrationFeatures` i `analyze`. `updateFaults` – porównuje maskę
usterek z poprzednią i zgłasza `SENSOR_FAULT` / `SENSOR_OK`.

`analyze` – przygotowuje `EngineInput` (praca, godziny od poprzedniego kroku, temperatura i wilgotność
z flagą „poprawne”, drgania, usterki), woła `engine_.step(…)`, każdą zmianę poziomu (oprócz kanału
SENSOR, który ma własne zdarzenia) zamienia w zdarzenie, a wynik kopiuje do `sys_` – stąd biorą
dane ekran, lampki, dziennik i MQTT.

`refreshIndicators` – zielona: OK (ciągła w pracy, „serce” na postoju); żółta: PLAN; czerwona według
najpilniejszego stanu alarmów (szybkie miganie / ciągłe / wolne miganie); dźwięk przy
niepotwierdzonym aktywnym alarmie.

`push` – nadaje zdarzeniu numer i czas, wkłada do dziennika (bufor 32), wypisuje
`[ZDARZENIE #n] …` i wysyła do kolejki MQTT (jeśli pełna – licznik „utracone”). `describe` składa
opis: `STAN -> PLAN (kontrola za 47 h)` albo `TEMP -> ALARM (71.00)`.

`recordHistory` – co 5 s jeden rekord 16 B (wartości ×10/×100, brak odczytu jako `INT16_MIN` /
`0xFF`, flagi w bitach). `restoreCounters` / `persist` – odczyt i zapis NVS (zapis okresowy tylko,
gdy coś się zmieniło – oszczędza pamięć flash).

`pollConsole` – zbiera znaki do bufora 48 B aż do Enter; `execute` – dzieli linię na polecenie
i argument (`historia 6` → `historia` + 6), zamienia litery na małe i wybiera akcję (rozdz. 7).
`printReport`, `printHistory`, `printEvents`, `printStatus`, `printDiagnostics` – formatują teksty
opisane w rozdziałach 6–7.

**Sekcja 12 – obiekty globalne i punkty wejścia (linie 1899–1930).**
```cpp
static MqttLink g_link;                   // komunikacja
static AcquisitionService g_acquisition;  // akwizycja
static MachineController g_controller;    // sterownik
// static = obiekty tworzone raz, w pamięci statycznej (nie na stercie)

void setup() {
  Serial.begin(115200);                   // port szeregowy 115200 bitów/s
  logMsg(…)                               // ramka z wersją
  xQueueCreate(…) ×4                      // skrzynki i kolejki
  g_controller.begin(&g_link);            // sterownik pierwszy – jego zadanie budzą przerwania
  g_acquisition.begin();                  // czujnik drgań, zadanie akwizycji, timer 100 Hz
  g_link.begin();                         // zadanie komunikacji na rdzeniu 0
  enableLoopWDT();                        // watchdog pilnuje też loop()
  logMsg("[SYSTEM] Gotowy: …");
}
void loop() { g_controller.tick(); }      // pętla główna = takt sterownika
```

### 12.3 `predictive.h` – „mózg”

Plik w czystym C++ (bez Arduino) – dlatego da się go testować na zwykłym komputerze.

**Funkcje pomocnicze.** `clampf(v, lo, hi)` obcina wartość do przedziału. `minf(a, b)` – mniejsza
z dwóch. `interpolate(x, xs, ys, n)` – odczyt z „łamanej” zadanej punktami, np. dla temperatury
punkty (50 → 1,0), (60 → 0,6): dla 55 °C wynik to 0,8 (w połowie drogi).

**`RingBuffer<T, N>`** – bufor cykliczny: tablica `N` elementów i „głowica” `head_`. `push` wpisuje
w miejsce głowicy i przesuwa ją (`(head_ + 1) % N` – po końcu wraca na początek); gdy bufor jest
pełny, nadpisuje najstarszy. `at(i)` – i-ty element od najstarszego.

**`LinearTrend<N>`** – regresja liniowa w oknie `N` próbek. Prosta `y = a + b·x` dopasowana metodą
najmniejszych kwadratów potrzebuje tylko sum Σy, Σxy, Σy² (Σx i Σx² dla x = 0…n−1 liczy się wzorem).
- `add(y)` – gdy okno nie jest pełne, dodaje składniki do sum; gdy jest pełne, „przesuwa” sumy
  w czasie stałym: `sxy = sxy − (sy − yOld) + (N−1)·y` (wszystkie stare punkty przesuwają się o jedno
  miejsce w lewo, najstarszy wypada, nowy wchodzi na koniec). Co `N` próbek `recalc()` liczy sumy od
  nowa, żeby nie narastały błędy zaokrągleń.
- `coefficients(a, b)` – nachylenie `b = (n·Σxy − Σx·Σy) / (n·Σx² − (Σx)²)`, wyraz wolny `a`.
- `r2()` – współczynnik determinacji (0…1).
- `hoursToReach(próg, …)` – jeśli jest za mało punktów → nieskończoność; jeśli dopasowana ostatnia
  wartość już ≥ próg → 0; jeśli trend za słaby (nachylenie < minimum albo R² < 0,6) → nieskończoność;
  w przeciwnym razie `(próg − ostatnia) / nachylenie`.

**Przykład:** 20 pomiarów 40, 41, …, 59 °C (1 °C na krok) → `b = 1 °C/h`, `R² = 1`, ostatnia 59 °C,
do 70 °C: `(70 − 59) / 1 = 11 h` (dokładnie to sprawdza test jednostkowy).

**`Level`, `AlarmState`, `Thresholds`.** Poziom: NORMA/OSTRZEŻENIE/ALARM. Stan alarmu wg ISA-18.2
(4 stany – rozdz. 9.3). Progi: `warn`, `alarm`, `hysteresis`, `delayOn` (ile ocen potwierdza wzrost).

**`AlarmChannel`** – jeden parametr:
```cpp
Level classify(float v) const {
  if (!isfinite(v)) return level_;                                          // brak danych – bez zmian
  if (v >= alarm || (level_ == kAlarm && v > alarm - hysteresis)) return kAlarm;
  if (v >= warn  || (level_ != kNormal && v > warn - hysteresis)) return kWarning;
  return kNormal;
}
bool update(float value) {
  const Level target = classify(value);
  if (target > level_) { if (++pending_ < delayOn) return false; }  // wzrost – czekaj na potwierdzenie
  pending_ = 0;
  if (target == level_) return false;                                // bez zmian
  previous_ = level_; level_ = target;
  if (level_ == kAlarm) state_ = kUnackActive;                       // nowy alarm – do potwierdzenia
  else if (previous_ == kAlarm)                                      // alarm ustąpił:
    state_ = (state_ == kUnackActive) ? kUnackCleared : kNormal;     // niepotwierdzony zostaje do ACK
  return true;
}
```
**Przykład (progi 60/70, histereza 2, zwłoka 2):** 61 → czekaj; 61 → OSTRZEŻENIE; 59 → nadal
OSTRZEŻENIE (59 > 58); 57,9 → NORMA. `acknowledge()` zmienia „niepotwierdzony aktywny” na
„potwierdzony”, a „ustąpił niepotwierdzony” na „brak”.

**`WearModel`** – zużycie: `tempFactor` (2 do potęgi (T−40)/10, ograniczone 1…16), `vibFactor`
((v/2,8)³ powyżej 2,8 mm/s, ograniczone 1…27), `update` (tylko w pracy: chwilowe tempo, wygładzone
tempo EWMA z α = 0,1, dodanie `tempo × godziny`), `onStart` (+0,5 h), `reset` (po przeglądzie: zero
i świeży filtr), `hoursToService` ((200 − zużycie) / max(tempo, 1)).

**`computeHealth`** – oceny cząstkowe przez `interpolate`, średnia ważona tylko z dostępnych
parametrów (waga 0 dla brakujących) i ograniczenie „najgorsza + 0,3” (rozdz. 9.4).
**`isoZone`** – strefa A/B/C/D z prędkości drgań.

**`Param`, `Cause`, `Driver`** – listy: kanały (TEMP, VIB, HUM, MAINT, SENSOR), przyczyny, źródła
prognozy. **`EngineConfig`** – wszystkie progi i parametry algorytmu w jednym miejscu (domyślne
wartości jak w rozdz. 9). **`EngineInput` / `EngineOutput`** – dane wejściowe kroku i jego wynik.
**`LevelChange`** – opis jednej zmiany poziomu (dla zdarzeń).

**`PredictiveEngine::step`** – jeden krok analizy:
1. `wear_.update(…)` – zużycie,
2. w pracy: `tempTrend_.add(T)`, `vibTrend_.add(v)` – trendy,
3. `ch_[…].update(…)` dla TEMP, VIB, HUM, SENSOR – progi; zmiany trafiają do tablicy `changes`,
4. nachylenia, R² i czasy do alarmu z trendów (tylko gdy wartość jest jeszcze poniżej progu alarmu),
5. `computeHealth` + wygładzanie EWMA (α = 0,3) → HI,
6. prognoza = najmniejszy z czasów + `driver` (co ją wyznaczyło),
7. `diagnose` → przyczyna; `rawStatus` → status „surowy”; potwierdzanie: wyżej po 2 krokach
   (`confirmCount_`), niżej po 5 (`holdCount_`); kanał MAINT dostaje wartość 0/1/2 = status.

`onMachineStart` – zużycie rozruchu i nowe okna trendów. `acknowledgeAll` – ACK na wszystkich
kanałach. `serviceDone` – zero zużycia, trendów, filtrów i statusu. `worstAlarmState` – najpilniejszy
stan alarmów (dla czerwonej lampki). `alarmMask` – bity kanałów w alarmie (pole `alarms` w MQTT).
`diagnose` i `rawStatus` – reguły z rozdz. 9.6–9.7, zapisane wprost jako kolejne `if`.

**Nazwy tekstowe** – `paramCode` (TEMP…), `levelCode` (NORMA/OSTRZEZENIE/ALARM, a dla MAINT
OK/PLAN/KONTROLA), `causeCode` (do MQTT), `causeText` (do OLED, bez polskich znaków – wyświetlacz ich
nie ma), `driverText`.

### 12.4 `test/test_predictive.cpp` – testy jednostkowe

```cpp
#define CHECK(cond) …            // jeśli warunek fałszywy – wypisz "BLAD plik:linia: warunek"
#define CHECK_NEAR(a, e, tol) …  // jeśli |a − e| > tol – wypisz obie wartości
```
Funkcje `testRingBuffer`, `testLinearTrend`, `testAlarmChannel`, `testWearModel`, `testHealth`,
`testEngine` sprawdzają kolejne klasy na znanych przykładach (np. trend 1 °C/h → 11 h do 70 °C).
`main()` uruchamia wszystkie i zwraca 0, gdy nie ma błędów. Uruchomienie (macOS/Linux, w katalogu
głównym repozytorium):
```bash
c++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -fsanitize=address,undefined \
    -o test_predictive test/test_predictive.cpp && ./test_predictive
```
Wynik: `Wynik: 84 asercji spelnionych, 0 niespelnionych`. Nowy test = nowa linia `CHECK(…)`
w odpowiedniej funkcji.

### 12.5 `diagram.json` – schemat

```json
{"type": "wokwi-dht22", "id": "dht1", "top": 290, "left": 280, "attrs": {"temperature": "40", "humidity": "45"}}
```
Element: `type` – rodzaj części Wokwi, `id` – nazwa (używana w połączeniach), `top`/`left` – położenie
w pikselach, `attrs` – ustawienia (tu: temperatura i wilgotność na starcie).

```json
["dht1:SDA", "esp:16", "orange", ["v10.1", "h-174.5", "v-280.6"]]
```
Połączenie: skąd (`element:pin`), dokąd, kolor przewodu, przebieg (`v` – odcinek pionowy o tyle
pikseli, `h` – poziomy; minus = w górę / w lewo). Para `esp:TX` ↔ `$serialMonitor:RX` podłącza monitor
portu. Części: `esp` (ESP32), `oled1`, `led_red`/`led_yel`/`led_grn`, `r_red`/`r_yel`/`r_grn`
(220 Ω), `bz1` (buzzer), `imu1` (MPU6050), `btn_ack`, `btn_srv`, `sw_run`, `dht1`, `r_dht` (10 kΩ).

### 12.6 `libraries.txt`

Lista bibliotek z przypiętymi wersjami (`nazwa@wersja`) – Wokwi instaluje je przed kompilacją.
Linie z `#` to komentarze. MPU6050 celowo nie ma biblioteki – projekt ma własny, lżejszy sterownik.

---

## 13. Jak samemu coś zmienić

Po każdej zmianie: wklej poprawiony plik do Wokwi i uruchom ponownie (kompilacja 1–2 min). Jeśli
zmieniasz `predictive.h`, uruchom też testy jednostkowe (rozdz. 12.4).

**13.1 Inne progi temperatury (np. 55 / 65 °C).** Najlepiej w `main.cpp`, w funkcji
`makeEngineConfig()` (klasa `MachineController`):
```cpp
pdm::EngineConfig c;
c.stepHours = cfg::kStepHours;
c.temp = {55.0f, 65.0f, 2.0f, 2};   // ostrzeżenie, alarm, histereza, zwłoka
return c;
```
Zmiana tutaj nie psuje testów jednostkowych (testują wartości domyślne z `predictive.h`).
Sprawdzenie: ustaw 56 °C → `TEMP -> OSTRZEZENIE`.

**13.2 Czas rzeczywisty.** `cfg::kTimeScale = 1` – 1 s = 1 s. Algorytm działa tak samo, ale zużycie
i trendy rosną w tempie fabrycznym (demonstracja trwałaby tygodnie).

**13.3 Dłuższy interwał przeglądu (np. 500 h).** W `makeEngineConfig()`: `c.wear.serviceIntervalH = 500.0f;`.

**13.4 Nowe polecenie konsoli `wersja`.** W `MachineController::execute`, przed ostatnim `else`:
```cpp
} else if (strcmp(line, "wersja") == 0) {
  logMsg("[SYSTEM] %s", cfg::kFirmware);
```
i dopisz linię do pomocy (`printLine("  wersja          - wersja oprogramowania");`).

**13.5 Inna maszyna w MQTT.** `cfg::kTopicBase = "wtp4/pdm/maszyna-02"` – tematy zmienią się
automatycznie (subskrybuj `wtp4/pdm/maszyna-02/#`).

**13.6 Inna maszyna (np. silnik 4-biegunowy, 1475 obr/min).** `cfg::kDominantFreqHz = 24.6f` –
ta sama amplituda w g da 2 razy większą prędkość drgań w mm/s.

---

## 14. Rozwiązywanie problemów

| Objaw | Przyczyna | Co zrobić |
|---|---|---|
| zegar symulacji stoi albo idzie bardzo wolno | karta Wokwi w tle | wróć na kartę Wokwi i zostaw ją widoczną |
| „Compiling project…” trwa długo | kolejka darmowego serwera Wokwi | poczekaj 1–2 min |
| błąd kompilacji „multiple definition of setup” | w `sketch.ino` zostały domyślne `setup()`/`loop()` | wklej `sketch.ino` z repozytorium |
| błąd „predictive.h: No such file” | brak pliku albo zła nazwa | dodaj plik `predictive.h` (dokładnie ta nazwa) |
| Library Manager pokazuje mniej niż 5 bibliotek | plik ma inną nazwę (np. `libraries.txtmain.cpp`) | zmień nazwę na `libraries.txt` |
| stale `T=---°C` | DHT22 bez przewodu danych albo usterka | sprawdź `diagram.json` (linia `"dht1:SDA", "esp:16"`) |
| drgania nie reagują na suwak | maszyna stoi (program uczy się grawitacji) | włącz RUN i dopiero wtedy przesuń suwak |
| `MQTT offline` cały czas | brak Internetu albo broker niedostępny | pomiary i lampki działają dalej; poczekaj na ponowienie (do 30 s) |
| przełącznik wygląda na włączony, a log pokazuje `POSTÓJ` | rozjechany stan elementu | przełącz go dwa razy |
| po Stop/Start liczniki od zera | Wokwi czyści pamięć flash przy nowej symulacji | trwałość sprawdzaj poleceniem `restart` |
| przez ok. 3 s po `restart` jest `T=---` | pierwszy odczyt DHT22 po restarcie programowym bywa nieudany | nic – po 2,5 s jest poprawny odczyt |
| status KONTROLA bez żadnego alarmu | przyspieszony czas + wysoka temperatura/drgania = szybkie zużycie | to prawidłowa predykcja (rozdz. 9.8); `serwis` zeruje |
| nic nie słychać | przeglądarka blokuje dźwięk | kliknij raz w stronę Wokwi |

---

## 15. Ściąga

```
LAMPKI   zielona ciągła = OK, praca │ zielona błysk co 2 s = OK, postój │ żółta = PLAN
         czerwona: szybko + pip = alarm niepotwierdzony (ACK!) │ ciągła = potwierdzony │ wolno = ustąpił (ACK)
PRZYCISKI ACK (A) = potwierdź alarmy │ SERWIS (S) trzymaj 3 s = przegląd wykonany │ RUN: prawo = praca
SUWAKI   DHT22: 60 °C ostrzeżenie, 70 °C alarm; 80 % / 90 % wilgotności
         MPU6050 (przy pracy!): X +0,10 g = 3,2 mm/s (C, ostrzeżenie) │ +0,25 g = 7,9 mm/s (D, alarm)
KONSOLA  pomoc · stan · historia [n] · zdarzenia [n] · ack · serwis · zuzycie <proc> · zapisz · restart
STATUS   KONTROLA: alarm / zużycie ≥ 100 % / prognoza ≤ 8 h / HI < 40 %
         PLAN: ostrzeżenie / usterka czujnika / zużycie ≥ 80 % / prognoza ≤ 48 h / HI < 70 %
ZUŻYCIE  k = 2^((T−40)/10) · (v/2,8)³ │ interwał 200 h │ 1 s symulacji = 1 h pracy
MQTT     broker.emqx.io │ wtp4/pdm/maszyna-01/{telemetry, event, status, cmd} │ cmd: ping, status, ack
```
