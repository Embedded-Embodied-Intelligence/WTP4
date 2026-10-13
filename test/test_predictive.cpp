/**
 * @file  test_predictive.cpp
 * @brief Testy jednostkowe logiki predykcyjnej (predictive.h) uruchamiane na komputerze.
 *
 * Kompilacja i uruchomienie (z katalogu głównego repozytorium):
 * @code
 *   c++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -fsanitize=address,undefined \
 *       -o test_predictive test/test_predictive.cpp && ./test_predictive
 * @endcode
 * Program kończy się kodem 0, gdy wszystkie asercje są spełnione.
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../predictive.h"

using namespace pdm;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond)                                                         \
  do {                                                                      \
    if (cond) {                                                             \
      ++g_passed;                                                           \
    } else {                                                                \
      ++g_failed;                                                           \
      std::printf("  BLAD %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
    }                                                                       \
  } while (0)

#define CHECK_NEAR(actual, expected, tol)                                                          \
  do {                                                                                             \
    const double a_ = (actual), e_ = (expected);                                                   \
    if (std::fabs(a_ - e_) <= (tol)) {                                                             \
      ++g_passed;                                                                                  \
    } else {                                                                                       \
      ++g_failed;                                                                                  \
      std::printf("  BLAD %s:%d: %s = %.6f, oczekiwano %.6f (+/- %g)\n", __FILE__, __LINE__, #actual, a_, e_, \
                  static_cast<double>(tol));                                                       \
    }                                                                                              \
  } while (0)

static void section(const char* name) { std::printf("[TEST] %s\n", name); }

// ---------------------------------------------------------------------------
static void testRingBuffer() {
  section("RingBuffer: kolejnosc, nadpisywanie najstarszych");
  RingBuffer<int, 4> rb;
  CHECK(rb.empty());
  for (int i = 1; i <= 3; ++i) rb.push(i);
  CHECK(rb.size() == 3 && rb.oldest() == 1 && rb.newest() == 3);
  for (int i = 4; i <= 6; ++i) rb.push(i);  // bufor 4-elementowy: zostaje 3,4,5,6
  CHECK(rb.full() && rb.size() == 4);
  CHECK(rb.at(0) == 3 && rb.at(1) == 4 && rb.at(2) == 5 && rb.at(3) == 6);
  rb.clear();
  CHECK(rb.empty());
}

// Regresja liniowa liczona wprost (wzorzec do porównań)
static void bruteForceOls(const float* y, int n, double& slope, double& r2) {
  double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
  for (int i = 0; i < n; ++i) {
    sx += i;
    sy += y[i];
    sxx += static_cast<double>(i) * i;
    sxy += static_cast<double>(i) * y[i];
    syy += static_cast<double>(y[i]) * y[i];
  }
  const double cov = n * sxy - sx * sy, vx = n * sxx - sx * sx, vy = n * syy - sy * sy;
  slope = cov / vx;
  r2 = (vy > 0) ? cov * cov / (vx * vy) : 0.0;
}

static void testLinearTrend() {
  section("LinearTrend: prosta dokladna, okno przesuwne O(1) == pelne przeliczenie");
  LinearTrend<60> lt(2.0f);  // odstep 2 h
  for (int i = 0; i < 150; ++i) lt.add(10.0f + 0.5f * static_cast<float>(i));  // 0,5 na probke -> 0,25 na godzine
  CHECK(lt.count() == 60);
  CHECK_NEAR(lt.slopePerHour(), 0.25, 1e-4);
  CHECK_NEAR(lt.r2(), 1.0, 1e-6);
  CHECK_NEAR(lt.fittedLast(), 10.0 + 0.5 * 149, 1e-3);

  // Dane pseudolosowe: aktualizacja O(1) musi dawac to samo co liczenie wprost
  LinearTrend<20> lt2(1.0f);
  float window[20];
  unsigned seed = 12345;
  for (int i = 0; i < 1000; ++i) {
    seed = seed * 1103515245u + 12345u;
    const float y = 50.0f + 0.03f * static_cast<float>(i) + static_cast<float>((seed >> 16) % 1000) / 100.0f;
    lt2.add(y);
    window[i % 20] = y;
  }
  float ordered[20];
  for (int i = 0; i < 20; ++i) ordered[i] = window[(1000 + i) % 20];  // od najstarszej
  double slope, r2;
  bruteForceOls(ordered, 20, slope, r2);
  CHECK_NEAR(lt2.slopePerHour(), slope, 1e-4);
  CHECK_NEAR(lt2.r2(), r2, 1e-4);

  section("LinearTrend: prognoza czasu do progu");
  LinearTrend<60> t(1.0f);
  for (int i = 0; i < 20; ++i) t.add(40.0f + 1.0f * static_cast<float>(i));  // 1 °C/h, ostatnia 59 °C
  CHECK_NEAR(t.hoursToReach(70.0f, 15, 0.6f, 0.2f), 11.0, 1e-3);
  CHECK(std::isinf(t.hoursToReach(70.0f, 30, 0.6f, 0.2f)));  // za malo probek
  CHECK(t.hoursToReach(50.0f, 15, 0.6f, 0.2f) == 0.0f);      // prog juz osiagniety

  section("LinearTrend: sygnal staly -> brak trendu");
  LinearTrend<60> flat(1.0f);
  for (int i = 0; i < 60; ++i) flat.add(40.0f);
  CHECK_NEAR(flat.slopePerHour(), 0.0, 1e-6);
  CHECK(flat.r2() == 0.0f);
  CHECK(std::isinf(flat.hoursToReach(70.0f, 15, 0.6f, 0.2f)));
}

static void testAlarmChannel() {
  section("AlarmChannel: zwloka zadzialania i histereza");
  AlarmChannel ch(Thresholds{60.0f, 70.0f, 2.0f, 2});
  CHECK(!ch.update(61.0f) && ch.level() == Level::kNormal);  // 1. ocena powyzej progu – czekamy
  CHECK(ch.update(61.0f) && ch.level() == Level::kWarning);  // 2. ocena – eskalacja
  CHECK(!ch.update(59.0f) && ch.level() == Level::kWarning);  // 59 > 60-2: histereza trzyma
  CHECK(ch.update(57.9f) && ch.level() == Level::kNormal);    // ponizej 58 – powrot
  CHECK(!ch.update(NAN) && ch.level() == Level::kNormal);     // brak danych nie zmienia poziomu

  section("AlarmChannel: stany ISA-18.2");
  ch.update(75.0f);
  ch.update(75.0f);
  CHECK(ch.level() == Level::kAlarm && ch.state() == AlarmState::kUnackActive);
  CHECK(ch.acknowledge() && ch.state() == AlarmState::kAckActive);
  ch.update(50.0f);  // ustapienie potwierdzonego alarmu
  CHECK(ch.level() == Level::kNormal && ch.state() == AlarmState::kNormal);
  ch.update(75.0f);
  ch.update(75.0f);
  ch.update(50.0f);  // ustapienie BEZ potwierdzenia
  CHECK(ch.state() == AlarmState::kUnackCleared && ch.unacknowledged());
  CHECK(ch.acknowledge() && ch.state() == AlarmState::kNormal);
  CHECK(!ch.acknowledge());  // nic do potwierdzenia
}

static void testWearModel() {
  section("WearModel: regula 10 °C i zaleznosc L10 ~ obciazenie^3");
  WearModel w;
  CHECK_NEAR(w.tempFactor(40.0f), 1.0, 1e-6);
  CHECK_NEAR(w.tempFactor(50.0f), 2.0, 1e-5);
  CHECK_NEAR(w.tempFactor(60.0f), 4.0, 1e-5);
  CHECK_NEAR(w.tempFactor(90.0f), 16.0, 1e-5);  // ograniczenie
  CHECK_NEAR(w.tempFactor(20.0f), 1.0, 1e-6);   // ponizej odniesienia – brak przyspieszenia
  CHECK_NEAR(w.vibFactor(2.0f), 1.0, 1e-6);
  CHECK_NEAR(w.vibFactor(5.6f), 8.0, 1e-4);

  section("WearModel: calkowanie zuzycia i prognoza do przegladu");
  for (int i = 0; i < 10; ++i) w.update(1.0f, true, 50.0f, 1.0f);  // 10 h przy 2x
  CHECK_NEAR(w.equivalentHours(), 20.0, 1e-4);
  CHECK_NEAR(w.hoursToService(), (200.0 - 20.0) / 2.0, 1e-3);
  w.update(5.0f, false, 80.0f, 10.0f);  // postoj – bez zuzycia
  CHECK_NEAR(w.equivalentHours(), 20.0, 1e-4);
  w.onStart();
  CHECK_NEAR(w.equivalentHours(), 20.5, 1e-4);
  w.reset();
  CHECK_NEAR(w.lifeFraction(), 0.0, 1e-6);
  CHECK_NEAR(w.rate(), 1.0, 1e-6);    // po przegladzie brak pamieci tempa sprzed przegladu
  w.update(1.0f, true, 60.0f, 1.0f);
  CHECK_NEAR(w.rate(), 4.0, 1e-5);    // pierwszy pomiar inicjalizuje EWMA bez opoznienia
}

static void testHealth() {
  section("computeHealth: warunki nominalne i pojedynczy parametr krytyczny");
  const Health ok = computeHealth(40.0f, true, 0.5f, true, 45.0f, true, 0.0f);
  CHECK_NEAR(ok.index, 100.0, 1e-3);
  const Health hot = computeHealth(70.0f, true, 0.5f, true, 45.0f, true, 0.0f);
  CHECK_NEAR(hot.temp, 0.2, 1e-4);
  CHECK(hot.index <= 50.0f + 1e-3f);  // ograniczenie: najgorsza ocena + 0,3
  const Health noDht = computeHealth(NAN, false, 0.5f, true, NAN, false, 0.5f);
  CHECK(noDht.index > 0.0f && noDht.index <= 100.0f);  // czujnik niedostepny – bez NaN
  CHECK(isoZone(1.0f) == 'A' && isoZone(2.0f) == 'B' && isoZone(5.0f) == 'C' && isoZone(8.0f) == 'D');
}

static EngineInput input(bool running, float t, float v, float rh) {
  EngineInput in;
  in.running = running;
  in.workHours = running ? 1.0f : 0.0f;
  in.tempC = t;
  in.tempValid = true;
  in.vibMmS = v;
  in.vibValid = true;
  in.humidityPct = rh;
  in.humidityValid = true;
  return in;
}

static void testEngine() {
  section("PredictiveEngine: praca nominalna -> OK, prognoza z modelu zuzycia");
  PredictiveEngine e;
  e.onMachineStart();  // +0,5 h za rozruch
  for (int i = 0; i < 30; ++i) e.step(input(true, 40.0f, 0.5f, 45.0f), nullptr, 0);
  CHECK(e.output().status == Level::kNormal);
  CHECK(e.output().cause == Cause::kNone);
  CHECK(e.output().driver == Driver::kWear);
  CHECK_NEAR(e.output().hoursToInspection, 200.0 - 30.5, 1e-2);

  section("PredictiveEngine: zuzycie 85 % -> PLAN, 100 % -> KONTROLA (eskalacja potwierdzona w 2 krokach)");
  e.wear().setEquivalentHours(170.0f);
  e.step(input(true, 40.0f, 0.5f, 45.0f), nullptr, 0);
  CHECK(e.output().status == Level::kNormal);  // 1. krok – czekamy na potwierdzenie
  e.step(input(true, 40.0f, 0.5f, 45.0f), nullptr, 0);
  CHECK(e.output().status == Level::kWarning && e.output().cause == Cause::kWear);
  e.wear().setEquivalentHours(200.0f);
  LevelChange ch[8];
  size_t n = e.step(input(true, 40.0f, 0.5f, 45.0f), ch, 8);
  CHECK(e.output().status == Level::kWarning && n == 0);
  n = e.step(input(true, 40.0f, 0.5f, 45.0f), ch, 8);
  CHECK(e.output().status == Level::kAlarm);
  CHECK(n == 1 && ch[0].param == Param::kMaint && ch[0].to == Level::kAlarm);
  CHECK(e.channel(Param::kMaint).state() == AlarmState::kUnackActive);
  CHECK(e.acknowledgeAll() == 1 && !e.anyUnacknowledged());
  e.serviceDone();
  CHECK(e.output().status == Level::kNormal && e.wear().equivalentHours() == 0.0f);

  section("PredictiveEngine: przeglad po przegrzaniu -> OK od razu (filtry EWMA bez pamieci)");
  PredictiveEngine sv;
  for (int i = 0; i < 40; ++i) sv.step(input(true, 72.0f, 0.5f, 45.0f), nullptr, 0);  // zuzycie x9,2
  for (int i = 0; i < 3; ++i) sv.step(input(true, 40.0f, 0.5f, 45.0f), nullptr, 0);   // powrot do normy
  CHECK(sv.output().status == Level::kAlarm && sv.wear().rate() > 4.2f);  // EWMA pamieta przegrzanie
  sv.serviceDone();
  sv.step(input(true, 40.0f, 0.5f, 45.0f), nullptr, 0);
  CHECK(sv.output().status == Level::kNormal);
  CHECK_NEAR(sv.output().wearRate, 1.0, 1e-6);
  CHECK_NEAR(sv.output().hoursToInspection, 199.0, 1e-3);  // (200 - 1 h pracy) / tempo 1,0
  CHECK(sv.output().health.index > 95.0f);

  section("PredictiveEngine: rosnaca temperatura -> trend, PLAN, przyczyna THERMAL");
  PredictiveEngine th;
  for (int i = 0; i < 20; ++i) th.step(input(true, 40.0f + static_cast<float>(i), 0.5f, 45.0f), nullptr, 0);  // 40..59 °C, 1 °C/h
  CHECK_NEAR(th.output().tempSlope, 1.0, 1e-3);
  CHECK_NEAR(th.output().tempHoursToAlarm, 11.0, 0.05);  // (70 - 59) / 1
  CHECK(th.output().driver == Driver::kTempTrend);
  CHECK(th.output().status == Level::kWarning);
  CHECK(th.output().cause == Cause::kThermal);
  for (int i = 20; i < 32; ++i) th.step(input(true, 40.0f + static_cast<float>(i), 0.5f, 45.0f), nullptr, 0);  // do 71 °C
  CHECK(th.channel(Param::kTemp).level() == Level::kAlarm);
  CHECK(th.output().status == Level::kAlarm);
  CHECK(th.worstAlarmState() == AlarmState::kUnackActive);

  section("PredictiveEngine: drgania + wzrost temperatury -> BEARING");
  PredictiveEngine br;
  for (int i = 0; i < 20; ++i) br.step(input(true, 40.0f + 0.5f * static_cast<float>(i), 3.5f, 45.0f), nullptr, 0);
  CHECK(br.channel(Param::kVib).level() == Level::kWarning);
  CHECK(br.output().cause == Cause::kBearing);
  CHECK(br.output().zone == 'C');

  section("PredictiveEngine: drgania przy stabilnej temperaturze -> MECHANICAL");
  PredictiveEngine me;
  for (int i = 0; i < 20; ++i) me.step(input(true, 40.0f, 4.0f, 45.0f), nullptr, 0);
  CHECK(me.output().cause == Cause::kMechanical);
  CHECK(me.output().wearRate > 2.5f);  // (4/2,8)^3 = 2,9

  section("PredictiveEngine: wilgotnosc -> ENVIRONMENT; usterka czujnika -> SENSOR");
  PredictiveEngine hu;
  for (int i = 0; i < 3; ++i) hu.step(input(true, 40.0f, 0.5f, 85.0f), nullptr, 0);
  CHECK(hu.channel(Param::kHum).level() == Level::kWarning && hu.output().cause == Cause::kEnvironment);
  EngineInput faulty = input(true, 40.0f, 0.5f, 45.0f);
  faulty.sensorFault = true;
  faulty.tempValid = faulty.humidityValid = false;
  hu.step(faulty, nullptr, 0);
  CHECK(hu.output().cause == Cause::kSensor);
  CHECK(hu.channel(Param::kSensor).state() == AlarmState::kUnackActive);

  section("PredictiveEngine: pojedynczy zaklocony krok nie zmienia statusu (brak alarmu ulotnego)");
  PredictiveEngine gl;
  for (int i = 0; i < 5; ++i) gl.step(input(false, 40.0f, 0.0f, 45.0f), nullptr, 0);
  gl.wear().setEquivalentHours(199.5f);  // jeden krok z prognoza <= 8 h (jak zaklocenie w Wokwi)
  n = gl.step(input(false, 40.0f, 0.0f, 45.0f), ch, 8);
  gl.wear().setEquivalentHours(0.0f);
  size_t n2 = gl.step(input(false, 40.0f, 0.0f, 45.0f), ch, 8);
  CHECK(n == 0 && n2 == 0);
  CHECK(gl.output().status == Level::kNormal && !gl.anyUnacknowledged());

  section("PredictiveEngine: antyoscylacja – obnizenie statusu dopiero po potwierdzeniu");
  PredictiveEngine hy;
  for (int i = 0; i < 3; ++i) hy.step(input(true, 40.0f, 0.5f, 85.0f), nullptr, 0);  // PLAN (wilgotnosc)
  CHECK(hy.output().status == Level::kWarning);
  int steps = 0;
  while (hy.output().status != Level::kNormal && steps < 20) {
    hy.step(input(true, 40.0f, 0.5f, 45.0f), nullptr, 0);
    ++steps;
  }
  CHECK(steps == hy.config().statusHoldSteps);
}

int main() {
  testRingBuffer();
  testLinearTrend();
  testAlarmChannel();
  testWearModel();
  testHealth();
  testEngine();
  std::printf("\nWynik: %d asercji spelnionych, %d niespelnionych\n", g_passed, g_failed);
  return g_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
