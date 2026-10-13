/**
 * @file    predictive.h
 * @brief   Logika utrzymania ruchu: progi alarmowe, trendy, model zużycia i prognoza terminu kontroli.
 *
 * @details Moduł jest niezależny od sprzętu (czysty C++17, bez Arduino i FreeRTOS), dzięki czemu
 *          algorytmy można testować jednostkowo na komputerze (test/test_predictive.cpp).
 *          Nie używa sterty – wszystkie bufory mają rozmiar ustalony w czasie kompilacji.
 *
 * Składniki:
 *  - pdm::RingBuffer       – bufor cykliczny o stałym rozmiarze,
 *  - pdm::LinearTrend      – regresja liniowa w oknie przesuwnym, aktualizacja O(1),
 *  - pdm::AlarmChannel     – progi ostrzeżenia/alarmu z histerezą, zwłoką i stanami ISA-18.2,
 *  - pdm::WearModel        – model skumulowanego zużycia (reguła 10 °C, trwałość L10 ~ obciążenie^3),
 *  - pdm::computeHealth()  – indeks kondycji maszyny 0–100 %,
 *  - pdm::PredictiveEngine – analiza wieloparametrowa: diagnoza przyczyny i prognoza terminu kontroli.
 *
 * Jednostki: temperatura [°C], wilgotność [%], prędkość drgań [mm/s], czas maszyny [h].
 */
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace pdm {

// ============================================================================
// Funkcje pomocnicze
// ============================================================================

/** @brief Ogranicza wartość do przedziału [lo, hi]. */
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/** @brief Mniejsza z dwóch wartości (bez makr Arduino). */
inline float minf(float a, float b) { return a < b ? a : b; }

/**
 * @brief Interpolacja liniowa funkcji zadanej punktami.
 * @param x  argument
 * @param xs węzły (rosnąco)
 * @param ys wartości w węzłach
 * @param n  liczba węzłów (>= 2)
 * @return wartość interpolowana; poza zakresem – wartość brzegowa
 */
inline float interpolate(float x, const float* xs, const float* ys, size_t n) {
  if (x <= xs[0]) return ys[0];
  for (size_t i = 1; i < n; ++i) {
    if (x <= xs[i]) {
      const float t = (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
      return ys[i - 1] + t * (ys[i] - ys[i - 1]);
    }
  }
  return ys[n - 1];
}

// ============================================================================
// Bufor cykliczny
// ============================================================================

/**
 * @brief Bufor cykliczny o stałej pojemności N (bez alokacji dynamicznej).
 * @tparam T typ elementu (musi mieć konstruktor domyślny)
 * @tparam N pojemność
 */
template <typename T, size_t N>
class RingBuffer {
  static_assert(N > 0, "Pojemnosc bufora musi byc dodatnia");

 public:
  /** @brief Dodaje element; przy pełnym buforze nadpisuje najstarszy. */
  void push(const T& value) {
    data_[head_] = value;
    head_ = (head_ + 1) % N;
    if (size_ < N) ++size_;
  }
  /** @brief Usuwa wszystkie elementy. */
  void clear() {
    head_ = 0;
    size_ = 0;
  }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  bool full() const { return size_ == N; }
  static constexpr size_t capacity() { return N; }
  /**
   * @brief Dostęp do elementu.
   * @param i indeks logiczny: 0 = najstarszy, size()-1 = najnowszy
   */
  const T& at(size_t i) const { return data_[(head_ + N - size_ + i) % N]; }
  /** @brief Najnowszy element (wymaga !empty()). */
  const T& newest() const { return data_[(head_ + N - 1) % N]; }
  /** @brief Najstarszy element (wymaga !empty()). */
  const T& oldest() const { return at(0); }

 private:
  T data_[N] = {};
  size_t head_ = 0;
  size_t size_ = 0;
};

// ============================================================================
// Trend liniowy w oknie przesuwnym
// ============================================================================

/**
 * @brief Trend liniowy (metoda najmniejszych kwadratów) w oknie przesuwnym N równoodległych próbek.
 *
 * @details Próbki mają stały odstęp dx [h], a oś x to indeks próbki w oknie (0 = najstarsza),
 *          więc Σx i Σx² zależą tylko od liczby próbek. Sumy Σy, Σxy i Σy² są aktualizowane
 *          w czasie O(1) – po przesunięciu okna o jedną próbkę:
 *          @f[ \Sigma xy' = \Sigma xy - (\Sigma y - y_{old}) + (N-1)\,y_{new} @f]
 *          Co N aktualizacji sumy są przeliczane od nowa, co ogranicza kumulację błędów zaokrągleń.
 *          Wiarygodność trendu ocenia współczynnik determinacji R².
 *
 * @tparam N długość okna (liczba próbek)
 */
template <size_t N>
class LinearTrend {
  static_assert(N >= 3, "Okno trendu musi miec co najmniej 3 probki");

 public:
  /** @param dxHours odstęp między próbkami w godzinach czasu maszyny */
  explicit LinearTrend(float dxHours = 1.0f) : dx_(dxHours) {}

  /** @brief Zmienia odstęp próbek (np. po zmianie skali czasu). */
  void setSpacing(float dxHours) { dx_ = dxHours; }

  /** @brief Czyści okno (np. po ponownym uruchomieniu maszyny). */
  void reset() {
    buf_.clear();
    sy_ = sxy_ = syy_ = 0.0;
    sinceRecalc_ = 0;
  }

  /** @brief Dodaje próbkę; przy pełnym oknie usuwa najstarszą. Złożoność O(1). */
  void add(float y) {
    const double yn = y;
    if (!buf_.full()) {
      sxy_ += static_cast<double>(buf_.size()) * yn;
      sy_ += yn;
      syy_ += yn * yn;
    } else {
      const double yOld = buf_.oldest();
      sxy_ = sxy_ - (sy_ - yOld) + static_cast<double>(N - 1) * yn;
      sy_ = sy_ - yOld + yn;
      syy_ = syy_ - yOld * yOld + yn * yn;
    }
    buf_.push(y);
    if (++sinceRecalc_ >= N) recalc();
  }

  /** @brief Liczba próbek w oknie. */
  size_t count() const { return buf_.size(); }

  /** @brief Nachylenie trendu [jednostka/h]; 0 przy mniej niż 2 próbkach. */
  float slopePerHour() const {
    double a, b;
    coefficients(a, b);
    return static_cast<float>(b / dx_);
  }

  /** @brief Wartość prostej regresji w chwili najnowszej próbki (wartość wygładzona). */
  float fittedLast() const {
    double a, b;
    coefficients(a, b);
    return static_cast<float>(a + b * (static_cast<double>(count()) - 1.0));
  }

  /** @brief Współczynnik determinacji R² ∈ [0, 1]; 0 gdy sygnał jest stały lub próbek jest za mało. */
  float r2() const {
    const double n = static_cast<double>(count());
    if (n < 3) return 0.0f;
    const double sx = n * (n - 1.0) / 2.0;
    const double sxx = (n - 1.0) * n * (2.0 * n - 1.0) / 6.0;
    const double cov = n * sxy_ - sx * sy_;
    const double varX = n * sxx - sx * sx;
    const double varY = n * syy_ - sy_ * sy_;
    if (varX <= 0.0 || varY <= 1e-9) return 0.0f;
    const double r2 = (cov * cov) / (varX * varY);
    return static_cast<float>(r2 > 1.0 ? 1.0 : r2);
  }

  /**
   * @brief Czy trend jest wiarygodny.
   * @param minPoints minimalna liczba próbek
   * @param minR2     minimalny współczynnik determinacji
   */
  bool reliable(size_t minPoints, float minR2) const { return count() >= minPoints && r2() >= minR2; }

  /**
   * @brief Prognoza czasu do osiągnięcia progu przy trendzie rosnącym.
   * @param threshold    próg
   * @param minPoints    minimalna liczba próbek
   * @param minR2        minimalny R²
   * @param minSlopePerH najmniejsze nachylenie uznawane za trend wzrostowy [jednostka/h]
   * @return czas [h]; 0 gdy wartość wygładzona już osiągnęła próg;
   *         INFINITY gdy brak wiarygodnego trendu wzrostowego
   */
  float hoursToReach(float threshold, size_t minPoints, float minR2, float minSlopePerH) const {
    if (count() < minPoints) return INFINITY;
    const float last = fittedLast();
    if (last >= threshold) return 0.0f;
    const float slope = slopePerHour();
    if (slope < minSlopePerH || r2() < minR2) return INFINITY;
    return (threshold - last) / slope;
  }

 private:
  void recalc() {
    sy_ = sxy_ = syy_ = 0.0;
    for (size_t i = 0; i < buf_.size(); ++i) {
      const double y = buf_.at(i);
      sy_ += y;
      sxy_ += static_cast<double>(i) * y;
      syy_ += y * y;
    }
    sinceRecalc_ = 0;
  }

  void coefficients(double& a, double& b) const {
    const double n = static_cast<double>(count());
    if (n < 2.0) {
      a = (n > 0.0) ? sy_ : 0.0;
      b = 0.0;
      return;
    }
    const double sx = n * (n - 1.0) / 2.0;
    const double sxx = (n - 1.0) * n * (2.0 * n - 1.0) / 6.0;
    b = (n * sxy_ - sx * sy_) / (n * sxx - sx * sx);
    a = (sy_ - b * sx) / n;
  }

  RingBuffer<float, N> buf_;
  double sy_ = 0.0, sxy_ = 0.0, syy_ = 0.0;
  size_t sinceRecalc_ = 0;
  float dx_;
};

// ============================================================================
// Progi alarmowe (ISA-18.2)
// ============================================================================

/** @brief Poziom zagrożenia parametru. Dla stanu utrzymania: OK / PLANUJ PRZEGLĄD / KONTROLA. */
enum class Level : uint8_t { kNormal = 0, kWarning = 1, kAlarm = 2 };

/** @brief Stan alarmu wg normy ISA-18.2 (zarządzanie alarmami w automatyce przemysłowej). */
enum class AlarmState : uint8_t {
  kNormal,        ///< brak alarmu
  kUnackActive,   ///< alarm aktywny, niepotwierdzony – szybkie miganie + sygnał dźwiękowy
  kAckActive,     ///< alarm aktywny, potwierdzony – światło ciągłe
  kUnackCleared,  ///< warunek ustąpił, ale alarm nie został potwierdzony – wolne miganie
};

/** @brief Konfiguracja progów jednego parametru (alarm górny). */
struct Thresholds {
  float warn = 0.0f;        ///< próg ostrzeżenia
  float alarm = 0.0f;       ///< próg alarmu
  float hysteresis = 0.0f;  ///< histereza powrotu (wartość bezwzględna)
  uint8_t delayOn = 1;      ///< liczba kolejnych ocen potwierdzających eskalację (filtr chwilowych skoków)
};

/**
 * @brief Kanał alarmowy jednego parametru: klasyfikacja z histerezą, zwłoka zadziałania
 *        i maszyna stanów potwierdzania alarmów ISA-18.2.
 *
 * @details Eskalacja poziomu wymaga cfg.delayOn kolejnych ocen powyżej progu; deeskalacja
 *          następuje od razu po spadku poniżej (próg − histereza). Wartość nieskończona
 *          lub NaN (brak danych) nie zmienia poziomu.
 */
class AlarmChannel {
 public:
  explicit AlarmChannel(const Thresholds& t = Thresholds()) : t_(t) {}

  void configure(const Thresholds& t) { t_ = t; }
  const Thresholds& thresholds() const { return t_; }

  /**
   * @brief Ocenia nową wartość parametru.
   * @return true, gdy zmienił się poziom (previous() → level())
   */
  bool update(float value) {
    const Level target = classify(value);
    if (target > level_) {
      if (++pending_ < t_.delayOn) return false;  // eskalacja jeszcze niepotwierdzona
    }
    pending_ = 0;
    if (target == level_) return false;
    previous_ = level_;
    level_ = target;
    if (level_ == Level::kAlarm) {
      state_ = AlarmState::kUnackActive;  // nowy (lub ponowny) alarm wymaga potwierdzenia
    } else if (previous_ == Level::kAlarm) {
      state_ = (state_ == AlarmState::kUnackActive) ? AlarmState::kUnackCleared : AlarmState::kNormal;
    }
    return true;
  }

  /**
   * @brief Potwierdzenie (kwitowanie) przez operatora.
   * @return true, gdy stan alarmu się zmienił
   */
  bool acknowledge() {
    if (state_ == AlarmState::kUnackActive) {
      state_ = AlarmState::kAckActive;
      return true;
    }
    if (state_ == AlarmState::kUnackCleared) {
      state_ = AlarmState::kNormal;
      return true;
    }
    return false;
  }

  /** @brief Przywraca stan początkowy (np. po serwisie). */
  void reset() {
    level_ = previous_ = Level::kNormal;
    state_ = AlarmState::kNormal;
    pending_ = 0;
  }

  Level level() const { return level_; }
  Level previous() const { return previous_; }
  AlarmState state() const { return state_; }
  bool unacknowledged() const {
    return state_ == AlarmState::kUnackActive || state_ == AlarmState::kUnackCleared;
  }

 private:
  Level classify(float v) const {
    if (!std::isfinite(v)) return level_;  // brak danych – utrzymaj poprzedni poziom
    if (v >= t_.alarm || (level_ == Level::kAlarm && v > t_.alarm - t_.hysteresis)) return Level::kAlarm;
    if (v >= t_.warn || (level_ != Level::kNormal && v > t_.warn - t_.hysteresis)) return Level::kWarning;
    return Level::kNormal;
  }

  Thresholds t_;
  Level level_ = Level::kNormal;
  Level previous_ = Level::kNormal;
  AlarmState state_ = AlarmState::kNormal;
  uint8_t pending_ = 0;
};

// ============================================================================
// Model zużycia
// ============================================================================

/** @brief Parametry modelu skumulowanego zużycia. */
struct WearConfig {
  float serviceIntervalH = 200.0f;  ///< interwał przeglądu przy warunkach nominalnych [h pracy]
  float tempRefC = 40.0f;           ///< temperatura bez przyspieszenia zużycia [°C]
  float tempDoublingC = 10.0f;      ///< przyrost temperatury podwajający tempo zużycia (reguła 10 °C)
  float tempFactorMax = 16.0f;      ///< ograniczenie współczynnika temperaturowego
  float vibRefMmS = 2.8f;           ///< drgania bez przyspieszenia zużycia (granica stref B/C ISO 10816)
  float vibExponent = 3.0f;         ///< wykładnik zależności trwałości łożysk L10 ~ (C/P)^3
  float vibFactorMax = 27.0f;       ///< ograniczenie współczynnika drganiowego
  float startPenaltyH = 0.5f;       ///< zużycie równoważne jednego rozruchu [h]
  float rateSmoothing = 0.1f;       ///< współczynnik EWMA wygładzania tempa zużycia
};

/**
 * @brief Model skumulowanego zużycia (reguła sumowania uszkodzeń Palmgrena–Minera).
 *
 * @details Każda godzina pracy zużywa @f$ k = k_T \cdot k_V @f$ godzin „równoważnych”:
 *  - @f$ k_T = 2^{(T - T_{ref})/10} @f$ – reguła 10 °C (Arrhenius): starzenie smaru i izolacji
 *    przyspiesza dwukrotnie na każde 10 °C powyżej temperatury odniesienia,
 *  - @f$ k_V = (v / v_{ref})^3 @f$ – trwałość łożysk L10 maleje z sześcianem obciążenia
 *    dynamicznego, którego miarą jest prędkość drgań.
 *
 * Każdy rozruch dolicza stałą porcję zużycia (udary prądowe i cieplne). Przegląd jest
 * wymagany, gdy suma godzin równoważnych osiągnie interwał przeglądu; prognoza czasu do
 * przeglądu dzieli pozostały zapas przez wygładzone (EWMA) bieżące tempo zużycia.
 */
class WearModel {
 public:
  explicit WearModel(const WearConfig& c = WearConfig()) : c_(c) {}

  /** @brief Współczynnik przyspieszenia zużycia od temperatury (>= 1). */
  float tempFactor(float tempC) const {
    if (!std::isfinite(tempC)) return 1.0f;
    return clampf(std::exp2((tempC - c_.tempRefC) / c_.tempDoublingC), 1.0f, c_.tempFactorMax);
  }

  /** @brief Współczynnik przyspieszenia zużycia od drgań (>= 1). */
  float vibFactor(float vibMmS) const {
    if (!std::isfinite(vibMmS) || vibMmS <= c_.vibRefMmS) return 1.0f;
    return clampf(std::pow(vibMmS / c_.vibRefMmS, c_.vibExponent), 1.0f, c_.vibFactorMax);
  }

  /**
   * @brief Całkuje zużycie za odcinek pracy.
   * @param dtH     czas pracy maszyny od poprzedniego wywołania [h] (0 na postoju)
   * @param running czy maszyna pracuje (tempo zużycia aktualizowane tylko w pracy)
   * @param tempC   temperatura [°C] (NaN = brak danych)
   * @param vibMmS  prędkość drgań [mm/s] (NaN = brak danych)
   */
  void update(float dtH, bool running, float tempC, float vibMmS) {
    if (!running) return;
    instRate_ = tempFactor(tempC) * vibFactor(vibMmS);
    smoothedRate_ = rateInit_ ? smoothedRate_ + c_.rateSmoothing * (instRate_ - smoothedRate_) : instRate_;
    rateInit_ = true;
    if (dtH > 0.0f) eqHours_ += instRate_ * dtH;
  }

  /** @brief Dolicza zużycie związane z rozruchem maszyny. */
  void onStart() { eqHours_ += c_.startPenaltyH; }
  /**
   * @brief Zeruje zużycie po wykonanym przeglądzie.
   * @details Zerowana jest też pamięć EWMA tempa zużycia – prognoza po przeglądzie startuje od
   *          pierwszego pomiaru w pracy, a nie od warunków sprzed przeglądu.
   */
  void reset() {
    eqHours_ = 0.0f;
    instRate_ = smoothedRate_ = 1.0f;
    rateInit_ = false;
  }
  /** @brief Ustawia zużycie (odtworzenie z pamięci nieulotnej lub demonstracja). */
  void setEquivalentHours(float h) { eqHours_ = h > 0.0f ? h : 0.0f; }

  float equivalentHours() const { return eqHours_; }
  /** @brief Wykorzystana część interwału przeglądu (1.0 = przegląd wymagany). */
  float lifeFraction() const { return eqHours_ / c_.serviceIntervalH; }
  /** @brief Wygładzone tempo zużycia [h równoważnych / h pracy]. */
  float rate() const { return smoothedRate_; }
  /** @brief Chwilowe tempo zużycia. */
  float instantRate() const { return instRate_; }
  /** @brief Prognoza godzin pracy do przeglądu przy obecnym tempie zużycia. */
  float hoursToService() const {
    const float remaining = c_.serviceIntervalH - eqHours_;
    if (remaining <= 0.0f) return 0.0f;
    return remaining / (smoothedRate_ > 1.0f ? smoothedRate_ : 1.0f);
  }
  const WearConfig& config() const { return c_; }

 private:
  WearConfig c_;
  float eqHours_ = 0.0f;
  float instRate_ = 1.0f;
  float smoothedRate_ = 1.0f;
  bool rateInit_ = false;
};

// ============================================================================
// Indeks kondycji
// ============================================================================

/** @brief Oceny cząstkowe (0 = stan krytyczny, 1 = stan bardzo dobry) i indeks kondycji. */
struct Health {
  float temp = 1.0f;     ///< ocena temperatury
  float vib = 1.0f;      ///< ocena drgań (strefy ISO 10816)
  float hum = 1.0f;      ///< ocena wilgotności
  float wear = 1.0f;     ///< ocena zużycia (1 − wykorzystany interwał)
  float index = 100.0f;  ///< indeks kondycji [%]
};

/**
 * @brief Wyznacza indeks kondycji maszyny z ocen cząstkowych.
 * @details Oceny cząstkowe to funkcje odcinkowo liniowe (punkty węzłowe = progi ostrzeżenia
 *          i alarmu). Indeks = średnia ważona ocen dostępnych parametrów, ograniczona od góry
 *          przez najgorszą ocenę + 0,3 – pojedynczy krytyczny parametr nie „ginie” w średniej.
 *          Wagi: drgania 0,35, temperatura 0,30, zużycie 0,25, wilgotność 0,10.
 */
inline Health computeHealth(float tempC, bool tempOk, float vibMmS, bool vibOk, float humPct, bool humOk,
                            float lifeFraction) {
  static const float kTempX[] = {50.0f, 60.0f, 70.0f, 80.0f};
  static const float kTempY[] = {1.0f, 0.6f, 0.2f, 0.0f};
  static const float kVibX[] = {1.12f, 2.8f, 7.1f, 11.2f};  // granice stref A/B, B/C, C/D ISO 10816-1 (kl. II)
  static const float kVibY[] = {1.0f, 0.7f, 0.25f, 0.0f};
  static const float kHumX[] = {70.0f, 80.0f, 90.0f, 100.0f};
  static const float kHumY[] = {1.0f, 0.6f, 0.2f, 0.0f};

  Health h;
  h.temp = interpolate(tempC, kTempX, kTempY, 4);
  h.vib = interpolate(vibMmS, kVibX, kVibY, 4);
  h.hum = interpolate(humPct, kHumX, kHumY, 4);
  h.wear = clampf(1.0f - lifeFraction, 0.0f, 1.0f);

  const float w[4] = {tempOk ? 0.30f : 0.0f, vibOk ? 0.35f : 0.0f, humOk ? 0.10f : 0.0f, 0.25f};
  const float s[4] = {h.temp, h.vib, h.hum, h.wear};
  float sumW = 0.0f, sumWs = 0.0f, worst = 1.0f;
  for (int i = 0; i < 4; ++i) {
    if (w[i] <= 0.0f) continue;
    sumW += w[i];
    sumWs += w[i] * s[i];
    worst = minf(worst, s[i]);
  }
  const float mean = sumW > 0.0f ? sumWs / sumW : 1.0f;
  h.index = 100.0f * clampf(minf(mean, worst + 0.3f), 0.0f, 1.0f);
  return h;
}

/** @brief Strefa stanu drganiowego wg ISO 10816-1 (maszyny klasy II, 15–75 kW). */
inline char isoZone(float vibMmS) {
  if (!std::isfinite(vibMmS)) return '?';
  if (vibMmS < 1.12f) return 'A';
  if (vibMmS < 2.8f) return 'B';
  if (vibMmS < 7.1f) return 'C';
  return 'D';
}

// ============================================================================
// Silnik predykcyjny
// ============================================================================

/** @brief Monitorowane wielkości (kanały alarmowe). */
enum class Param : uint8_t { kTemp = 0, kVib = 1, kHum = 2, kMaint = 3, kSensor = 4 };
constexpr size_t kParamCount = 5;  ///< liczba kanałów alarmowych

/** @brief Prawdopodobna przyczyna pogorszenia stanu (diagnoza wieloparametrowa). */
enum class Cause : uint8_t {
  kNone,         ///< praca prawidłowa
  kSensor,       ///< usterka czujnika – diagnostyka ograniczona
  kBearing,      ///< drgania + wzrost temperatury: łożysko / smarowanie
  kThermal,      ///< temperatura bez drgań: chłodzenie / przeciążenie
  kMechanical,   ///< drgania przy stabilnej temperaturze: niewyważenie / luz / nieosiowość
  kEnvironment,  ///< wilgotność: ryzyko kondensacji i korozji
  kWear,         ///< zbliża się przegląd okresowy (zużycie eksploatacyjne)
};

/** @brief Czynnik, który wyznaczył prognozowany termin kontroli. */
enum class Driver : uint8_t { kNone, kWear, kTempTrend, kVibTrend };

/** @brief Konfiguracja silnika predykcyjnego. */
struct EngineConfig {
  Thresholds temp{60.0f, 70.0f, 2.0f, 2};  ///< temperatura obudowy [°C]
  Thresholds vib{2.8f, 7.1f, 0.3f, 2};     ///< prędkość drgań [mm/s] – granice stref C i D ISO 10816-1
  Thresholds hum{80.0f, 90.0f, 3.0f, 2};   ///< wilgotność względna [%]
  WearConfig wear;
  float stepHours = 1.0f;          ///< czas maszyny przypadający na jeden krok analizy [h]
  size_t trendMinPoints = 15;      ///< minimalna liczba próbek wiarygodnego trendu
  float trendMinR2 = 0.6f;         ///< minimalny R² wiarygodnego trendu
  float tempMinSlope = 0.2f;       ///< [°C/h] – wolniejszy wzrost nie jest traktowany jako trend
  float vibMinSlope = 0.02f;       ///< [mm/s/h]
  float planHorizonH = 48.0f;      ///< prognoza <= tyle godzin -> zaplanuj przegląd
  float criticalHorizonH = 8.0f;   ///< prognoza <= tyle godzin -> wymagana kontrola
  float planLife = 0.8f;           ///< wykorzystanie interwału -> zaplanuj przegląd
  float planHealth = 70.0f;        ///< indeks kondycji < tej wartości -> zaplanuj przegląd
  float criticalHealth = 40.0f;    ///< indeks kondycji < tej wartości -> wymagana kontrola
  uint8_t statusConfirmSteps = 2;  ///< kroki potwierdzające podwyższenie statusu (odporność na zakłócenia)
  uint8_t statusHoldSteps = 5;     ///< kroki potwierdzające obniżenie statusu (antyoscylacja)
  float healthSmoothing = 0.3f;    ///< EWMA indeksu kondycji
};

/** @brief Dane wejściowe jednego kroku analizy. */
struct EngineInput {
  bool running = false;
  float workHours = 0.0f;  ///< czas pracy od poprzedniego kroku [h maszyny]
  float tempC = NAN;
  bool tempValid = false;
  float humidityPct = NAN;
  bool humidityValid = false;
  float vibMmS = NAN;
  bool vibValid = false;
  bool sensorFault = false;  ///< usterka któregokolwiek czujnika
};

/** @brief Wynik analizy – stan, przyczyna i prognoza. */
struct EngineOutput {
  Level status = Level::kNormal;   ///< OK / PLANUJ PRZEGLĄD / WYMAGANA KONTROLA
  Cause cause = Cause::kNone;
  Driver driver = Driver::kNone;
  float hoursToInspection = INFINITY;  ///< prognozowany czas pracy do kontroli [h]
  float hoursToService = INFINITY;     ///< z modelu zużycia [h]
  float tempHoursToAlarm = INFINITY;   ///< z trendu temperatury [h]
  float vibHoursToAlarm = INFINITY;    ///< z trendu drgań [h]
  float tempSlope = 0.0f, tempR2 = 0.0f;
  float vibSlope = 0.0f, vibR2 = 0.0f;
  Health health;
  float lifeFraction = 0.0f;
  float wearRate = 1.0f;
  char zone = 'A';
};

/** @brief Zmiana poziomu kanału zgłaszana przez krok analizy (na potrzeby dziennika zdarzeń). */
struct LevelChange {
  Param param;
  Level from;
  Level to;
  float value;  ///< wartość parametru (dla kMaint – prognoza godzin do kontroli)
};

/**
 * @brief Silnik predykcyjny: wieloparametrowa ocena stanu i prognoza terminu kontroli maszyny.
 *
 * @details Kolejność kroku analizy:
 *  1. model zużycia – całkowanie zużycia z czynnikami temperatury i drgań,
 *  2. trendy temperatury i drgań (tylko w pracy) – nachylenie, R², czas do progu alarmu,
 *  3. kanały alarmowe parametrów (progi z histerezą, ISA-18.2),
 *  4. indeks kondycji,
 *  5. prognoza: najkrótszy z czasów (zużycie, trend temperatury, trend drgań),
 *  6. diagnoza przyczyny na podstawie korelacji parametrów,
 *  7. status utrzymania z potwierdzaniem zmian: podwyższenie po 2 krokach (odporność na pojedyncze
 *     zakłócenia), obniżenie po 5 krokach (antyoscylacja).
 */
class PredictiveEngine {
 public:
  static constexpr size_t kTrendWindow = 60;  ///< długość okna trendów [kroki]

  explicit PredictiveEngine(const EngineConfig& cfg = EngineConfig())
      : cfg_(cfg), tempTrend_(cfg.stepHours), vibTrend_(cfg.stepHours), wear_(cfg.wear) {
    ch_[idx(Param::kTemp)].configure(cfg.temp);
    ch_[idx(Param::kVib)].configure(cfg.vib);
    ch_[idx(Param::kHum)].configure(cfg.hum);
    ch_[idx(Param::kMaint)].configure(Thresholds{1.0f, 2.0f, 0.5f, 1});   // wartość = status 0/1/2
    ch_[idx(Param::kSensor)].configure(Thresholds{1.0f, 2.0f, 0.5f, 1});  // wartość = 2 przy usterce
  }

  /**
   * @brief Wykonuje jeden krok analizy (wywoływany cyklicznie, np. co 1 s).
   * @param in        bieżące dane
   * @param changes   tablica na zmiany poziomów kanałów (może być nullptr)
   * @param maxChanges pojemność tablicy changes
   * @return liczba zapisanych zmian
   */
  size_t step(const EngineInput& in, LevelChange* changes, size_t maxChanges) {
    size_t nChanges = 0;
    auto record = [&](Param p, float value) {
      const AlarmChannel& c = ch_[idx(p)];
      if (changes != nullptr && nChanges < maxChanges) changes[nChanges++] = {p, c.previous(), c.level(), value};
    };

    // 1. Zużycie
    wear_.update(in.workHours, in.running, in.tempValid ? in.tempC : NAN, in.vibValid ? in.vibMmS : NAN);

    // 2. Trendy (próbkowane tylko w czasie pracy)
    if (in.running) {
      if (in.tempValid) tempTrend_.add(in.tempC);
      if (in.vibValid) vibTrend_.add(in.vibMmS);
    }

    // 3. Kanały alarmowe parametrów procesowych
    if (ch_[idx(Param::kTemp)].update(in.tempValid ? in.tempC : NAN)) record(Param::kTemp, in.tempC);
    if (ch_[idx(Param::kVib)].update(in.vibValid ? in.vibMmS : NAN)) record(Param::kVib, in.vibMmS);
    if (ch_[idx(Param::kHum)].update(in.humidityValid ? in.humidityPct : NAN)) {
      record(Param::kHum, in.humidityPct);
    }
    if (ch_[idx(Param::kSensor)].update(in.sensorFault ? 2.0f : 0.0f)) record(Param::kSensor, in.sensorFault);

    // 4. Trend: nachylenia i czas do progu alarmu
    out_.tempSlope = tempTrend_.slopePerHour();
    out_.tempR2 = tempTrend_.r2();
    out_.vibSlope = vibTrend_.slopePerHour();
    out_.vibR2 = vibTrend_.r2();
    out_.tempHoursToAlarm = (in.tempValid && in.tempC < cfg_.temp.alarm)
                                ? tempTrend_.hoursToReach(cfg_.temp.alarm, cfg_.trendMinPoints, cfg_.trendMinR2,
                                                          cfg_.tempMinSlope)
                                : INFINITY;
    out_.vibHoursToAlarm = (in.vibValid && in.vibMmS < cfg_.vib.alarm)
                               ? vibTrend_.hoursToReach(cfg_.vib.alarm, cfg_.trendMinPoints, cfg_.trendMinR2,
                                                        cfg_.vibMinSlope)
                               : INFINITY;

    // 5. Indeks kondycji (wygładzony EWMA)
    const Health raw = computeHealth(in.tempC, in.tempValid, in.vibMmS, in.vibValid, in.humidityPct,
                                     in.humidityValid, wear_.lifeFraction());
    out_.health = raw;
    smoothedHealth_ = healthInit_ ? smoothedHealth_ + cfg_.healthSmoothing * (raw.index - smoothedHealth_) : raw.index;
    healthInit_ = true;
    out_.health.index = smoothedHealth_;
    out_.lifeFraction = wear_.lifeFraction();
    out_.wearRate = wear_.rate();
    out_.zone = in.vibValid ? isoZone(in.vibMmS) : '?';

    // 6. Prognoza terminu kontroli = najkrótszy z czasów
    out_.hoursToService = wear_.hoursToService();
    out_.hoursToInspection = out_.hoursToService;
    out_.driver = Driver::kWear;
    if (out_.tempHoursToAlarm < out_.hoursToInspection) {
      out_.hoursToInspection = out_.tempHoursToAlarm;
      out_.driver = Driver::kTempTrend;
    }
    if (out_.vibHoursToAlarm < out_.hoursToInspection) {
      out_.hoursToInspection = out_.vibHoursToAlarm;
      out_.driver = Driver::kVibTrend;
    }

    // 7. Diagnoza i status utrzymania
    out_.cause = diagnose(in);
    const Level target = rawStatus(in);
    if (target > out_.status) {
      holdCount_ = 0;
      if (++confirmCount_ >= cfg_.statusConfirmSteps) {  // eskalacja po potwierdzeniu: bez alarmów ulotnych
        out_.status = target;
        confirmCount_ = 0;
      }
    } else if (target < out_.status) {
      confirmCount_ = 0;
      if (++holdCount_ >= cfg_.statusHoldSteps) {  // obniżenie dopiero po dłuższym potwierdzeniu
        out_.status = target;
        holdCount_ = 0;
      }
    } else {
      holdCount_ = 0;
      confirmCount_ = 0;
    }
    if (ch_[idx(Param::kMaint)].update(static_cast<float>(out_.status))) {
      record(Param::kMaint, out_.hoursToInspection);
    }
    return nChanges;
  }

  /** @brief Ponowne uruchomienie maszyny: dolicza zużycie rozruchu i czyści okna trendów. */
  void onMachineStart() {
    wear_.onStart();
    tempTrend_.reset();
    vibTrend_.reset();
  }

  /**
   * @brief Kwitowanie wszystkich kanałów przez operatora.
   * @return liczba kanałów, których stan się zmienił
   */
  size_t acknowledgeAll() {
    size_t n = 0;
    for (auto& c : ch_) n += c.acknowledge() ? 1 : 0;
    return n;
  }

  /**
   * @brief Wykonany przegląd: zerowanie zużycia, trendów i statusu utrzymania.
   * @details Filtry EWMA (tempo zużycia, indeks kondycji) są inicjalizowane od nowa – przegląd to
   *          skokowa zmiana stanu maszyny, której filtr szumu nie powinien opóźniać.
   */
  void serviceDone() {
    wear_.reset();
    tempTrend_.reset();
    vibTrend_.reset();
    healthInit_ = false;
    out_.status = Level::kNormal;
    holdCount_ = 0;
    confirmCount_ = 0;
    ch_[idx(Param::kMaint)].reset();
  }

  /** @brief Czy którykolwiek kanał ma niepotwierdzony alarm. */
  bool anyUnacknowledged() const {
    for (const auto& c : ch_) {
      if (c.unacknowledged()) return true;
    }
    return false;
  }

  /** @brief Najpilniejszy stan alarmów (dla sygnalizacji): Unack > Ack > UnackCleared > Normal. */
  AlarmState worstAlarmState() const {
    bool ack = false, cleared = false;
    for (const auto& c : ch_) {
      if (c.state() == AlarmState::kUnackActive) return AlarmState::kUnackActive;
      ack |= c.state() == AlarmState::kAckActive;
      cleared |= c.state() == AlarmState::kUnackCleared;
    }
    return ack ? AlarmState::kAckActive : (cleared ? AlarmState::kUnackCleared : AlarmState::kNormal);
  }

  /** @brief Najwyższy poziom spośród parametrów procesowych (temperatura, drgania, wilgotność). */
  Level worstProcessLevel() const {
    Level worst = Level::kNormal;
    for (Param p : {Param::kTemp, Param::kVib, Param::kHum}) {
      if (ch_[idx(p)].level() > worst) worst = ch_[idx(p)].level();
    }
    return worst;
  }

  /** @brief Maska kanałów w stanie alarmu (bit = indeks Param). */
  uint8_t alarmMask() const {
    uint8_t m = 0;
    for (size_t i = 0; i < kParamCount; ++i) {
      if (ch_[i].level() == Level::kAlarm) m |= static_cast<uint8_t>(1U << i);
    }
    return m;
  }

  const EngineOutput& output() const { return out_; }
  const AlarmChannel& channel(Param p) const { return ch_[idx(p)]; }
  WearModel& wear() { return wear_; }
  const WearModel& wear() const { return wear_; }
  const EngineConfig& config() const { return cfg_; }

 private:
  static constexpr size_t idx(Param p) { return static_cast<size_t>(p); }

  bool tempRising() const {
    return tempTrend_.reliable(cfg_.trendMinPoints, cfg_.trendMinR2) && out_.tempSlope >= cfg_.tempMinSlope;
  }

  /** @brief Reguły diagnostyczne – korelacja parametrów, w kolejności priorytetu. */
  Cause diagnose(const EngineInput& in) const {
    if (in.sensorFault) return Cause::kSensor;
    const Level t = ch_[idx(Param::kTemp)].level();
    const Level v = ch_[idx(Param::kVib)].level();
    const Level h = ch_[idx(Param::kHum)].level();
    const bool vibRising = out_.vibHoursToAlarm <= cfg_.planHorizonH;
    const bool tempTrendRisk = out_.tempHoursToAlarm <= cfg_.planHorizonH;
    if (v >= Level::kWarning && (t >= Level::kWarning || tempRising())) return Cause::kBearing;
    if (t >= Level::kWarning) return Cause::kThermal;
    if (v >= Level::kWarning) return Cause::kMechanical;
    if (vibRising && tempTrendRisk) return Cause::kBearing;
    if (tempTrendRisk) return Cause::kThermal;
    if (vibRising) return Cause::kMechanical;
    if (h >= Level::kWarning) return Cause::kEnvironment;
    if (wear_.lifeFraction() >= cfg_.planLife || out_.hoursToService <= cfg_.planHorizonH) return Cause::kWear;
    return Cause::kNone;
  }

  /** @brief Status utrzymania przed potwierdzeniem (wynik bieżącego kroku). */
  Level rawStatus(const EngineInput& in) const {
    const Level process = worstProcessLevel();
    const float h = out_.health.index;
    const float t = out_.hoursToInspection;
    if (process == Level::kAlarm || wear_.lifeFraction() >= 1.0f || t <= cfg_.criticalHorizonH ||
        h < cfg_.criticalHealth) {
      return Level::kAlarm;
    }
    if (process == Level::kWarning || in.sensorFault || wear_.lifeFraction() >= cfg_.planLife ||
        t <= cfg_.planHorizonH || h < cfg_.planHealth) {
      return Level::kWarning;
    }
    return Level::kNormal;
  }

  EngineConfig cfg_;
  AlarmChannel ch_[kParamCount];
  LinearTrend<kTrendWindow> tempTrend_;
  LinearTrend<kTrendWindow> vibTrend_;
  WearModel wear_;
  EngineOutput out_;
  float smoothedHealth_ = 100.0f;
  bool healthInit_ = false;
  uint8_t holdCount_ = 0;
  uint8_t confirmCount_ = 0;
};

// ============================================================================
// Nazwy tekstowe (dziennik, MQTT, OLED – bez polskich znaków)
// ============================================================================

/** @brief Kod parametru (MQTT/dziennik). */
inline const char* paramCode(Param p) {
  switch (p) {
    case Param::kTemp: return "TEMP";
    case Param::kVib: return "VIB";
    case Param::kHum: return "HUM";
    case Param::kMaint: return "MAINT";
    case Param::kSensor: return "SENSOR";
  }
  return "?";
}

/** @brief Nazwa poziomu; dla kanału utrzymania: OK / PLAN / KONTROLA. */
inline const char* levelCode(Param p, Level l) {
  if (p == Param::kMaint) return l == Level::kAlarm ? "KONTROLA" : (l == Level::kWarning ? "PLAN" : "OK");
  return l == Level::kAlarm ? "ALARM" : (l == Level::kWarning ? "OSTRZEZENIE" : "NORMA");
}

/** @brief Kod przyczyny (MQTT). */
inline const char* causeCode(Cause c) {
  switch (c) {
    case Cause::kNone: return "NONE";
    case Cause::kSensor: return "SENSOR";
    case Cause::kBearing: return "BEARING";
    case Cause::kThermal: return "THERMAL";
    case Cause::kMechanical: return "MECHANICAL";
    case Cause::kEnvironment: return "ENVIRONMENT";
    case Cause::kWear: return "WEAR";
  }
  return "?";
}

/** @brief Opis przyczyny dla operatora (krótki, bez polskich znaków – mieści się na OLED). */
inline const char* causeText(Cause c) {
  switch (c) {
    case Cause::kNone: return "praca prawidlowa";
    case Cause::kSensor: return "usterka czujnika";
    case Cause::kBearing: return "lozysko/smarowanie";
    case Cause::kThermal: return "przegrzewanie";
    case Cause::kMechanical: return "niewywazenie/luz";
    case Cause::kEnvironment: return "wilgoc/korozja";
    case Cause::kWear: return "przeglad okresowy";
  }
  return "?";
}

/** @brief Opis czynnika prognozy. */
inline const char* driverText(Driver d) {
  switch (d) {
    case Driver::kNone: return "-";
    case Driver::kWear: return "zuzycie";
    case Driver::kTempTrend: return "trend temp.";
    case Driver::kVibTrend: return "trend drgan";
  }
  return "?";
}

}  // namespace pdm
