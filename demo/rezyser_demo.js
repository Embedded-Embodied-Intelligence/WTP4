/*
 * Reżyser demonstracji WTP4 – prowadzi 3-minutową demonstrację w Wokwi automatycznie, z podpisami.
 *
 * Użycie (szczegóły: demo/INSTRUKCJA_NAGRANIA.md):
 *   1. W Wokwi uruchom symulację projektu i poczekaj, aż w monitorze portu pojawią się linie stanu.
 *   2. Otwórz konsolę przeglądarki (Chrome: Cmd+Opt+J / Ctrl+Shift+J), wklej CAŁY ten plik, Enter.
 *   3. Skrypt przygotuje układ (ok. 15 s), odliczy 5 s i wykona scenariusz 0:00–3:00.
 *   Zatrzymanie w dowolnej chwili:  rezyser.stop()      Ponowny start:  rezyser.start()
 *
 * Jak to działa: skrypt steruje elementami Wokwi tak jak użytkownik – przełącza RUN, otwiera panele
 * czujników i przesuwa suwaki, naciska przyciski ACK/SERWIS, wpisuje polecenia konsoli. Nie zmienia
 * programu ESP32. Oś czasu sprawdzona symulacją silnika predictive.h (zużycie przyspiesza przy wysokiej
 * temperaturze i drganiach, więc status KONTROLA pojawia się ok. 1:35 – bez przekroczenia progów).
 */
(() => {
  'use strict';
  if (window.rezyser && window.rezyser.stop) window.rezyser.stop();

  // ------------------------------------------------------------------ oś czasu (sekundy od 0:00)
  const KROKI = [
    { t: 0, tytul: 'Uruchomienie maszyny',
      opis: 'Przełącznik RUN → praca. Zielona lampka świeci ciągle: status OK, prognoza kontroli ok. 200 h (interwał przeglądu).',
      akcja: async () => { await ustawRun(true); } },
    { t: 20, tytul: 'Drgania +0,10 g',
      opis: 'v ≈ 3,2 mm/s – strefa C wg ISO 10816 → ostrzeżenie drgań, żółta lampka (PLAN). Przyczyna: niewyważenie / luz.',
      akcja: async () => { await ustawDrgania(0.10); },
      potem: [{ t: 36, akcja: () => polecenie('stan') }] },
    { t: 50, tytul: 'Rośnie temperatura łożyska',
      opis: 'Te same drgania + rosnąca temperatura → przyczyna: łożysko / smarowanie. Zużycie przyspiesza ×3–4, prognoza kontroli maleje.',
      akcja: async () => { await rampaTemperatury(40, 56, 2500); } },
    { t: 98, tytul: 'KONTROLA – bez przekroczenia progów',
      opis: 'Prognoza spadła do 0 h → status KONTROLA (czerwona miga, sygnał), choć T < 60 °C i v < 7,1 mm/s. To jest predykcja.',
      akcja: async () => { polecenie('stan'); } },
    { t: 120, tytul: 'Alarm temperatury 71 °C',
      opis: 'Przekroczony próg alarmu temperatury → nowy alarm parametru, niepotwierdzony (ISA-18.2).',
      akcja: async () => { await ustawTemperature(71); } },
    { t: 140, tytul: 'Kwitowanie i dziennik zdarzeń',
      opis: 'ACK potwierdza alarmy: czerwona świeci ciągle, sygnał milknie. Te same zdarzenia system wysyła przez MQTT.',
      akcja: async () => { await nacisnij('ACK', 150); },
      potem: [{ t: 146, akcja: () => polecenie('zdarzenia 8') }] },
    { t: 158, tytul: 'Serwis',
      opis: 'Warunki wracają do normy, przytrzymanie SERWIS 3 s → zużycie 0 %, od razu status OK i prognoza ok. 200 h.',
      akcja: async () => { await ustawTemperature(40); await ustawDrgania(0); await czekaj(2500); await nacisnij('SERWIS', 3300); } },
  ];
  const KONIEC = 180;

  // ------------------------------------------------------------------ narzędzia
  let przerwano = false;
  const timery = [];
  const czekaj = (ms) => new Promise((r) => { timery.push(setTimeout(r, ms)); });
  const log = (...a) => console.log('%c[reżyser]', 'color:#087E8B;font-weight:bold', ...a);

  function monitor() {
    const kandydaci = [...document.querySelectorAll('[class*="serial-monitor"], [class*="serialMonitor"], pre, div')]
      .filter((el) => el.children.length < 400 && /mth /.test(el.textContent) && el.scrollHeight > el.clientHeight + 5);
    return kandydaci.sort((a, b) => a.textContent.length - b.textContent.length)[0] || null;
  }
  function ostatniaLiniaStanu() {
    const el = monitor();
    if (!el) return '';
    const linie = el.innerText.split('\n').filter((l) => /\] (PRACA|POSTÓJ) /.test(l));
    return linie[linie.length - 1] || '';
  }
  const maszynaPracuje = () => /\] PRACA /.test(ostatniaLiniaStanu());

  function zdarzenie(el, typ) {
    const r = el.getBoundingClientRect();
    el.dispatchEvent(new MouseEvent(typ, { bubbles: true, cancelable: true, composed: true, view: window,
      button: 0, clientX: r.x + r.width / 2, clientY: r.y + r.height / 2 }));
  }
  const stuknij = (el) => ['pointerdown', 'mousedown', 'pointerup', 'mouseup', 'click'].forEach((t) => zdarzenie(el, t));
  const suwaki = () => [...document.querySelectorAll('input[type=range]')];
  const suwak = (min, max, nr = 0) => suwaki().filter((s) => s.min === String(min) && s.max === String(max))[nr];

  async function otworzPanel(czujnik) {
    const tag = czujnik === 'dht' ? 'wokwi-dht22' : 'wokwi-mpu6050';
    const gotowy = () => (czujnik === 'dht' ? suwak(-40, 80) : suwak(-2, 2));
    for (let proba = 0; proba < 3 && !gotowy(); ++proba) {
      if (suwaki().length) { stuknij(document.querySelector(tag)); await czekaj(400); }  // zamknij inny panel
      stuknij(document.querySelector(tag));                                                // otwórz właściwy
      await czekaj(600);
    }
    if (!gotowy()) throw new Error('nie udało się otworzyć panelu ' + tag);
  }
  function ustawSuwak(s, wartosc) {
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(s, String(wartosc));
    s.dispatchEvent(new Event('input', { bubbles: true }));
    s.dispatchEvent(new Event('change', { bubbles: true }));
  }
  async function ustawTemperature(v) { await otworzPanel('dht'); ustawSuwak(suwak(-40, 80), v); log('temperatura', v); }
  async function ustawDrgania(g) { await otworzPanel('mpu'); ustawSuwak(suwak(-2, 2, 0), g); log('przyspieszenie X', g); }
  async function rampaTemperatury(od, doT, okresMs) {
    await otworzPanel('dht');
    for (let t = od; t <= doT && !przerwano; ++t) { ustawSuwak(suwak(-40, 80), t); await czekaj(okresMs); }
  }
  function polecenie(tekst) {
    const pole = document.querySelector('[class*="serialControls"] input') || document.querySelector('input:not([type=range])');
    if (!pole) { log('brak pola konsoli'); return; }
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(pole, tekst);
    pole.dispatchEvent(new Event('input', { bubbles: true }));
    const o = { key: 'Enter', code: 'Enter', keyCode: 13, which: 13, bubbles: true, cancelable: true };
    ['keydown', 'keypress', 'keyup'].forEach((t) => pole.dispatchEvent(new KeyboardEvent(t, o)));
    log('konsola:', tekst);
  }
  async function ustawRun(praca) {
    if (maszynaPracuje() === praca) return;
    document.querySelector('wokwi-slide-switch').onClick();  // przełącza suwak jak kliknięcie użytkownika
    await czekaj(1500);
    if (maszynaPracuje() !== praca) log('UWAGA: przełącznik RUN nie zmienił stanu – przełącz go ręcznie');
  }
  async function nacisnij(etykieta, ms) {
    const b = [...document.querySelectorAll('wokwi-pushbutton')].find((x) => x.label === etykieta);
    b.down();
    await czekaj(ms);
    b.up();
    log('przycisk', etykieta, ms + ' ms');
  }

  // ------------------------------------------------------------------ podpisy (nakładka)
  const pasek = document.createElement('div');
  pasek.id = 'rezyser-pasek';
  pasek.style.cssText = 'position:fixed;top:56px;left:calc(50% + 12px);right:12px;z-index:99999;pointer-events:none;' +
    'background:rgba(11,57,84,0.94);color:#fff;border-radius:10px;padding:12px 18px 14px;' +
    'font:15px/1.35 "Helvetica Neue",Helvetica,Arial,sans-serif;box-shadow:0 6px 18px rgba(0,0,0,.35)';
  pasek.innerHTML = '<div id="rz-meta" style="font-size:13px;color:#7FDBDA;letter-spacing:.04em"></div>' +
    '<div id="rz-tytul" style="font-size:22px;font-weight:700;margin:2px 0 4px"></div>' +
    '<div id="rz-opis"></div>' +
    '<div style="height:5px;background:rgba(255,255,255,.2);border-radius:3px;margin-top:10px">' +
    '<div id="rz-postep" style="height:5px;width:0;background:#7FDBDA;border-radius:3px"></div></div>';
  document.body.appendChild(pasek);
  const el = (id) => pasek.querySelector('#' + id);
  const mmss = (s) => Math.floor(s / 60) + ':' + String(Math.floor(s % 60)).padStart(2, '0');
  function podpis(meta, tytul, opis) { el('rz-meta').textContent = meta; el('rz-tytul').textContent = tytul; el('rz-opis').textContent = opis; }

  // ------------------------------------------------------------------ przebieg
  // Ukryta karta = zatrzymana symulacja (przeglądarka dławi kartę w tle), a oś czasu biegłaby dalej.
  const UKRYTA = 'karta Wokwi jest ukryta lub zasłonięta – symulacja stoi. Odsłoń okno i uruchom: rezyser.start()';
  function przyUkryciu() {
    if (document.visibilityState !== 'hidden' || przerwano) return;
    przerwano = true;
    timery.forEach((t) => { clearTimeout(t); clearInterval(t); });
    podpis('PRZERWANO', 'Karta Wokwi została ukryta', UKRYTA);
    log('przerwano:', UKRYTA);
  }
  document.addEventListener('visibilitychange', przyUkryciu);

  async function przygotuj() {
    podpis('PRZYGOTOWANIE', 'Ustawiam warunki początkowe…', 'temperatura 40 °C, drgania 0 g, serwis (zużycie 0 %), maszyna zatrzymana');
    if (document.visibilityState === 'hidden') throw new Error(UKRYTA);
    if (!ostatniaLiniaStanu()) throw new Error('brak linii stanu w monitorze portu – czy symulacja działa?');
    await ustawDrgania(0);
    await ustawTemperature(40);
    await ustawRun(false);
    polecenie('serwis');
    await czekaj(6000);  // nowy odczyt DHT22 i ustalenie stanu
  }

  async function start() {
    przerwano = false;
    if (!pasek.isConnected) document.body.appendChild(pasek);  // ponowny start po stop()
    document.addEventListener('visibilitychange', przyUkryciu);   // (ponowne dodanie nie dubluje)
    el('rz-postep').style.width = '0';
    try {
      await przygotuj();
      for (let i = 5; i > 0 && !przerwano; --i) { podpis('START ZA ' + i + ' s', 'Demonstracja: inteligentny system utrzymania ruchu', 'Oś czasu 3:00 – podpisy opisują, co widać na schemacie i w monitorze portu.'); await czekaj(1000); }
      const t0 = performance.now();
      const teraz = () => (performance.now() - t0) / 1000;
      const zegar = setInterval(() => { el('rz-postep').style.width = Math.min(100, (teraz() / KONIEC) * 100) + '%'; }, 250);
      timery.push(zegar);
      // akcje dodatkowe (np. polecenie "stan") planowane niezależnie od akcji głównych
      KROKI.forEach((k) => (k.potem || []).forEach((p) => timery.push(setTimeout(() => !przerwano && p.akcja(), p.t * 1000))));
      for (let i = 0; i < KROKI.length && !przerwano; ++i) {
        const k = KROKI[i];
        const opoznienie = k.t - teraz();
        if (opoznienie > 0) await czekaj(opoznienie * 1000);
        if (przerwano) break;
        podpis('KROK ' + (i + 1) + '/' + KROKI.length + ' · ' + mmss(k.t), k.tytul, k.opis);
        log(mmss(teraz()), 'krok', i + 1, k.tytul);
        k.akcja().catch((e) => log('błąd akcji:', e.message));  // akcje długie (rampa) działają w tle
      }
      const reszta = KONIEC - teraz();
      if (reszta > 0) await czekaj(reszta * 1000);
      clearInterval(zegar);
      if (!przerwano) podpis('KONIEC · 3:00', 'Koniec demonstracji', 'Zużycie wyzerowane, status OK. Szczegóły: TESTY.md i DOKUMENTACJA.md.');
    } catch (e) {
      podpis('BŁĄD', 'Demonstracja przerwana', e.message);
      log('błąd:', e);
    }
  }

  function stop() {
    przerwano = true;
    timery.forEach((t) => { clearTimeout(t); clearInterval(t); });
    document.removeEventListener('visibilitychange', przyUkryciu);
    pasek.remove();
    log('zatrzymano');
  }

  window.rezyser = { start, stop, polecenie, ustawTemperature, ustawDrgania, nacisnij, ustawRun };
  start();
})();
