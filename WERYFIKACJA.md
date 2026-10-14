# Weryfikacja projektu z wytycznymi i założeniami początkowymi

| | |
|---|---|
| **Data** | 14.10.2026 |
| **Wersja** | WTP4-PdM 0.4.0 (historia: `git log`) |
| **Źródła wymagań** | (1) wytyczne prowadzącego „Wyzwania Technologiczne Przemysłu 4.0 – przewodnik realizacji projektu”, (2) założenia z zadania początkowego: stos, metodyka, roadmapa, zasady współpracy |
| **Metoda** | każde wymaganie → dowód (plik, sekcja, test, polecenie) → status; kontrole automatyczne w rozdz. 3 |

Legenda: ✅ spełnione · ⚠️ spełnione z zastrzeżeniem (rozdz. 4) · ☐ do wykonania przez autora.

---

## 1. Wytyczne prowadzącego – kryteria oceny 5.0

### 1.1 Kod źródłowy

| Wymaganie | Dowód | Status |
|---|---|---|
| kod w pełni zoptymalizowany | alokacja statyczna; trend regresji O(1); okno drgań bez bufora próbek; arytmetyka całkowita w zadaniu 100 Hz; pomiar: 198 KB wolnej sterty, 1 pominięty takt na 38 800 próbek (DOKUMENTACJA, rozdz. 6) | ✅ |
| czytelny, dobrze zorganizowany | `main.cpp` w 12 opisanych sekcjach, algorytm w niezależnym `predictive.h`; stałe w jednym miejscu (`cfg`); konwencje nazw (RUNBOOK, rozdz. 11–12) | ✅ |
| pełne komentarze | Doxygen: 140 opisów `@brief` (75 w `main.cpp`, 65 w `predictive.h`), `@file` w 4 z 4 plików kodu | ✅ |
| przerwania | przerwanie timera sprzętowego 100 Hz (takt pomiaru) i przerwania GPIO (CHANGE) dla ACK, SERWIS, RUN; procedury w IRAM, powiadomienia zadań FreeRTOS | ✅ |
| oszczędność pamięci | 0 użyć `String`, 0 `malloc`/`new` w kodzie; struktury 16 B / 16 B / 40 B (2 × `static_assert`); historia 10 min = 1,9 KB | ✅ |
| zaawansowane algorytmy sterowania | model zużycia Palmgrena–Minera, regresja w oknie przesuwnym z R², alarmy wg ISA-18.2 (histereza, zwłoka, kwitowanie), indeks kondycji, diagnoza wieloparametrowa, prognoza terminu kontroli; samodiagnostyka FPU | ✅ |

### 1.2 Dokumentacja

| Wymagany element | Gdzie | Status |
|---|---|---|
| szczegółowy opis techniczny | DOKUMENTACJA, rozdz. 1–4 (cel, sprzęt, architektura, moduły z wzorami) | ✅ |
| schematy połączeń układu | DOKUMENTACJA 2.2: zrzut z Wokwi (`docs/schemat_wokwi.png`), schemat blokowy, tabela pinów | ✅ |
| diagramy przepływu danych i algorytmów | DOKUMENTACJA: rys. 2–7 (połączenia, przepływ danych, akwizycja, krok analizy, stany alarmu, status) – 6 diagramów, renderowanie sprawdzone na GitHubie | ✅ |
| opis testów i wyników | DOKUMENTACJA, rozdz. 7 + TESTY.md | ✅ |
| wyzwania technologiczne | DOKUMENTACJA 8.1 (rozwiązywane przez projekt) i 8.2 (napotkane trudności i rozwiązania) | ✅ |
| dalszy rozwój | DOKUMENTACJA, rozdz. 9 | ✅ |
| dokumentacja dla nowego użytkownika (ponad wymagania) | RUNBOOK.md – nauka od zera, każda kontrolka, każda linia dziennika, przewodnik po kodzie | ✅ |

### 1.3 Testy

| Wymaganie | Dowód | Status |
|---|---|---|
| co najmniej 8 scenariuszy | TESTY.md: dokładnie 8 (T1–T8) – zgodnie z roadmapą | ✅ |
| przypadki standardowe | T1 (praca), T2 (progi temperatury), T3 (drgania), T6 (przegląd) | ✅ |
| przypadki niestandardowe / brzegowe | T3 (skok w środku okna pomiarowego), T6 (stan tuż po serwisie), T7 (usterka czujnika – awaryjny), T8 (restart – utrata zasilania) | ✅ |
| udokumentowane wyniki | każdy scenariusz: procedura, oczekiwanie, fragment dziennika z wersji 0.4.0, komunikaty MQTT; 8/8 zaliczonych | ✅ |
| weryfikacja uzupełniająca | 84 asercje testów jednostkowych; pomiar integralności FPU (DOKUMENTACJA 7.3) | ✅ |

### 1.4 Prezentacja

| Wymaganie | Dowód | Status |
|---|---|---|
| ok. 10 minut | PREZENTACJA.md: plan czasowy 0:00–10:00; notatki prelegenta z oknami czasowymi | ✅ |
| cel i założenia | slajdy 2–3 | ✅ |
| rozwiązania technologiczne | slajdy 4–7 | ✅ |
| demonstracja (nagranie lub na żywo) | slajd 8 + scenariusz 3-minutowy; skrypt reżysera `demo/rezyser_demo.js` (pokaz automatyczny z podpisami) i instrukcja nagrania planu B | ✅ |
| trudności i sposoby rozwiązania | slajd 9 (diagnoza błędu FPU + 4 inne) | ✅ |
| dalszy rozwój | slajd 12 | ✅ |
| gotowe pliki | `prezentacja/slajdy.pdf`, `.pptx` (16 slajdów, 16 notatek), `.html`; źródło Marp i skrypt budowania | ✅ |
| wygłoszenie, nazwisko na slajdzie, ewentualne nagranie | wykonuje autor | ☐ |

### 1.5 Temat i progi ocen

| Ocena | Wymaganie | Realizacja | Test | Status |
|---|---|---|---|---|
| 3.0 | jeden parametr | drgania (MPU6050, 100 Hz, prędkość drgań i strefy ISO 10816) | T3 | ✅ |
| 3.5 | co najmniej dwa parametry | temperatura, wilgotność, czas pracy, liczba uruchomień | T1, T2 | ✅ |
| 4.0 | progi alarmowe | ostrzeżenie/alarm dla temperatury, drgań i wilgotności; ISA-18.2 | T2, T3 | ✅ |
| 4.5 | historia pomiarów i zdarzeń/awarii | historia 10 min, dziennik 32 zdarzeń, usterki czujników jako zdarzenia, NVS, MQTT | T7, T8 | ✅ |
| 5.0 | analiza kilku parametrów + mechanizm wskazujący termin kontroli | zużycie (temperatura × drgania), trendy, diagnoza z korelacji, prognoza „kontrola za X h”, status OK/PLAN/KONTROLA | T4 (PLAN 8 °C przed progiem), T5 (łożysko), T6 | ✅ |
| temat | „automatycznie informować o konieczności kontroli lub serwisu” | lampki, buzzer, OLED, dziennik, MQTT | wszystkie | ✅ |

### 1.6 Praca w Wokwi (kroki z wytycznych)

| Krok | Realizacja | Status |
|---|---|---|
| projektowanie układu | `diagram.json` – 15 elementów, 32 połączenia | ✅ |
| programowanie | C++ (`main.cpp`, `predictive.h`) | ✅ |
| symulacja i testowanie | wszystkie scenariusze wykonane w Wokwi | ✅ |
| rozwiązanie IoT (MQTT) | telemetria, zdarzenia, polecenia, LWT – sprawdzone zewnętrznym klientem | ✅ |
| rejestracja / zapis projektu | konto Wokwi i **SAVE** – opcjonalne, opisane w README | ☐ |

---

## 2. Założenia początkowe (zadanie z pierwszej wiadomości)

### 2.1 Stos technologiczny

| Założenie | Realizacja | Status |
|---|---|---|
| ESP32 | ESP32 DevKitC V4, dwa rdzenie, FreeRTOS | ✅ |
| MPU6050 – drgania | własny sterownik rejestrowy, 100 Hz, ±4 g, filtr 44 Hz | ✅ |
| DHT22 – temperatura | temperatura i wilgotność, wykrywanie usterki | ✅ |
| LED / wyświetlacz (opcjonalnie) | kolumna 3 LED, buzzer, OLED 128×64 z 3 ekranami | ✅ |
| C++ | C++17 | ✅ |
| przerwania sprzętowe | timer + GPIO (rozdz. 1.1) | ✅ |
| optymalizacja pamięci | rozdz. 1.1 | ✅ |
| MQTT | `broker.emqx.io` + zapasowy, 4 tematy, LWT, ponawianie | ✅ |

### 2.2 Metodyka

| Założenie | Dowód | Status |
|---|---|---|
| praca iteracyjna (MVP), etapami | commity: `50237a6` (Etap 1), `e4cfdff` (Etap 2), `1b9259a` (Etap 3), `a79f4b0` (poprawka), `0807e2e` (Etap 4) | ✅ |
| zatrzymanie po etapie, podsumowanie, zgoda | każdy etap zakończony podsumowaniem i akceptacją przed kolejnym | ✅ |
| czysty kod, wydajność | rozdz. 1.1 | ✅ |
| brak blokującego `delay()` | 0 wywołań `delay()` w kodzie; zadania czekają na powiadomienia i kolejki | ⚠️ zastrzeżenie: biblioteka DHT22 używa wewnętrznie `delay(2)` (w FreeRTOS oddaje procesor) |

### 2.3 Roadmapa

| Etap | Wymaganie | Status |
|---|---|---|
| 1 | `diagram.json`, `libraries.txt` | ✅ |
| 2 | `main.cpp`: obiektowo, przerwania, odczyt czujników | ✅ |
| 3 | algorytm analizujący kilka parametrów i wskazujący moment kontroli; komentarze Doxygen; test przez autora | ✅ |
| 4 | `DOKUMENTACJA.md` (z diagramem przepływu), `TESTY.md` (dokładnie 8 scenariuszy), `PREZENTACJA.md` | ✅ |

### 2.4 Zasady współpracy

| Zasada | Dowód | Status |
|---|---|---|
| commity i push przy kamieniach milowych, opisy po polsku | `git log` – wszystkie opisy po polsku | ✅ |
| README jako dziennik projektu | README: stan etapów, decyzje, osobliwości symulatora | ✅ |
| instrukcja „jak przetestować u siebie” w repozytorium | README (krok po kroku) + RUNBOOK | ✅ |
| weryfikacja w przeglądarce (Wokwi) | scenariusze, pomiary FPU i demonstracja wykonane w Wokwi | ✅ |
| wytyczne prowadzącego poza repozytorium | plik w `.gitignore` | ✅ |

---

## 3. Kontrole wykonane przy weryfikacji

| Kontrola | Polecenie / metoda | Wynik |
|---|---|---|
| testy jednostkowe | `c++ -std=c++17 … -fsanitize=address,undefined test/test_predictive.cpp && ./test_predictive` | 84/84, kod wyjścia 0 |
| kompilacja `main.cpp` | kompilacja składniowa z nagłówkami zastępczymi Arduino, `-Wall -Wextra` | 0 ostrzeżeń |
| oprogramowanie w Wokwi | porównanie skrótów plików w edytorze Wokwi z repozytorium | identyczne we wszystkich testach |
| `delay`, `String`, `malloc`/`new` | `grep` w `main.cpp`, `predictive.h` (poza komentarzami) | 0 / 0 / 0 |
| komentarze Doxygen | `grep -c '@brief'` | 75 + 65 |
| liczba scenariuszy | `grep -c '^## T[0-9]' TESTY.md` | 8 |
| odnośniki w dokumentach | skrypt sprawdzający pliki i kotwice w 6 plikach Markdown | 0 błędnych |
| diagramy | podgląd DOKUMENTACJA.md na GitHubie | 6/6 wyrenderowanych |
| slajdy | `./prezentacja/zbuduj.sh` + przegląd PDF slajd po slajdzie | 16 slajdów, 16 notatek, bez nachodzenia elementów |
| oś czasu demonstracji | symulacja 180 kroków silnika `predictive.h` z osią czasu skryptu | zdarzenia w zaplanowanych chwilach (KONTROLA ok. 1:34, alarm 2:01, ACK 2 kanałów 2:20, OK po serwisie) |
| skrypt reżysera | `node --check`; w Wokwi sprawdzone osobno sterowanie: przełącznik RUN, panele czujników, suwaki, przyciski, polecenia konsoli, nakładka z podpisami | działa; patrz rozdz. 4 |

---

## 4. Zastrzeżenia, ograniczenia i ryzyka

| Temat | Opis | Wpływ |
|---|---|---|
| sprzęt fizyczny | projekt sprawdzony w symulatorze (zgodnie z wytycznymi). Poprawka FPU zmierzona w Wokwi; na sprzęcie ESP-IDF zapisuje rejestry FPU, a poprawka i tak ogranicza przełączenia kontekstu | brak |
| `delay(2)` w bibliotece DHT22 | impuls startowy czujnika; w FreeRTOS to `vTaskDelay` (procesor wolny dla innych zadań), wykonywany w zadaniu sterownika – akwizycja ma wyższy priorytet | brak wpływu na pomiar |
| czas przyspieszony | x3600 na potrzeby demonstracji; we wdrożeniu `kTimeScale = 1` | konfiguracja |
| pomiar drgań | prędkość szacowana z przyspieszenia przy założeniu składowej 1× obroty; bez FFT | opisane w „dalszym rozwoju” |
| DHT22 zamiast czujnika przemysłowego | uproszczenie symulacji; logika niezależna od czujnika | brak |
| bezpieczeństwo MQTT | publiczny broker bez TLS i uwierzytelnienia | tylko demonstracja; opisane w „dalszym rozwoju” |
| pierwszy odczyt DHT22 po restarcie programowym | nieudany, `---` przez ok. 3 s, usterka nie jest zgłaszana | kosmetyczne |
| pełny 3-minutowy przebieg skryptu reżysera | wymaga widocznego okna przeglądarki przez cały czas – skrypt przerywa pokaz z komunikatem, gdy karta zostanie ukryta | instrukcja w `demo/INSTRUKCJA_NAGRANIA.md` |

---

## 5. Wniosek

Wszystkie wymagania z wytycznych (kod, dokumentacja, testy, materiały prezentacji, progi 3.0–5.0)
i wszystkie założenia początkowe (stos, metodyka, roadmapa, zasady współpracy) są spełnione
i udokumentowane dowodami. Zastrzeżenia z rozdz. 4 są opisane w dokumentacji i nie naruszają wymagań.

**Do zrobienia przez autora przed zajęciami:**
- [ ] wpisać imię i nazwisko na slajdzie tytułowym (`prezentacja/slajdy.md`) i przebudować slajdy (`./prezentacja/zbuduj.sh`),
- [ ] zapisać projekt w Wokwi (**SAVE**) – stały link do pokazu,
- [ ] nagrać plan B według `demo/INSTRUKCJA_NAGRANIA.md`,
- [ ] przećwiczyć wystąpienie z zegarem (10 minut, notatki w PPTX / widok prelegenta w HTML).
