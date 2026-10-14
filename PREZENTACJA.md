# Prezentacja projektu – scenariusz (ok. 10 minut)

**Temat:** Inteligentny system utrzymania ruchu maszyn (ESP32, Wokwi, MQTT)
**Forma:** slajdy + demonstracja na żywo w Wokwi (plan awaryjny na końcu)

Zawartość zgodna z wytycznymi: cel i założenia → rozwiązania technologiczne → demonstracja →
trudności i ich rozwiązania → dalszy rozwój.

## Plan czasowy

| Czas | Część | Slajdy |
|---|---|---|
| 0:00–0:45 | Problem i cel | 1–2 |
| 0:45–1:30 | Założenia i realizacja wymagań | 3 |
| 1:30–3:00 | Architektura sprzętu i oprogramowania | 4–5 |
| 3:00–4:30 | Algorytm predykcyjny | 6–7 |
| 4:30–7:30 | **Demonstracja na żywo** | – |
| 7:30–8:45 | Trudności i ich rozwiązania | 8 |
| 8:45–9:30 | Testy i wyniki | 9 |
| 9:30–10:00 | Dalszy rozwój, podsumowanie | 10 |

## Przygotowanie (przed zajęciami)

- [ ] Projekt zapisany w Wokwi (**SAVE**) – stały link; pliki zgodne z repozytorium.
- [ ] **Symulacja uruchomiona 2–3 min przed wystąpieniem** – pierwsza kompilacja trwa 1–2 min.
- [ ] Karta Wokwi **widoczna** (karta w tle spowalnia symulację prawie do zera).
- [ ] Maszyna zatrzymana lub tuż po `serwis`; temperatura 40 °C, przyspieszenie X = 0.
- [ ] Druga karta: MQTTX Web połączony z `broker.emqx.io` (wss, port 8084, ścieżka `/mqtt`),
      subskrypcja `wtp4/pdm/maszyna-01/#` – instrukcja w README.
- [ ] Konsola przeglądarki z wklejonym skryptem rampy temperatury (README, sekcja „Jak testować”) –
      gotowa do uruchomienia jednym Enterem.
- [ ] Plan awaryjny: nagranie ekranu z próby (np. QuickTime) i zrzuty z [TESTY.md](TESTY.md).

---

## Slajdy i notatki

### Slajd 1 – Tytuł
**Na slajdzie:** tytuł, autor, przedmiot, zrzut układu z Wokwi (`docs/schemat_wokwi.png`).
**Powiedz:** „Pokażę system, który nie tylko alarmuje po przekroczeniu progu, ale przewiduje, kiedy
maszyna będzie wymagała kontroli – i podaje dlaczego.”

### Slajd 2 – Problem i cel
**Na slajdzie:** trzy strategie utrzymania ruchu: reaktywna (po awarii) → prewencyjna (co stały okres)
→ **predykcyjna (wg stanu maszyny)**. Cel: monitorować kilka parametrów i prognozować termin kontroli.
**Powiedz:** przestój nieplanowany jest najdroższy; serwis „co 200 h” bywa za wcześnie albo za późno.
Przemysł 4.0 = pomiar + analiza + komunikacja.

### Slajd 3 – Założenia i wymagania
**Na slajdzie:** tabela 3.0–5.0 z realizacją (z [DOKUMENTACJA.md, 1.2](DOKUMENTACJA.md#12-realizacja-wymagań));
maszyna: pompa 2950 obr/min, klasa II ISO 10816; czas przyspieszony 1 s = 1 h.
**Powiedz:** dlaczego przyspieszony czas – żeby w 3 minuty pokazać to, co w zakładzie trwa tygodnie.

### Slajd 4 – Sprzęt
**Na slajdzie:** schemat (rys. 1 i 2 z dokumentacji): ESP32, MPU6050 (drgania), DHT22 (T, RH), OLED,
kolumna LED, buzzer, ACK, SERWIS, RUN; dwie magistrale I2C.
**Powiedz:** osobna magistrala dla wyświetlacza, żeby odświeżanie ekranu nie zakłócało pomiaru drgań.

### Slajd 5 – Oprogramowanie
**Na slajdzie:** diagram przepływu danych (rys. 3): przerwanie timera 100 Hz → zadanie akwizycji
(rdzeń 1, priorytet 3) → sterownik → komunikacja MQTT (rdzeń 0); przerwania przycisków.
**Powiedz:** przerwania tylko zapisują czas i budzą zadanie; zero `delay()`; pamięć alokowana
statycznie; algorytm w osobnym pliku testowanym na komputerze (84 asercje).

### Slajd 6 – Algorytm: zużycie i trendy
**Na slajdzie:** k = k_T · k_V (reguła 10 °C, L10 ~ obciążenie³); trend regresji w oknie 60 h z R²;
prognoza = min(zużycie, trend temperatury, trend drgań).
**Powiedz:** model fizyczny zamiast uczenia maszynowego – działa od pierwszej godziny i każdy wynik
da się wyjaśnić; trend liczy się tylko, gdy jest wiarygodny (R² ≥ 0,6).

### Slajd 7 – Algorytm: diagnoza i status
**Na slajdzie:** tabela przyczyn (łożysko / przegrzewanie / niewyważenie / wilgoć / przegląd / czujnik);
status OK / PLAN / KONTROLA; alarmy wg ISA-18.2 (kwitowanie, histereza, zwłoka).
**Powiedz:** te same drgania mogą znaczyć co innego – z rosnącą temperaturą to łożysko, przy stałej
temperaturze raczej niewyważenie. Za chwilę to pokażę.

---

## Demonstracja na żywo (3 min)

| Krok | Czas | Akcja | Co pokazać i powiedzieć |
|---|---|---|---|
| 1 | 0:00 | przełącznik RUN → praca | `RUN_START`, zielona lampka, OLED: stan OK, „kontrola za ~200 h (zużycie)” |
| 2 | 0:20 | MPU6050: *Acceleration X* = +0,10 g | v = 3,17 mm/s, strefa C → `VIB -> OSTRZEZENIE`, żółta lampka; `stan` → przyczyna **niewywazenie/luz** |
| 3 | 0:50 | skrypt rampy temperatury (Enter w konsoli przeglądarki) | prognoza zmienia się na `(trend temp.)`; po kilkunastu sekundach przyczyna **lozysko/smarowanie**, choć temperatura < 50 °C – korelacja parametrów |
| 4 | 1:40 | drugi monitor/karta: MQTTX | zdarzenia `LEVEL` i telemetria JSON (pola `state`, `cause`, `insp_h`) – dane trafiają do systemu nadrzędnego |
| 5 | 2:10 | DHT22: temperatura 71 °C | `TEMP -> ALARM`, `STAN -> KONTROLA`, czerwona lampka miga, buzzer „pip-pip” |
| 6 | 2:30 | przycisk **ACK** (lub `ack`) | czerwona świeci ciągle, buzzer milknie – alarm potwierdzony, ale nadal aktywny (ISA-18.2) |
| 7 | 2:45 | temperatura 40 °C, X = 0, **przytrzymanie SERWIS 3 s** | `SERVICE_DONE`, od razu OK i „kontrola za ~200 h” |

**Jeśli coś pójdzie nie tak:**
- MQTT się nie łączy (sieć sali) → system działa lokalnie, pokazać zdarzenia w monitorze portu i nagranie.
- Symulacja „stoi” → sprawdzić, czy karta jest widoczna; w ostateczności nagranie.
- Brak czasu → pominąć krok 4 (MQTT) i 5 (alarm), zostawić predykcję (krok 3) – najważniejszą dla 5.0.

---

### Slajd 8 – Trudności i ich rozwiązania
**Na slajdzie (4 przykłady):**
1. **Fałszywy alarm „kontrola za 1 h” przy zatrzymanej maszynie** – niemożliwy według logiki.
   Hipoteza → test izolowany → przyczyna: Wokwi nie zachowuje rejestrów FPU przy przełączaniu zadań
   na tym samym rdzeniu (11 475/11 475 błędnych obliczeń). Rozwiązanie: akwizycja tylko na liczbach
   całkowitych + monitor integralności FPU: **105 błędów → 0**.
2. DHT22 milczał na GPIO4 → zmiana pinu na GPIO16 (ustalone eksperymentalnie).
3. Drgania styków: 40–100 przerwań na kliknięcie → filtracja w ISR + zatwierdzenie po 30 ms ciszy.
4. Odczyty MPU6050 = 0 → nazwa `word` jest w Arduino makrem.

**Powiedz:** pierwszy przykład to najciekawsza część pracy inżynierskiej – od objawu, przez wykluczanie
hipotez, do pomiaru, który potwierdził skuteczność poprawki.

### Slajd 9 – Testy i wyniki
**Na slajdzie:** 8 scenariuszy (T1–T8) – wszystkie zaliczone ([TESTY.md](TESTY.md)); 84 asercje testów
jednostkowych; najważniejszy wynik: **PLAN przy 52 °C – 8 °C przed progiem ostrzeżenia** (T4);
zasoby: 198 KB wolnej sterty, 1 pominięty takt na 38 800 próbek.
**Powiedz:** testy obejmują przypadki standardowe, brzegowe (restart, skok w środku okna) i awaryjne
(usterka czujnika).

### Slajd 10 – Dalszy rozwój i podsumowanie
**Na slajdzie:** FFT i analiza obwiedni (częstotliwości łożyskowe); prąd silnika; uczenie linii bazowej
i wykrywanie anomalii; panel Grafana, integracja z CMMS (automatyczne zlecenie przeglądu); TLS i OTA.
**Powiedz (zamknięcie):** „System mierzy, łączy parametry, przewiduje termin kontroli i mówi, co jest
prawdopodobną przyczyną – a cała logika jest przetestowana i udokumentowana.”

---

## Możliwe pytania i krótkie odpowiedzi

| Pytanie | Odpowiedź |
|---|---|
| Dlaczego nie uczenie maszynowe? | Brak danych o awariach do uczenia; model fizyczny działa od razu i jest wyjaśnialny. ML ma sens jako uzupełnienie (wykrywanie anomalii) przy flocie maszyn. |
| Skąd prędkość drgań z akcelerometru? | v = a / (2π f) dla dominującej składowej 1× obroty (49,2 Hz); w realnym wdrożeniu FFT lub całkowanie w paśmie. |
| Po co przyspieszony czas? | Żeby demonstracja trwała minuty; we wdrożeniu `kTimeScale = 1`, algorytm się nie zmienia. |
| Co, jeśli zabraknie sieci? | Pomiary, analiza i sygnalizacja działają lokalnie; MQTT łączy się ponownie z odstępem wykładniczym, a liczniki są w pamięci nieulotnej. |
| Jak uniknięto fałszywych alarmów? | Zwłoka 2 ocen, histereza, potwierdzanie zmian statusu, R² dla trendów, monitor FPU. |
| Czy błąd FPU wystąpi na prawdziwym ESP32? | ESP-IDF zapisuje rejestry FPU przy przełączaniu zadań; problem dotyczy symulatora. Rozwiązanie i tak jest korzystne na sprzęcie: mniej przełączeń kontekstu FPU w zadaniu 100 Hz. |
| Bezpieczeństwo MQTT? | Publiczny broker tylko do demonstracji; docelowo TLS, uwierzytelnienie, ACL. Serwisu celowo nie da się potwierdzić zdalnie. |
