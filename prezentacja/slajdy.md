---
marp: true
theme: default
size: 16:9
paginate: true
math: katex
lang: pl
title: Inteligentny system utrzymania ruchu maszyn
description: WTP4 – predykcja terminu kontroli maszyny (ESP32, MQTT, Wokwi)
footer: WTP4 · Inteligentny system utrzymania ruchu maszyn
style: |
  section {
    font-family: "Helvetica Neue", Helvetica, Arial, sans-serif;
    font-size: 26px;
    color: #1F2933;
    background: #FFFFFF;
    padding: 46px 64px 54px;
  }
  section h1 {
    font-size: 42px;
    color: #0B3954;
    border-bottom: 4px solid #087E8B;
    padding-bottom: 6px;
    margin: 0 0 20px;
  }
  section h2 { font-size: 30px; color: #087E8B; margin: 0 0 10px; }
  section strong { color: #0B3954; }
  section h1 strong { color: #087E8B; }
  section.tytul strong { color: #FFFFFF; }
  .tabele table { font-size: 19px; margin: 0 0 16px; width: 100%; }
  section table { font-size: 21px; border-collapse: collapse; margin: 0 auto; }
  section th { background: #0B3954; color: #FFFFFF; }
  section td, section th { padding: 6px 12px; border-color: #D9E2EC; }
  section code { font-size: 0.9em; }
  section footer { color: #829AB1; font-size: 15px; }
  section::after { color: #829AB1; font-size: 15px; }
  section.tytul { background: #0B3954; color: #FFFFFF; justify-content: center; }
  section.tytul h1 { color: #FFFFFF; border: none; font-size: 56px; line-height: 1.15; }
  section.tytul h2 { color: #7FDBDA; font-size: 30px; }
  section.tytul p { color: #D9E2EC; font-size: 24px; }
  section.sekcja { background: #087E8B; color: #FFFFFF; justify-content: center; }
  section.sekcja h1 { color: #FFFFFF; border: none; font-size: 50px; }
  .ok { color: #2E7D32; font-weight: bold; }
  .plan { color: #B07D00; font-weight: bold; }
  .alarm { color: #C62828; font-weight: bold; }
  .kol2 { display: grid; grid-template-columns: 1fr 1fr; gap: 30px; align-items: start; }
  .kol3 { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 22px; }
  .karta { background: #F5F7FA; border-left: 6px solid #087E8B; padding: 12px 18px; border-radius: 6px; }
  .karta.zla { border-left-color: #C62828; }
  .karta.srednia { border-left-color: #E0A100; }
  .karta.dobra { border-left-color: #2E7D32; }
  .maly { font-size: 19px; color: #52606D; }
  .duzy { font-size: 34px; font-weight: bold; color: #0B3954; }
  img[alt~="srodek"] { display: block; margin: 0 auto; }
---

<!-- _class: tytul -->
<!-- _paginate: false -->
<!-- _footer: "" -->

# Inteligentny system<br>utrzymania ruchu maszyn

## Prognoza terminu kontroli · ESP32 · FreeRTOS · MQTT · Wokwi

Wyzwania Technologiczne Przemysłu 4.0 – projekt zaliczeniowy
Autor: [imię i nazwisko]

<!--
0:00–0:20
Pokażę system, który nie tylko alarmuje po przekroczeniu progu, ale przewiduje, kiedy maszyna będzie wymagała kontroli – i podaje dlaczego.
-->

---

# Kiedy serwisować maszynę?

<div class="kol3">
<div class="karta zla">

**Reaktywnie**
po awarii

<span class="maly">nieplanowany przestój, uszkodzenia wtórne, najwyższy koszt</span>

</div>
<div class="karta srednia">

**Prewencyjnie**
co stały okres (np. 200 h)

<span class="maly">serwis za wcześnie (koszt) albo za późno (awaria)</span>

</div>
<div class="karta dobra">

**Predykcyjnie**
gdy wskazuje na to **stan maszyny**

<span class="maly">wymaga pomiarów, analizy i komunikacji – Przemysł 4.0</span>

</div>
</div>

<br>

**Cel projektu:** mierzyć kilka parametrów pracy, łączyć je w ocenę stanu i **prognozować, za ile
godzin pracy maszyna powinna zostać poddana kontroli** – lokalnie i zdalnie (MQTT).

<!--
0:20–0:45
Przestój nieplanowany jest najdroższy; serwis „co 200 h” bywa za wcześnie albo za późno. Przemysł 4.0 to pomiar + analiza + komunikacja – i to realizuje ten projekt.
-->

---

# Założenia i realizacja wymagań

| Ocena | Wymaganie | Realizacja |
|:---:|---|---|
| 3.0 | jeden parametr | drgania – akcelerometr MPU6050, 100 Hz |
| 3.5 | dwa parametry | + temperatura, wilgotność (DHT22), czas pracy, liczba uruchomień |
| 4.0 | progi alarmowe | ostrzeżenie / alarm, histereza, zwłoka, kwitowanie wg ISA-18.2 |
| 4.5 | historia i zdarzenia | historia 10 min, dziennik zdarzeń, liczniki w pamięci nieulotnej, MQTT |
| **5.0** | **analiza wieloparametrowa + predykcja** | **model zużycia, trendy, diagnoza przyczyny, „kontrola za X h”** |

<span class="maly">Maszyna: pompa 2950 obr/min, klasa II wg ISO 10816. Czas przyspieszony: **1 s symulacji = 1 h pracy** –
cykl zużycia widać w kilka minut.</span>

<!--
0:45–1:30
Tabela pokazuje, że projekt pokrywa wszystkie progi. Przyspieszony czas: to, co w zakładzie trwa tygodnie, pokażę w trzy minuty – we wdrożeniu wystarczy zmienić jedną stałą.
-->

---

# Sprzęt (symulator Wokwi)

![bg right:46% fit](../docs/schemat_wokwi.png)

- **ESP32** – 2 rdzenie, Wi-Fi
- **MPU6050** – drgania, I2C0
- **DHT22** – temperatura, wilgotność
- **OLED** – osobna magistrala I2C1
- **kolumna LED**: <span class="ok">OK</span> · <span class="plan">PLAN</span> · <span class="alarm">alarm</span>
- **buzzer**, przyciski **ACK** i **SERWIS**, przełącznik **RUN**

<span class="maly">Osobna magistrala dla wyświetlacza – odświeżanie ekranu nie zakłóca pomiaru drgań.</span>

<!--
1:30–2:10
Na schemacie: w środku ESP32, u góry wyświetlacz i akcelerometr na dwóch osobnych magistralach, po lewej kolumna sygnalizacyjna, po prawej przyciski operatora, przełącznik pracy maszyny i czujnik temperatury.
-->

---

# Oprogramowanie: przerwania + FreeRTOS

![w:1080 srodek](img/przeplyw.png)

- przerwania tylko zapisują czas i **budzą zadanie** – zero `delay()` w pętli pracy
- pomiar ma najwyższy priorytet; sieć działa na **drugim rdzeniu**
- pamięć statyczna; algorytm w `predictive.h` – **testowany na komputerze (84 asercje)**

<!--
2:10–3:00
Timer 100 Hz budzi zadanie akwizycji, które co sekundę oddaje sumy okna drgań sterownikowi. Sterownik liczy analizę i obsługuje operatora, a komunikacja MQTT ma własny rdzeń – zawieszona sieć nie zatrzymuje pomiarów.
-->

---

# Algorytm (1): zużycie, trendy, prognoza

<div class="kol2">
<div>

**Zużycie** (reguła Palmgrena–Minera):

$$k = \underbrace{2^{\,(T-40)/10}}_{\text{reguła 10 °C}} \cdot \underbrace{\left(\tfrac{v}{2{,}8\ \mathrm{mm/s}}\right)^{3}}_{\text{łożyska L10}}$$

każda godzina pracy zużywa **k** godzin z interwału 200 h

**Trendy:** regresja w oknie 60 h, aktualizacja O(1), liczone tylko przy **R² ≥ 0,6**

</div>
<div>

**Prognoza terminu kontroli:**

$$t = \min\left(t_{\text{zużycie}},\ t_{\text{trend }T},\ t_{\text{trend }v}\right)$$

+ czynnik, który ją wyznaczył:
`zuzycie` · `trend temp.` · `trend drgan`

<span class="maly">Model fizyczny zamiast uczenia maszynowego: działa od pierwszej godziny, nie wymaga historii awarii, każdy wynik da się wyjaśnić.</span>

</div>
</div>

<!--
3:00–3:45
Każde 10 °C powyżej 40 podwaja tempo starzenia smaru i izolacji; drgania ponad granicę strefy B przyspieszają zużycie łożysk z trzecią potęgą. Trend uznajemy tylko wtedy, gdy jest wiarygodny – szum nie generuje fałszywych prognoz.
-->

---

# Algorytm (2): diagnoza i status

<div class="kol2">
<div>

![w:600](img/krok_analizy.png)

</div>
<div class="tabele">

| Objawy | Przyczyna |
|---|---|
| drgania + rosnąca temperatura | **łożysko / smarowanie** |
| tylko temperatura | przegrzewanie |
| tylko drgania | niewyważenie / luz |
| wilgotność ≥ 80 % | wilgoć / korozja |

| Status | Kiedy (wystarczy jeden warunek) |
|---|---|
| <span class="alarm">KONTROLA</span> | alarm, zużycie ≥ 100 %, prognoza ≤ 8 h |
| <span class="plan">PLAN</span> | ostrzeżenie, zużycie ≥ 80 %, prognoza ≤ 48 h |
| <span class="ok">OK</span> | brak zastrzeżeń |

</div>
</div>

<span class="maly">Podwyższenie statusu po 2 krokach, obniżenie po 5 – bez alarmów ulotnych i „migotania”.</span>

<!--
3:45–4:30
Te same drgania mogą znaczyć co innego: z rosnącą temperaturą to typowy objaw łożyska, przy stałej temperaturze raczej niewyważenie. Alarmy obsługuję wg ISA-18.2 – z kwitowaniem, histerezą i zwłoką, żeby nie było powodzi alarmów. Za chwilę to pokażę na żywo.
-->

---

# Demonstracja na żywo (3 min)

| Czas | Akcja w Wokwi | Co widać |
|:---:|---|---|
| 0:00 | przełącznik **RUN** → praca | zielona lampka, „kontrola za ~200 h” |
| 0:20 | drgania **+0,10 g** | strefa C, <span class="plan">ostrzeżenie</span>, przyczyna: niewyważenie |
| 0:50 | **temperatura rośnie** 40 → 56 °C | przyczyna: **łożysko**, zużycie ×3–4, prognoza maleje |
| ~1:35 | (samo) | <span class="alarm">KONTROLA</span> **bez przekroczenia progów** – predykcja |
| 2:00 | temperatura **71 °C** | alarm temperatury – przekroczony próg |
| 2:20 | przycisk **ACK**, dziennik zdarzeń | 2 alarmy potwierdzone; te same zdarzenia idą przez MQTT |
| 2:38 | **SERWIS** (3 s) | zużycie 0 %, od razu <span class="ok">OK</span> |

<span class="maly">Skrypt reżysera `demo/rezyser_demo.js` prowadzi te kroki automatycznie z podpisami · plan B: nagranie.</span>

<!--
4:30–7:30 – przełączam na kartę Wokwi. Skrypt reżysera (demo/rezyser_demo.js) może prowadzić demonstrację automatycznie z podpisami; w razie problemów z siecią sali – nagranie.
-->

---

# Trudności: fałszywy alarm, którego „nie mogło być”

<div class="kol2">
<div>

**Objaw:** „kontrola za 1 h (trend drgań)” przy **zatrzymanej** maszynie i pustym oknie trendu

**Diagnoza:** wykluczone hipotezy → szkic testowy z próbą kontrolną → **Wokwi nie zachowuje rejestrów FPU** przy przełączaniu zadań na tym samym rdzeniu (11 475 / 11 475 błędnych obliczeń)

**Rozwiązanie:** akwizycja tylko na liczbach całkowitych, `float` wyłącznie w sterowniku + monitor integralności FPU

</div>
<div>

![w:560](img/wykres_fpu.svg)

</div>
</div>

<span class="maly">Inne: DHT22 milczy na GPIO4 → GPIO16 · drgania styków 40–100 przerwań → filtr 30 ms · `word` to makro Arduino · spóźniony LWT → odświeżanie „online”.</span>

<!--
7:30–8:45
To najciekawsza część pracy inżynierskiej: objaw niemożliwy według logiki. Wykluczyłem dostęp z innych zadań i liczby zmiennoprzecinkowe w przerwaniach, postawiłem hipotezę o przełączaniu kontekstu FPU, potwierdziłem ją izolowanym testem z próbą kontrolną, a poprawkę zmierzyłem w docelowym programie: 105 błędów w 270 s przed, zero po. Na prawdziwym ESP32 to rozwiązanie też pomaga – zadanie 100 Hz nie przełącza kontekstu FPU.
-->

---

# Wynik kluczowy: kontrola wskazana **przed** progiem

![w:840 srodek](img/wykres_predykcja.svg)

<span class="maly">Scenariusz T4: temperatura rośnie 1 °C/h. Status PLAN przy **52 °C** – 8 °C przed progiem ostrzeżenia, bez żadnego alarmu parametru.</span>

<!--
8:45–9:10
To różnica między zwykłym progiem a predykcją: system wskazał potrzebę przeglądu, zanim cokolwiek przekroczyło próg, i podał przyczynę – trend temperatury.
-->

---

# Testy i wyniki

<div class="kol2">
<div>

| | Scenariusz | |
|:---:|---|:---:|
| T1 | praca, licznik motogodzin | ✅ |
| T2 | progi temperatury, ACK | ✅ |
| T3 | drgania, strefy ISO | ✅ |
| T4 | predykcja z trendu | ✅ |
| T5 | diagnoza: łożysko | ✅ |
| T6 | przegląd i serwis | ✅ |
| T7 | usterka czujnika | ✅ |
| T8 | historia, restart, NVS | ✅ |

</div>
<div>

<p class="duzy">8 / 8 scenariuszy</p>

**84** asercje testów jednostkowych

**0** błędów FPU w 34 658 kontrolach

**198 KB** wolnej sterty

**1** pominięty takt na 38 800 próbek

</div>
</div>

<!--
9:10–9:35
Scenariusze obejmują przypadki standardowe, brzegowe – restart, skok w środku okna pomiarowego – i awaryjne, jak odłączony czujnik. Wszystkie wyniki są w TESTY.md z fragmentami dzienników.
-->

---

# Dalszy rozwój

<div class="kol2">
<div>

**Analiza**
- FFT i analiza obwiedni – częstotliwości łożyskowe
- uczenie linii bazowej i wykrywanie anomalii (flota maszyn)
- prąd silnika, ciśnienie, przepływ

</div>
<div>

**Integracja i bezpieczeństwo**
- panel Grafana / Node-RED, baza szeregów czasowych
- CMMS: automatyczne zlecenie przeglądu
- MQTT przez TLS, uwierzytelnienie, aktualizacje OTA

</div>
</div>

<!--
9:35–9:50
Najbliższy krok to prawdziwy czujnik drgań z analizą widma – wtedy diagnoza łożyska opiera się na częstotliwościach charakterystycznych, a nie tylko na korelacji z temperaturą.
-->

---

<!-- _class: tytul -->
<!-- _paginate: false -->
<!-- _footer: "" -->

# Podsumowanie

## System mierzy · łączy parametry · przewiduje termin kontroli · wskazuje przyczynę

Kod, dokumentacja, testy: repozytorium projektu (README → DOKUMENTACJA → TESTY)

**Dziękuję – pytania?**

<!--
9:50–10:00
System mierzy, łączy parametry, przewiduje termin kontroli i mówi, co jest prawdopodobną przyczyną – a cała logika jest przetestowana i udokumentowana. Dziękuję.
-->

---

<!-- _class: sekcja -->
<!-- _footer: "" -->

# Materiały zapasowe

---

# Pytania – krótkie odpowiedzi

| Pytanie | Odpowiedź |
|---|---|
| Dlaczego nie uczenie maszynowe? | brak historii awarii do uczenia; model fizyczny działa od razu i jest wyjaśnialny |
| Skąd prędkość z akcelerometru? | v = a / (2π f) dla składowej 1× obroty (49,2 Hz); docelowo FFT |
| Po co przyspieszony czas? | demonstracja w minutach; we wdrożeniu `kTimeScale = 1` |
| Brak sieci? | pomiary, analiza i sygnalizacja lokalnie; MQTT wraca z odstępem wykładniczym |
| Fałszywe alarmy? | zwłoka, histereza, potwierdzanie statusu, R² trendów, monitor FPU |
| Błąd FPU na sprzęcie? | ESP-IDF zapisuje rejestry FPU; problem dotyczy symulatora – rozwiązanie i tak oszczędza przełączeń |
| Bezpieczeństwo MQTT? | publiczny broker tylko do demonstracji; docelowo TLS, uwierzytelnienie, ACL |

<!-- Slajd zapasowy – do pytań. -->

---

# Zadania i przerwania (szczegóły)

| Kontekst | Rdzeń | Priorytet | Zakres | FPU |
|---|:---:|:---:|---|:---:|
| ISR timera 100 Hz | 1 | – | powiadomienie akwizycji | nie |
| ISR GPIO (ACK, SERWIS, RUN) | 1 | – | znacznik czasu, powiadomienie | nie |
| akwizycja | 1 | 3 | MPU6050, sumy okna (Q8, `uint64`) | **nie** |
| sterownik `loop()` | 1 | 1 | DHT22, analiza, HMI, NVS, konsola | tak |
| komunikacja | 0 | 1 | Wi-Fi, MQTT, LWT | tak |

<span class="maly">Komunikacja między zadaniami: powiadomienia, skrzynki (najnowszy stan), kolejka zdarzeń FIFO, kolejka poleceń. Stan maszyny ma jednego właściciela – brak muteksów.</span>

<!-- Slajd zapasowy – do pytań o architekturę. -->
