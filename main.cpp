/**
 * @file    main.cpp
 * @brief   Inteligentny system utrzymania ruchu maszyn (Predictive Maintenance) – ESP32.
 *
 * @details Warstwa sprzętowa i systemowa aplikacji (Arduino + FreeRTOS). Algorytmy utrzymania
 *          ruchu (progi alarmowe, trendy, model zużycia, prognoza terminu kontroli) znajdują się
 *          w niezależnym od sprzętu module predictive.h, testowanym jednostkowo na komputerze.
 *
 * Realizacja wymagań:
 *  - ocena 3.0/3.5 – monitorowanie drgań (MPU6050), temperatury i wilgotności (DHT22),
 *    czasu pracy i liczby uruchomień (sygnał RUN),
 *  - ocena 4.0 – progi ostrzeżenia/alarmu z histerezą i zwłoką; potwierdzanie alarmów wg ISA-18.2,
 *  - ocena 4.5 – historia pomiarów (bufor cykliczny 10 min) i dziennik zdarzeń; liczniki pracy
 *    i zużycia w pamięci nieulotnej (NVS),
 *  - ocena 5.0 – analiza wieloparametrowa: indeks kondycji, trendy (regresja w oknie przesuwnym),
 *    model skumulowanego zużycia, diagnoza przyczyny i prognoza „kontrola za X h”.
 *
 * Architektura (FreeRTOS, przepływ danych w jedną stronę, bez blokującego delay()):
 * @verbatim
 *  Timer ISR (100 Hz) --notify--> [Akwizycja: rdzeń 1, prio 3] --skrzynka--+
 *                                                                          v
 *  GPIO ISR (ACK/SERWIS/RUN) --notify--> [Sterownik loop(): rdzeń 1, prio 1]
 *                                           | analiza (predictive.h), historia, NVS,
 *                                           | OLED / LED / buzzer / konsola
 *                                           | skrzynka + kolejka zdarzeń
 *                                           v
 *                              [Komunikacja: rdzeń 0, prio 1] <--> MQTT (telemetria, zdarzenia, polecenia)
 * @endverbatim
 *
 * Czas maszyny jest na potrzeby demonstracji przyspieszony (cfg::kTimeScale): 1 s symulacji
 * = 1 h pracy, dzięki czemu cykl zużycia i prognozy widać w ciągu kilku minut.
 *
 * Zasady: brak alokacji pamięci w pętli pracy (bufory statyczne, brak klasy String),
 * procedury ISR ograniczone do zapisu znacznika czasu i powiadomienia zadania.
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <atomic>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "predictive.h"

// ============================================================================
// 1. KONFIGURACJA
// ============================================================================
namespace cfg {

constexpr char kFirmware[] = "WTP4-PdM 0.3.0 (Etap 3)";

// --- Piny (zgodne z diagram.json) ---
constexpr uint8_t kPinI2c0Sda = 21;     ///< I2C0 (Wire): MPU6050
constexpr uint8_t kPinI2c0Scl = 22;
constexpr uint8_t kPinI2c1Sda = 33;     ///< I2C1 (Wire1): OLED SSD1306
constexpr uint8_t kPinI2c1Scl = 32;
constexpr uint8_t kPinDht = 16;         ///< DHT22 (na GPIO4 model Wokwi zwraca TIMEOUT)
constexpr uint8_t kPinBtnAck = 19;      ///< przycisk ACK – kwitowanie alarmów
constexpr uint8_t kPinBtnService = 18;  ///< przycisk SERWIS – przytrzymanie 3 s
constexpr uint8_t kPinRun = 17;         ///< sygnał pracy maszyny (styk pomocniczy stycznika)
constexpr uint8_t kPinLedRed = 25;
constexpr uint8_t kPinLedYellow = 26;
constexpr uint8_t kPinLedGreen = 27;
constexpr uint8_t kPinBuzzer = 13;

// --- Magistrale I2C ---
constexpr uint32_t kI2cClockHz = 400000;
constexpr uint16_t kI2cTimeoutMs = 10;
constexpr uint8_t kMpuAddress = 0x68;
constexpr uint8_t kOledAddress = 0x3C;

// --- Akwizycja i przetwarzanie sygnałów ---
constexpr uint32_t kSampleRateHz = 100;             ///< próbkowanie MPU6050 (przerwanie timera)
constexpr uint16_t kWindowSamples = kSampleRateHz;  ///< okno analizy drgań: 1 s
constexpr uint32_t kDhtPeriodMs = 2500;             ///< DHT22: okres min. 2 s + zapas
constexpr uint8_t kDhtFaultStreak = 3;              ///< kolejne błędy DHT22 = usterka czujnika
constexpr uint8_t kMpuFaultStreak = 10;             ///< kolejne błędy MPU6050 = usterka czujnika
constexpr uint32_t kMpuRetryMs = 1000;              ///< okres prób ponownej inicjalizacji MPU
constexpr float kGravityTauS = 2.0f;      ///< stała czasowa adaptacji wektora grawitacji (postój)
constexpr float kDominantFreqHz = 49.2f;  ///< 1x obroty: pompa z silnikiem 2-biegunowym (~2950 obr/min)
constexpr float kStandardGravity = 9.80665f;

// --- Czas maszyny i analiza predykcyjna ---
constexpr uint32_t kTimeScale = 3600;     ///< 1 s symulacji = 1 h pracy maszyny (demonstracja)
constexpr float kAnalysisPeriodS = 1.0f;  ///< krok analizy = okno drgań
constexpr float kStepHours = kTimeScale * kAnalysisPeriodS / 3600.0f;  ///< czas maszyny na krok [h]

// --- Historia i pamięć nieulotna ---
constexpr uint32_t kHistoryPeriodMs = 5000;  ///< okres zapisu rekordu historii
constexpr size_t kHistorySize = 120;         ///< 120 x 5 s = 10 min historii
constexpr size_t kEventLogSize = 32;         ///< ostatnie zdarzenia w RAM
constexpr uint32_t kPersistPeriodMs = 60000; ///< okresowy zapis liczników do NVS
constexpr char kNvsNamespace[] = "wtp4pdm";

// --- Wejścia ---
constexpr uint32_t kDebounceUs = 30000;    ///< wymagany czas stabilnego stanu (30 ms)
constexpr uint32_t kServiceHoldMs = 3000;  ///< potwierdzenie serwisu przytrzymaniem 3 s

// --- HMI ---
constexpr uint32_t kControllerTickMs = 10;  ///< maks. czas oczekiwania sterownika na zdarzenie
constexpr uint32_t kDisplayPeriodMs = 500;
constexpr uint32_t kScreenPeriodMs = 4000;  ///< rotacja ekranów OLED (stan / prognoza / trend)
constexpr uint32_t kStatusPeriodMs = 1000;
constexpr uint32_t kDiagPeriodMs = 30000;
constexpr uint32_t kLampTestMs = 1500;        ///< test lampek po starcie (praktyka przemysłowa)
constexpr uint32_t kAlarmBeepPeriodMs = 2000; ///< sygnał niepotwierdzonego alarmu

// --- Sieć i MQTT ---
constexpr char kWifiSsid[] = "Wokwi-GUEST";
constexpr char kWifiPass[] = "";
constexpr int32_t kWifiChannel = 6;  ///< stały kanał przyspiesza łączenie w Wokwi
constexpr uint32_t kWifiRetryMs = 15000;
constexpr const char* kMqttHosts[] = {"broker.emqx.io", "test.mosquitto.org"};  ///< główny, zapasowy
constexpr uint8_t kMqttHostCount = sizeof(kMqttHosts) / sizeof(kMqttHosts[0]);
constexpr uint16_t kMqttPort = 1883;
constexpr char kTopicBase[] = "wtp4/pdm/maszyna-01";
constexpr uint32_t kTelemetryPeriodMs = 5000;
constexpr uint32_t kStatusRefreshMs = 30000;  ///< odświeżanie statusu "online" (koryguje spóźniony LWT)
constexpr uint32_t kMqttBackoffMinMs = 1000;
constexpr uint32_t kMqttBackoffMaxMs = 30000;
constexpr uint16_t kMqttBufferSize = 768;
constexpr uint16_t kMqttKeepAliveS = 30;

// --- Zadania FreeRTOS (rozmiary stosu w bajtach) ---
constexpr uint32_t kAcqStack = 4096;
constexpr UBaseType_t kAcqPriority = 3;
constexpr uint32_t kCommsStack = 6144;
constexpr UBaseType_t kCommsPriority = 1;
constexpr uint8_t kEventQueueLen = 16;
constexpr uint8_t kCommandQueueLen = 4;

}  // namespace cfg

// ============================================================================
// 2. TYPY DANYCH
// ============================================================================

/** @brief Cechy sygnału drgań wyznaczone w jednym oknie analizy (1 s). */
struct VibrationFeatures {
  float aRmsG = 0.0f;    ///< wartość skuteczna przyspieszenia dynamicznego [g]
  float aPeakG = 0.0f;   ///< wartość szczytowa przyspieszenia dynamicznego [g]
  float crest = 0.0f;    ///< współczynnik szczytu (peak / RMS)
  float vRmsMmS = 0.0f;  ///< estymata prędkości drgań RMS [mm/s] (miara z ISO 10816)
};

/** @brief Ostatni odczyt czujnika klimatu DHT22. */
struct ClimateReading {
  float tempC = NAN;        ///< temperatura [°C]
  float humidityPct = NAN;  ///< wilgotność względna [%]
  bool valid = false;       ///< czy dostępny jest poprawny odczyt
  uint8_t errorStreak = 0;  ///< liczba kolejnych nieudanych odczytów
};

/** @brief Migawka danych z zadania akwizycji (przekazywana przez skrzynkę). */
struct SensorSnapshot {
  uint32_t seq = 0;          ///< numer kolejny migawki
  uint32_t timestampMs = 0;  ///< czas utworzenia [ms od startu]
  uint32_t windows = 0;      ///< liczba zamkniętych okien drgań (wyzwala krok analizy)
  VibrationFeatures vib;
  ClimateReading climate;
  float accelG[3] = {0.0f, 0.0f, 0.0f};  ///< ostatnia próbka przyspieszenia [g]
  float mpuTempC = NAN;                  ///< temperatura struktury MPU6050 [°C]
  bool mpuOk = false;                    ///< czy MPU6050 odpowiada
  uint32_t samples = 0;                  ///< liczba próbek od startu
  uint32_t overruns = 0;                 ///< pominięte takty timera (przeciążenie)
};

/** @brief Maska usterek czujników. */
enum FaultBit : uint8_t { kFaultMpu = 0x01, kFaultDht = 0x02 };

/** @brief Pełny stan systemu udostępniany HMI i komunikacji. */
struct SystemSnapshot {
  SensorSnapshot sensors;
  bool running = false;           ///< sygnał pracy maszyny (po filtracji)
  double runTotalH = 0.0;         ///< łączny czas pracy (motogodziny) [h]
  double runSinceServiceH = 0.0;  ///< czas pracy od ostatniego serwisu [h]
  uint32_t starts = 0;            ///< liczba uruchomień maszyny
  uint8_t faults = 0;             ///< maska FaultBit
  // --- wynik analizy predykcyjnej (Etap 3) ---
  pdm::Level status = pdm::Level::kNormal;  ///< OK / PLAN / KONTROLA
  pdm::Cause cause = pdm::Cause::kNone;
  pdm::Driver driver = pdm::Driver::kNone;
  float hoursToInspection = INFINITY;  ///< prognoza [h pracy]
  float tempHoursToAlarm = INFINITY;
  float vibHoursToAlarm = INFINITY;
  float healthIndex = 100.0f;  ///< indeks kondycji [%]
  float lifePct = 0.0f;        ///< wykorzystanie interwału przeglądu [%]
  float equivalentH = 0.0f;    ///< zużycie równoważne [h]
  float wearRate = 1.0f;       ///< tempo zużycia [h równoważnych / h pracy]
  float tempSlope = 0.0f, tempR2 = 0.0f;
  float vibSlope = 0.0f, vibR2 = 0.0f;
  char zone = 'A';            ///< strefa ISO 10816
  uint8_t alarmMask = 0;      ///< kanały w stanie alarmu (bit = pdm::Param)
  bool unacknowledged = false;
  pdm::Level levels[pdm::kParamCount] = {};
};

/** @brief Typy zdarzeń systemowych (dziennik + publikacja MQTT). */
enum class EventType : uint8_t {
  Boot,         ///< start sterownika
  Restored,     ///< odtworzono liczniki z pamięci nieulotnej (value = motogodziny)
  RunStart,     ///< uruchomienie maszyny (value = numer uruchomienia)
  RunStop,      ///< zatrzymanie (value = czas cyklu [h])
  Ack,          ///< kwitowanie (value = liczba potwierdzonych kanałów)
  ServiceDone,  ///< potwierdzony serwis (value = zużycie przed serwisem [%])
  SensorFault,  ///< usterka czujnika (value: 1 = MPU6050, 2 = DHT22)
  SensorOk,     ///< czujnik znów sprawny
  LevelChange,  ///< zmiana poziomu kanału (param, level; value = wartość parametru)
};

/** @brief Zdarzenie systemowe (16 B). */
struct Event {
  uint32_t seq;
  uint32_t timestampMs;
  EventType type;
  uint8_t param;  ///< pdm::Param dla LevelChange (0xFF = nie dotyczy)
  uint8_t level;  ///< pdm::Level dla LevelChange
  float value;
};

/** @brief Rekord historii pomiarów – zapis stałoprzecinkowy, 16 B zamiast ~40 B. */
struct HistoryRecord {
  uint32_t uptimeS;      ///< czas od startu sterownika [s]
  uint32_t machineHx10;  ///< motogodziny x10
  int16_t tempX10;       ///< temperatura x10 (INT16_MIN = brak danych)
  uint16_t vibX100;      ///< prędkość drgań x100 [mm/s]
  uint8_t humidity;      ///< wilgotność [%] (0xFF = brak danych)
  uint8_t health;        ///< indeks kondycji [%]
  uint8_t lifePct;       ///< zużycie [%] (nasycenie 255)
  uint8_t flags;         ///< bit0: praca, bity 1-2: status, bit3: alarm niepotwierdzony
};
static_assert(sizeof(HistoryRecord) == 16, "Rekord historii powinien mieć 16 B");

/** @brief Polecenia przekazywane z MQTT do sterownika. */
enum class RemoteCommand : uint8_t { Ack = 1 };

/** @brief Nazwa zdarzenia używana w dzienniku i w komunikatach MQTT. */
const char* toString(EventType type) {
  switch (type) {
    case EventType::Boot: return "BOOT";
    case EventType::Restored: return "RESTORED";
    case EventType::RunStart: return "RUN_START";
    case EventType::RunStop: return "RUN_STOP";
    case EventType::Ack: return "ACK";
    case EventType::ServiceDone: return "SERVICE_DONE";
    case EventType::SensorFault: return "SENSOR_FAULT";
    case EventType::SensorOk: return "SENSOR_OK";
    case EventType::LevelChange: return "LEVEL";
  }
  return "UNKNOWN";
}

/** @brief Obiekty FreeRTOS współdzielone przez moduły. */
namespace rtos {
QueueHandle_t sensorMailbox = nullptr;  ///< akwizycja -> sterownik (1 element, nadpisywany)
QueueHandle_t systemMailbox = nullptr;  ///< sterownik -> komunikacja (1 element, nadpisywany)
QueueHandle_t eventQueue = nullptr;     ///< sterownik -> komunikacja (FIFO zdarzeń)
QueueHandle_t commandQueue = nullptr;   ///< komunikacja -> sterownik (polecenia zdalne)
TaskHandle_t acqTask = nullptr;
TaskHandle_t controllerTask = nullptr;  ///< zadanie Arduino loop()
TaskHandle_t commsTask = nullptr;
std::atomic<bool> machineRunning{false};  ///< stan RUN dla akwizycji (zamrażanie grawitacji)
}  // namespace rtos

// ============================================================================
// 3. NARZĘDZIA
// ============================================================================

/**
 * @brief Wypisuje linię dziennika ze znacznikiem czasu.
 * @details Formatowanie do bufora na stosie i jeden zapis do UART – bez alokacji na stercie
 *          i bez przeplatania linii z różnych zadań. Nie wywoływać z ISR.
 */
void logMsg(const char* fmt, ...) {
  char line[256];
  const uint32_t ms = millis();
  int n = snprintf(line, sizeof(line), "[%6lu.%03lu] ", (unsigned long)(ms / 1000UL),
                   (unsigned long)(ms % 1000UL));
  va_list args;
  va_start(args, fmt);
  const int m = vsnprintf(line + n, sizeof(line) - n - 1, fmt, args);
  va_end(args);
  n += (m < 0) ? 0 : m;
  if (n > static_cast<int>(sizeof(line)) - 2) n = sizeof(line) - 2;
  line[n++] = '\n';
  Serial.write(reinterpret_cast<const uint8_t*>(line), n);
}

/** @brief Wypisuje tekst bez znacznika czasu (raporty konsoli). */
void printLine(const char* fmt, ...) {
  char line[200];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(line, sizeof(line) - 1, fmt, args);
  va_end(args);
  if (n < 0) n = 0;
  if (n > static_cast<int>(sizeof(line)) - 2) n = sizeof(line) - 2;
  line[n++] = '\n';
  Serial.write(reinterpret_cast<const uint8_t*>(line), n);
}

/** @brief Formatuje liczbę do wyświetlenia; brak odczytu (NaN) jako "---". */
const char* formatValue(char* out, size_t size, float value, uint8_t decimals) {
  if (!isfinite(value)) {
    strlcpy(out, "---", size);
  } else {
    snprintf(out, size, "%.*f", decimals, value);
  }
  return out;
}

/** @brief Formatuje liczbę dla JSON; NaN/Inf zamieniane na null. */
const char* jsonNum(char* out, size_t size, float value, uint8_t decimals) {
  if (!isfinite(value)) {
    strlcpy(out, "null", size);
  } else {
    snprintf(out, size, "%.*f", decimals, value);
  }
  return out;
}

/** @brief Prognoza godzin do kontroli jako tekst ("TERAZ" / "> 9999" / liczba). */
const char* formatForecast(char* out, size_t size, float hours) {
  if (hours <= 0.5f) {
    strlcpy(out, "TERAZ", size);
  } else if (!isfinite(hours) || hours > 9999.0f) {
    strlcpy(out, ">9999 h", size);
  } else {
    snprintf(out, size, "%.0f h", hours);
  }
  return out;
}

// ============================================================================
// 4. WARSTWA PRZERWAŃ
// ============================================================================

/**
 * @brief Wejście cyfrowe obsługiwane przerwaniem (CHANGE) z programową eliminacją drgań styków.
 * @details ISR zapisuje wyłącznie znacznik czasu zbocza, zlicza zbocza i budzi zadanie
 *          sterownika. Nowy stan jest akceptowany dopiero, gdy od ostatniego zbocza minie
 *          cfg::kDebounceUs – niezależnie od liczby odbić (w Wokwi nawet 100 na naciśnięcie).
 *          Wejście aktywne stanem niskim (wewnętrzny pull-up, zestyk zwiera do GND).
 */
class DebouncedInput {
 public:
  DebouncedInput(uint8_t pin, const char* name) : pin_(pin), name_(name) {}

  /** @brief Konfiguruje pin i dołącza procedurę obsługi przerwania (z argumentem this). */
  void begin() {
    pinMode(pin_, INPUT_PULLUP);
    stable_ = readActive();
    changedAtMs_ = millis();
    attachInterruptArg(digitalPinToInterrupt(pin_), &DebouncedInput::isrThunk, this, CHANGE);
  }

  /**
   * @brief Zatwierdza stan po ustaniu drgań styków (wywoływać cyklicznie z zadania).
   * @return true, gdy ustalił się nowy stan wejścia.
   */
  bool update(uint32_t nowUs, uint32_t nowMs) {
    if (pending_.load(std::memory_order_acquire) == 0) return false;
    if (nowUs - lastEdgeUs_.load(std::memory_order_relaxed) < cfg::kDebounceUs) return false;
    pending_.store(0, std::memory_order_relaxed);
    const bool level = readActive();
    if (level == stable_) return false;  // zakłócenie – powrót do poprzedniego stanu
    stable_ = level;
    changedAtMs_ = nowMs;
    return true;
  }

  bool active() const { return stable_; }
  /** @brief Czas trwania stanu aktywnego [ms] (0 gdy nieaktywne). */
  uint32_t heldMs(uint32_t nowMs) const { return stable_ ? nowMs - changedAtMs_ : 0; }
  /** @brief Łączna liczba zboczy zarejestrowanych przez ISR (diagnostyka drgań styków). */
  uint32_t edges() const { return edges_.load(std::memory_order_relaxed); }
  const char* name() const { return name_; }

  /** @brief Wskazuje zadanie budzone przez przerwania wejść. */
  static void setNotifyTask(TaskHandle_t task) { notifyTask_ = task; }

 private:
  bool readActive() const { return digitalRead(pin_) == LOW; }

  // Procedury ISR zdefiniowane poza klasą: funkcja inline z IRAM_ATTR trafiłaby z pulą
  // literałów do innej sekcji niż kod (błąd linkera "l32r: literal placed after use").
  static void isrThunk(void* arg);
  void onEdge();

  const uint8_t pin_;
  const char* const name_;
  std::atomic<uint32_t> lastEdgeUs_{0};
  std::atomic<uint32_t> edges_{0};
  std::atomic<uint32_t> pending_{0};  ///< 32 bity: natywne operacje atomowe Xtensa
  bool stable_ = false;
  uint32_t changedAtMs_ = 0;
  static TaskHandle_t notifyTask_;
};
TaskHandle_t DebouncedInput::notifyTask_ = nullptr;

void IRAM_ATTR DebouncedInput::isrThunk(void* arg) { static_cast<DebouncedInput*>(arg)->onEdge(); }

void IRAM_ATTR DebouncedInput::onEdge() {
  lastEdgeUs_.store(micros(), std::memory_order_relaxed);
  edges_.fetch_add(1, std::memory_order_relaxed);
  // Powiadomienie tylko przy pierwszym zboczu serii – kolejne odbicia nie budzą zadania.
  if (pending_.exchange(1, std::memory_order_acq_rel) == 0 && notifyTask_ != nullptr) {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(notifyTask_, &woken);
    portYIELD_FROM_ISR(woken);
  }
}

/**
 * @brief Sprzętowy timer próbkowania – procedura ISR jedynie budzi zadanie akwizycji.
 * @details Zgodny z API arduino-esp32 2.x i 3.x (Wokwi kompiluje na 3.3.x).
 */
class SampleTimer {
 public:
  /**
   * @param rateHz częstotliwość przerwań [Hz]
   * @param task   zadanie budzone w każdym takcie
   * @return true przy powodzeniu
   */
  bool begin(uint32_t rateHz, TaskHandle_t task) {
    task_ = task;
    const uint32_t periodUs = 1000000UL / rateHz;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    timer_ = timerBegin(1000000);  // zegar 1 MHz -> rozdzielczość 1 µs
    if (timer_ == nullptr) return false;
    timerAttachInterrupt(timer_, &SampleTimer::onAlarm);
    timerAlarm(timer_, periodUs, true, 0);  // auto-reload, bez limitu powtórzeń
#else
    timer_ = timerBegin(0, 80, true);  // 80 MHz / 80 = 1 MHz
    if (timer_ == nullptr) return false;
    timerAttachInterrupt(timer_, &SampleTimer::onAlarm, true);
    timerAlarmWrite(timer_, periodUs, true);
    timerAlarmEnable(timer_);
#endif
    return true;
  }

 private:
  static void onAlarm();  ///< ISR (definicja poza klasą – patrz DebouncedInput)

  hw_timer_t* timer_ = nullptr;
  static TaskHandle_t task_;
};
TaskHandle_t SampleTimer::task_ = nullptr;

void IRAM_ATTR SampleTimer::onAlarm() {
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(task_, &woken);
  portYIELD_FROM_ISR(woken);
}

// ============================================================================
// 5. STEROWNIKI CZUJNIKÓW
// ============================================================================

/**
 * @brief Lekki sterownik MPU6050 operujący bezpośrednio na rejestrach (bez biblioteki).
 * @details Jeden odczyt typu burst (14 B: akcelerometr, temperatura, żyroskop).
 *          Filtr DLPF ~44 Hz pełni rolę filtru antyaliasingowego dla fs = 100 Hz.
 */
class Mpu6050 {
 public:
  /** @brief Próbka pomiarowa w jednostkach fizycznych. */
  struct Sample {
    float accG[3];     ///< przyspieszenie [g]
    float gyroDps[3];  ///< prędkość kątowa [°/s]
    float tempC;       ///< temperatura struktury [°C]
  };

  Mpu6050(TwoWire& bus, uint8_t address) : bus_(bus), addr_(address) {}

  /** @brief Weryfikuje WHO_AM_I i konfiguruje czujnik. @return true gdy czujnik odpowiada. */
  bool begin() {
    uint8_t who = 0;
    if (!readRegs(kRegWhoAmI, &who, 1) || (who != 0x68 && who != 0x98)) return false;
    return writeReg(kRegPwrMgmt1, 0x01)       // wybudzenie, zegar PLL (oś X żyroskopu)
           && writeReg(kRegConfig, 0x03)      // DLPF: pasmo ok. 44 Hz
           && writeReg(kRegSmplrtDiv, 9)      // 1 kHz / (1 + 9) = 100 Hz
           && writeReg(kRegGyroConfig, 0x08)  // zakres ±500 °/s
           && writeReg(kRegAccelConfig, 0x08);  // zakres ±4 g
  }

  /** @brief Odczytuje komplet pomiarów jednym transferem I2C. */
  bool read(Sample& out) {
    uint8_t raw[14];
    if (!readRegs(kRegAccelXoutH, raw, sizeof(raw))) return false;
    // Uwaga: nazwa "word" jest w Arduino makrem (makeWord) – stąd be16 (big-endian, 16 bit).
    auto be16 = [&raw](uint8_t i) { return static_cast<int16_t>((raw[i] << 8) | raw[i + 1]); };
    for (uint8_t axis = 0; axis < 3; ++axis) {
      out.accG[axis] = be16(axis * 2) / kAccLsbPerG;
      out.gyroDps[axis] = be16(8 + axis * 2) / kGyroLsbPerDps;
    }
    out.tempC = be16(6) / 340.0f + 36.53f;
    return true;
  }

 private:
  static constexpr uint8_t kRegSmplrtDiv = 0x19;
  static constexpr uint8_t kRegConfig = 0x1A;
  static constexpr uint8_t kRegGyroConfig = 0x1B;
  static constexpr uint8_t kRegAccelConfig = 0x1C;
  static constexpr uint8_t kRegAccelXoutH = 0x3B;
  static constexpr uint8_t kRegPwrMgmt1 = 0x6B;
  static constexpr uint8_t kRegWhoAmI = 0x75;
  static constexpr float kAccLsbPerG = 8192.0f;   ///< czułość dla ±4 g
  static constexpr float kGyroLsbPerDps = 65.5f;  ///< czułość dla ±500 °/s

  bool writeReg(uint8_t reg, uint8_t value) {
    bus_.beginTransmission(addr_);
    bus_.write(reg);
    bus_.write(value);
    return bus_.endTransmission() == 0;
  }

  bool readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
    bus_.beginTransmission(addr_);
    bus_.write(reg);
    if (bus_.endTransmission(false) != 0) return false;  // powtórzony START
    if (bus_.requestFrom(addr_, len) != len) return false;
    for (uint8_t i = 0; i < len; ++i) buf[i] = static_cast<uint8_t>(bus_.read());
    return true;
  }

  TwoWire& bus_;
  const uint8_t addr_;
};

/**
 * @brief Obsługa DHT22 (temperatura, wilgotność) z wykrywaniem usterki czujnika.
 * @note Biblioteka DHTesp generuje impuls startowy przez delay(2) (w FreeRTOS to vTaskDelay –
 *       procesor jest oddawany innym zadaniom) i odczytuje bity w sekcji krytycznej (~4 ms).
 *       Odczyt co 2,5 s w zadaniu akwizycji nie powoduje utraty próbek (okres 10 ms).
 */
class ClimateSensor {
 public:
  explicit ClimateSensor(uint8_t pin) : pin_(pin) {}

  void begin() { dht_.setup(pin_, DHTesp::DHT22); }

  /** @brief Wykonuje jeden odczyt i aktualizuje licznik kolejnych błędów. */
  void poll() {
    const TempAndHumidity th = dht_.getTempAndHumidity();
    if (dht_.getStatus() == DHTesp::ERROR_NONE && !isnan(th.temperature) && !isnan(th.humidity)) {
      reading_.tempC = th.temperature;
      reading_.humidityPct = th.humidity;
      reading_.valid = true;
      reading_.errorStreak = 0;
      return;
    }
    if (reading_.errorStreak < UINT8_MAX) ++reading_.errorStreak;
    if (reading_.errorStreak >= cfg::kDhtFaultStreak) reading_.valid = false;
  }

  const ClimateReading& reading() const { return reading_; }

 private:
  const uint8_t pin_;
  DHTesp dht_;
  ClimateReading reading_;
};

// ============================================================================
// 6. PRZETWARZANIE SYGNAŁÓW I CZAS PRACY
// ============================================================================

/**
 * @brief Wyznacza cechy drgań w oknach 1 s metodą akumulacyjną (O(1) pamięci).
 * @details Składowa dynamiczna = przyspieszenie minus wektor odniesienia (grawitacja).
 *          Wektor odniesienia adaptuje się filtrem dolnoprzepustowym 1. rzędu tylko na
 *          postoju maszyny i jest zamrażany w czasie pracy. Prędkość drgań szacowana jako
 *          v_RMS = a_RMS / (2*pi*f_dom) – założenie dominującej składowej 1x obroty.
 */
class VibrationAnalyzer {
 public:
  /**
   * @param acc            próbka przyspieszenia [g]
   * @param machineRunning czy maszyna pracuje (zamrożenie wektora odniesienia)
   * @return true, gdy zamknięto okno i dostępne są nowe cechy
   */
  bool addSample(const float acc[3], bool machineRunning) {
    if (!refInit_) {
      for (uint8_t i = 0; i < 3; ++i) ref_[i] = acc[i];
      refInit_ = true;
    }
    if (!machineRunning) {
      for (uint8_t i = 0; i < 3; ++i) ref_[i] += kAlpha * (acc[i] - ref_[i]);
    }
    float d2 = 0.0f;
    for (uint8_t i = 0; i < 3; ++i) {
      const float d = acc[i] - ref_[i];
      d2 += d * d;
    }
    sumSq_ += d2;
    if (d2 > peakSq_) peakSq_ = d2;  // porównanie kwadratów – pierwiastek raz na okno
    if (++count_ < cfg::kWindowSamples) return false;

    features_.aRmsG = sqrtf(static_cast<float>(sumSq_ / count_));
    features_.aPeakG = sqrtf(peakSq_);
    features_.crest = (features_.aRmsG > 1e-4f) ? features_.aPeakG / features_.aRmsG : 0.0f;
    features_.vRmsMmS = features_.aRmsG * kVelocityPerG;
    sumSq_ = 0.0;
    peakSq_ = 0.0f;
    count_ = 0;
    return true;
  }

  const VibrationFeatures& features() const { return features_; }

 private:
  static constexpr float kAlpha = 1.0f / (cfg::kGravityTauS * cfg::kSampleRateHz);
  static constexpr float kVelocityPerG =
      cfg::kStandardGravity * 1000.0f / (2.0f * static_cast<float>(PI) * cfg::kDominantFreqHz);

  float ref_[3] = {0.0f, 0.0f, 0.0f};
  bool refInit_ = false;
  double sumSq_ = 0.0;
  float peakSq_ = 0.0f;
  uint16_t count_ = 0;
  VibrationFeatures features_;
};

/**
 * @brief Licznik czasu pracy maszyny (motogodziny) i liczby uruchomień.
 * @details Czas z 64-bitowego zegara esp_timer [µs] przeliczany na godziny czasu maszyny
 *          (z uwzględnieniem cfg::kTimeScale). Bieżący odcinek pracy dopisywany jest do
 *          liczników przy zmianie stanu, serwisie lub odczycie stanu do zapisu w NVS.
 */
class RunHourMeter {
 public:
  /** @brief Ustala stan początkowy; praca w chwili startu sterownika nie jest liczona jako start. */
  void begin(bool running, uint64_t nowUs) {
    running_ = running;
    segmentStartUs_ = cycleStartUs_ = nowUs;
  }

  /** @brief Odtwarza liczniki z pamięci nieulotnej. */
  void restore(double totalH, double sinceServiceH, uint32_t starts) {
    totalH_ = totalH;
    sinceServiceH_ = sinceServiceH;
    starts_ = starts;
  }

  /** @return czas zakończonego cyklu pracy [h] przy zatrzymaniu, w pozostałych przypadkach 0 */
  double setRunning(bool running, uint64_t nowUs) {
    if (running == running_) return 0.0;
    if (running) {
      running_ = true;
      segmentStartUs_ = cycleStartUs_ = nowUs;
      ++starts_;
      return 0.0;
    }
    commit(nowUs);
    running_ = false;
    return toHours(nowUs - cycleStartUs_);
  }

  /** @brief Potwierdzenie serwisu. @return czas pracy od poprzedniego serwisu [h]. */
  double resetService(uint64_t nowUs) {
    commit(nowUs);
    const double before = sinceServiceH_;
    sinceServiceH_ = 0.0;
    return before;
  }

  double totalHours(uint64_t nowUs) const { return totalH_ + openHours(nowUs); }
  double sinceServiceHours(uint64_t nowUs) const { return sinceServiceH_ + openHours(nowUs); }
  uint32_t starts() const { return starts_; }
  bool running() const { return running_; }

 private:
  static double toHours(uint64_t us) { return static_cast<double>(us) * cfg::kTimeScale / 3.6e9; }
  double openHours(uint64_t nowUs) const { return running_ ? toHours(nowUs - segmentStartUs_) : 0.0; }

  void commit(uint64_t nowUs) {
    const double h = openHours(nowUs);
    totalH_ += h;
    sinceServiceH_ += h;
    segmentStartUs_ = nowUs;
  }

  bool running_ = false;
  uint64_t segmentStartUs_ = 0;
  uint64_t cycleStartUs_ = 0;
  double totalH_ = 0.0;
  double sinceServiceH_ = 0.0;
  uint32_t starts_ = 0;
};

// ============================================================================
// 7. PAMIĘĆ NIEULOTNA (NVS)
// ============================================================================

/** @brief Liczniki zapisywane w NVS (40 B, z sumą kontrolną CRC-16). */
struct PersistedCounters {
  uint32_t magic = kMagic;
  uint16_t version = kVersion;
  uint16_t crc = 0;
  double totalH = 0.0;          ///< motogodziny
  double sinceServiceH = 0.0;   ///< godziny od serwisu
  float equivalentH = 0.0f;     ///< zużycie równoważne (model zużycia)
  uint32_t starts = 0;          ///< liczba uruchomień
  uint32_t services = 0;        ///< liczba potwierdzonych serwisów
  uint32_t alarms = 0;          ///< liczba alarmów od początku eksploatacji

  static constexpr uint32_t kMagic = 0x50444D33;  // "PDM3"
  static constexpr uint16_t kVersion = 1;
};
static_assert(sizeof(PersistedCounters) == 40, "Uklad PersistedCounters bez wypelnien");

/**
 * @brief Zapis i odczyt liczników w pamięci nieulotnej (NVS, biblioteka Preferences).
 * @details Jeden blok binarny z wersją i sumą CRC-16/CCITT – uszkodzony lub niezgodny zapis
 *          jest odrzucany. NVS sam rozkłada zapisy po sektorach (wear leveling); zapis co 60 s
 *          i przy ważnych zdarzeniach ogranicza liczbę cykli kasowania flash.
 * @note W Wokwi pamięć flash jest odtwarzana przy każdym starcie symulacji – trwałość widać
 *       po restarcie programowym (polecenie konsoli "restart").
 */
class CounterStore {
 public:
  bool begin() { return prefs_.begin(cfg::kNvsNamespace, false); }

  /** @return true, gdy odczytano poprawny zapis */
  bool load(PersistedCounters& out) {
    PersistedCounters tmp;
    if (prefs_.getBytes("cnt", &tmp, sizeof(tmp)) != sizeof(tmp)) return false;
    if (tmp.magic != PersistedCounters::kMagic || tmp.version != PersistedCounters::kVersion) return false;
    const uint16_t stored = tmp.crc;
    tmp.crc = 0;
    if (crc16(reinterpret_cast<const uint8_t*>(&tmp), sizeof(tmp)) != stored) return false;
    tmp.crc = stored;
    out = tmp;
    return true;
  }

  bool save(PersistedCounters c) {
    c.crc = 0;
    c.crc = crc16(reinterpret_cast<const uint8_t*>(&c), sizeof(c));
    return prefs_.putBytes("cnt", &c, sizeof(c)) == sizeof(c);
  }

 private:
  /** @brief CRC-16/CCITT-FALSE (wielomian 0x1021, wartość początkowa 0xFFFF). */
  static uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
      crc ^= static_cast<uint16_t>(data[i]) << 8;
      for (uint8_t b = 0; b < 8; ++b) crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : crc << 1;
    }
    return crc;
  }

  Preferences prefs_;
};

// ============================================================================
// 8. HMI: SYGNALIZACJA I WYŚWIETLACZ
// ============================================================================

/** @brief Wzorce świecenia lampek sygnalizacyjnych. */
enum class Pattern : uint8_t { Off, On, Blink, FastBlink, Heartbeat };

/**
 * @brief Kolumna sygnalizacyjna (LED R/Y/G) i buzzer – nieblokujące wzorce czasowe.
 */
class Indicators {
 public:
  enum Lamp : uint8_t { kRed = 0, kYellow = 1, kGreen = 2 };

  /** @brief Konfiguruje wyjścia i uruchamia test lampek (wszystkie świecą + sygnał). */
  void begin(uint32_t nowMs) {
    for (uint8_t i = 0; i < 3; ++i) {
      pinMode(pins_[i], OUTPUT);
      digitalWrite(pins_[i], LOW);
    }
    lampTestUntilMs_ = nowMs + cfg::kLampTestMs;
    beep(2000, 200, 1);
  }

  void set(Lamp lamp, Pattern pattern) { patterns_[lamp] = pattern; }

  /** @brief Włącza/wyłącza cykliczny sygnał niepotwierdzonego alarmu. */
  void setAlarmSound(bool on) { alarmSound_ = on; }

  /** @brief Planuje sekwencję sygnałów dźwiękowych (realizowaną w update()). */
  void beep(uint16_t freqHz, uint16_t onMs, uint8_t count, uint16_t offMs = 120) {
    beepFreqHz_ = freqHz;
    beepOnMs_ = onMs;
    beepOffMs_ = offMs;
    beepsLeft_ = count;
    beepNextMs_ = millis();
  }

  /** @brief Odświeża wyjścia; zapis do GPIO tylko przy zmianie stanu. */
  void update(uint32_t nowMs) {
    const bool lampTest = static_cast<int32_t>(lampTestUntilMs_ - nowMs) > 0;
    for (uint8_t i = 0; i < 3; ++i) {
      const bool on = lampTest || evaluate(patterns_[i], nowMs);
      if (on != state_[i]) {
        digitalWrite(pins_[i], on ? HIGH : LOW);
        state_[i] = on;
      }
    }
    if (alarmSound_ && beepsLeft_ == 0 && !beepOn_ && nowMs - lastAlarmBeepMs_ >= cfg::kAlarmBeepPeriodMs) {
      lastAlarmBeepMs_ = nowMs;
      beep(2400, 90, 2, 90);  // "pip-pip" co 2 s do czasu potwierdzenia
    }
    updateBuzzer(nowMs);
  }

 private:
  static bool evaluate(Pattern pattern, uint32_t nowMs) {
    switch (pattern) {
      case Pattern::On: return true;
      case Pattern::Blink: return ((nowMs / 500) & 1U) != 0;
      case Pattern::FastBlink: return ((nowMs / 125) & 1U) != 0;
      case Pattern::Heartbeat: return (nowMs % 2000) < 120;
      default: return false;
    }
  }

  void updateBuzzer(uint32_t nowMs) {
    if ((beepsLeft_ == 0 && !beepOn_) || static_cast<int32_t>(nowMs - beepNextMs_) < 0) return;
    if (beepOn_) {
      noTone(cfg::kPinBuzzer);
      beepOn_ = false;
      beepNextMs_ = nowMs + beepOffMs_;
    } else {
      tone(cfg::kPinBuzzer, beepFreqHz_);
      beepOn_ = true;
      --beepsLeft_;
      beepNextMs_ = nowMs + beepOnMs_;
    }
  }

  const uint8_t pins_[3] = {cfg::kPinLedRed, cfg::kPinLedYellow, cfg::kPinLedGreen};
  Pattern patterns_[3] = {Pattern::Off, Pattern::Off, Pattern::Off};
  bool state_[3] = {false, false, false};
  uint32_t lampTestUntilMs_ = 0;
  uint16_t beepFreqHz_ = 0, beepOnMs_ = 0, beepOffMs_ = 0;
  uint8_t beepsLeft_ = 0;
  bool beepOn_ = false;
  uint32_t beepNextMs_ = 0;
  bool alarmSound_ = false;
  uint32_t lastAlarmBeepMs_ = 0;
};

/** @brief Historia pomiarów (typ wspólny dla sterownika i wyświetlacza). */
using History = pdm::RingBuffer<HistoryRecord, cfg::kHistorySize>;

/**
 * @brief Wyświetlacz OLED SSD1306 (128x64, osobna magistrala I2C1) z trzema ekranami
 *        zmienianymi co cfg::kScreenPeriodMs: stan maszyny, prognoza, trend indeksu kondycji.
 */
class StatusDisplay {
 public:
  explicit StatusDisplay(TwoWire& bus) : oled_(128, 64, &bus, -1, cfg::kI2cClockHz, cfg::kI2cClockHz) {}

  /** @return true, gdy wyświetlacz odpowiada (bufor ramki 1 KB alokowany jednorazowo). */
  bool begin() {
    ok_ = oled_.begin(SSD1306_SWITCHCAPVCC, cfg::kOledAddress, true, false);
    if (ok_) {
      oled_.clearDisplay();
      oled_.setTextColor(SSD1306_WHITE);
      oled_.setTextWrap(false);
      oled_.display();
    }
    return ok_;
  }

  /** @brief Rysuje bieżący ekran (21 znaków x 6 wierszy) i wysyła ramkę przez I2C1. */
  void render(const SystemSnapshot& s, const History& history, bool wifiUp, bool mqttUp, uint32_t nowMs) {
    if (!ok_) return;
    oled_.clearDisplay();
    oled_.setTextSize(1);
    switch ((nowMs / cfg::kScreenPeriodMs) % 3) {
      case 0: renderStatus(s, wifiUp, mqttUp); break;
      case 1: renderForecast(s); break;
      default: renderTrend(s, history); break;
    }
    oled_.display();  // ~1 KB przez I2C1 – nie koliduje z magistralą czujników
  }

 private:
  static const char* statusText(pdm::Level l) { return pdm::levelCode(pdm::Param::kMaint, l); }

  void renderStatus(const SystemSnapshot& s, bool wifiUp, bool mqttUp) {
    char line[24], f[12];
    const ClimateReading& c = s.sensors.climate;
    snprintf(line, sizeof(line), "%-6s %-8s %c %c", s.running ? "PRACA" : "POSTOJ", statusText(s.status),
             wifiUp ? 'W' : '-', mqttUp ? 'M' : '-');
    printAt(0, 0, line);
    oled_.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    if (c.valid) {
      snprintf(line, sizeof(line), "T %5.1fC   RH %3.0f%%", c.tempC, c.humidityPct);
    } else {
      snprintf(line, sizeof(line), "T  ---     RH ---");
    }
    printAt(0, 13, line);
    snprintf(line, sizeof(line), "V %5.2fmm/s  ISO %c", s.sensors.vib.vRmsMmS, s.zone);
    printAt(0, 23, line);
    snprintf(line, sizeof(line), "HI %3.0f%%   Zuz. %3.0f%%", s.healthIndex, s.lifePct);
    printAt(0, 33, line);
    snprintf(line, sizeof(line), "Mth %6.1fh  St %lu", s.runTotalH, (unsigned long)s.starts);
    printAt(0, 43, line);
    snprintf(line, sizeof(line), "Kontrola za %s", formatForecast(f, sizeof(f), s.hoursToInspection));
    printAt(0, 55, line);
  }

  void renderForecast(const SystemSnapshot& s) {
    char line[24], f[12];
    snprintf(line, sizeof(line), "PROGNOZA [%s]", statusText(s.status));
    printAt(0, 0, line);
    oled_.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    snprintf(line, sizeof(line), "Kontrola za %s", formatForecast(f, sizeof(f), s.hoursToInspection));
    printAt(0, 12, line);
    snprintf(line, sizeof(line), "Wg: %s", pdm::driverText(s.driver));
    printAt(0, 21, line);
    printAt(0, 30, "Przyczyna:");
    printAt(0, 39, pdm::causeText(s.cause));
    snprintf(line, sizeof(line), "dT %+5.2fC/h R2 %.2f", s.tempSlope, s.tempR2);
    printAt(0, 48, line);
    snprintf(line, sizeof(line), "dV %+5.2f/h  R2 %.2f", s.vibSlope, s.vibR2);
    printAt(0, 56, line);
  }

  /** @brief Wykres indeksu kondycji z historii (1 piksel = 1 rekord) z liniami progów 70 % i 40 %. */
  void renderTrend(const SystemSnapshot& s, const History& history) {
    char line[24];
    snprintf(line, sizeof(line), "Kondycja %3.0f%%  10min", s.healthIndex);
    printAt(0, 0, line);
    constexpr int16_t kTop = 11, kBottom = 63, kHeight = kBottom - kTop;
    for (int16_t x = 0; x < 128; x += 4) {  // linie przerywane progów PLAN (70 %) i KONTROLA (40 %)
      oled_.drawPixel(x, kBottom - kHeight * 70 / 100, SSD1306_WHITE);
      oled_.drawPixel(x, kBottom - kHeight * 40 / 100, SSD1306_WHITE);
    }
    const size_t n = history.size();
    for (size_t i = 1; i < n; ++i) {
      const int16_t x0 = static_cast<int16_t>(128 - n + i - 1), x1 = static_cast<int16_t>(128 - n + i);
      const int16_t y0 = kBottom - kHeight * history.at(i - 1).health / 100;
      const int16_t y1 = kBottom - kHeight * history.at(i).health / 100;
      oled_.drawLine(x0, y0, x1, y1, SSD1306_WHITE);
    }
  }

  void printAt(int16_t x, int16_t y, const char* text) {
    oled_.setCursor(x, y);
    oled_.print(text);
  }

  Adafruit_SSD1306 oled_;
  bool ok_ = false;
};

// ============================================================================
// 9. KOMUNIKACJA: WiFi + MQTT (zadanie na rdzeniu 0)
// ============================================================================

/**
 * @brief Łącze IoT: utrzymanie połączenia WiFi/MQTT, publikacja telemetrii i zdarzeń, polecenia.
 * @details Pracuje we własnym zadaniu na rdzeniu 0 (razem ze stosem WiFi), dlatego blokujące
 *          operacje sieciowe (DNS, TCP, CONNECT) nie wpływają na akwizycję na rdzeniu 1.
 *          Broker zapasowy, wykładnicze opóźnienie ponowień i LWT ("offline", retained).
 *
 * Tematy: <base>/telemetry (co 5 s), <base>/event, <base>/status (retained), <base>/cmd.
 * Polecenia: "ping", "status", "ack". Potwierdzenia serwisu celowo nie da się wydać zdalnie –
 * wymaga obecności przy maszynie (przycisk SERWIS lub konsola lokalna).
 */
class MqttLink {
 public:
  /** @brief Przygotowuje klienta i uruchamia zadanie komunikacji. */
  bool begin() {
    snprintf(clientId_, sizeof(clientId_), "wtp4-pdm-%08lx", (unsigned long)random(0x7FFFFFFF));
    snprintf(topicTelemetry_, sizeof(topicTelemetry_), "%s/telemetry", cfg::kTopicBase);
    snprintf(topicEvent_, sizeof(topicEvent_), "%s/event", cfg::kTopicBase);
    snprintf(topicStatus_, sizeof(topicStatus_), "%s/status", cfg::kTopicBase);
    snprintf(topicCmd_, sizeof(topicCmd_), "%s/cmd", cfg::kTopicBase);
    mqtt_.setBufferSize(cfg::kMqttBufferSize);
    mqtt_.setKeepAlive(cfg::kMqttKeepAliveS);
    mqtt_.setSocketTimeout(5);
    mqtt_.setCallback([this](char* topic, uint8_t* payload, unsigned int len) { onMessage(topic, payload, len); });
    return xTaskCreatePinnedToCore(&MqttLink::taskEntry, "comms", cfg::kCommsStack, this, cfg::kCommsPriority,
                                   &rtos::commsTask, 0) == pdPASS;
  }

  bool wifiUp() const { return wifiUp_.load(std::memory_order_relaxed); }
  bool mqttUp() const { return mqttUp_.load(std::memory_order_relaxed); }
  uint32_t sent() const { return sent_.load(std::memory_order_relaxed); }

 private:
  static void taskEntry(void* arg) { static_cast<MqttLink*>(arg)->run(); }

  /** @brief Pętla zadania: czeka na zdarzenia (maks. 50 ms), utrzymuje łącze, publikuje dane. */
  void run() {
    WiFi.mode(WIFI_STA);
    startWifi(millis());
    uint32_t lastTelemetryMs = 0;
    for (;;) {
      Event event;
      if (xQueueReceive(rtos::eventQueue, &event, pdMS_TO_TICKS(50)) == pdTRUE) publishEvent(event);
      const uint32_t nowMs = millis();
      maintain(nowMs);
      if (mqtt_.connected()) {
        mqtt_.loop();
        // LWT poprzedniej, przerwanej sesji może zostać opublikowany przez broker dopiero po
        // upływie jej keep-alive, czyli PO naszym "online" – cykliczne odświeżenie koryguje status.
        if (nowMs - lastOnlineMs_ >= cfg::kStatusRefreshMs) {
          lastOnlineMs_ = nowMs;
          mqtt_.publish(topicStatus_, "online", true);
        }
      }
      if (forceTelemetry_ || nowMs - lastTelemetryMs >= cfg::kTelemetryPeriodMs) {
        forceTelemetry_ = false;
        lastTelemetryMs = nowMs;
        SystemSnapshot snapshot;
        if (xQueuePeek(rtos::systemMailbox, &snapshot, 0) == pdTRUE) publishTelemetry(snapshot);
      }
    }
  }

  void startWifi(uint32_t nowMs) {
    if (wifiStarted_) WiFi.disconnect();
    WiFi.begin(cfg::kWifiSsid, cfg::kWifiPass, cfg::kWifiChannel);
    wifiStarted_ = true;
    wifiAttemptMs_ = nowMs;
    logMsg("[SIEĆ] Łączenie z WiFi \"%s\"...", cfg::kWifiSsid);
  }

  /** @brief Maszyna stanów połączenia: WiFi -> MQTT -> online, z ponawianiem. */
  void maintain(uint32_t nowMs) {
    if (WiFi.status() != WL_CONNECTED) {
      if (wifiUp()) {
        wifiUp_.store(false);
        mqttUp_.store(false);
        logMsg("[SIEĆ] Utracono WiFi");
      }
      if (nowMs - wifiAttemptMs_ >= cfg::kWifiRetryMs) startWifi(nowMs);
      return;
    }
    if (!wifiUp()) {
      wifiUp_.store(true);
      const IPAddress ip = WiFi.localIP();
      logMsg("[SIEĆ] WiFi połączone, IP %u.%u.%u.%u, RSSI %d dBm", ip[0], ip[1], ip[2], ip[3], (int)WiFi.RSSI());
      backoffMs_ = 0;
    }
    if (mqtt_.connected()) return;
    if (mqttUp()) {
      mqttUp_.store(false);
      logMsg("[MQTT] Rozłączono (stan %d)", mqtt_.state());
    }
    if (nowMs - mqttAttemptMs_ < backoffMs_) return;
    mqttAttemptMs_ = nowMs;

    const char* host = cfg::kMqttHosts[hostIndex_];
    mqtt_.setServer(host, cfg::kMqttPort);
    logMsg("[MQTT] Łączenie z %s:%u jako %s...", host, cfg::kMqttPort, clientId_);
    if (mqtt_.connect(clientId_, nullptr, nullptr, topicStatus_, 1, true, "offline")) {
      mqttUp_.store(true);
      backoffMs_ = cfg::kMqttBackoffMinMs;
      mqtt_.publish(topicStatus_, "online", true);
      lastOnlineMs_ = nowMs;
      mqtt_.subscribe(topicCmd_, 1);
      logMsg("[MQTT] Połączono. Telemetria: %s", topicTelemetry_);
      forceTelemetry_ = true;
    } else {
      backoffMs_ = (backoffMs_ == 0) ? cfg::kMqttBackoffMinMs : min(backoffMs_ * 2, cfg::kMqttBackoffMaxMs);
      hostIndex_ = (hostIndex_ + 1) % cfg::kMqttHostCount;  // następna próba – broker zapasowy
      logMsg("[MQTT] Błąd połączenia (stan %d), ponowienie za %lu ms", mqtt_.state(), (unsigned long)backoffMs_);
    }
  }

  void publishTelemetry(const SystemSnapshot& s) {
    if (!mqtt_.connected()) return;
    const SensorSnapshot& d = s.sensors;
    char t[12], rh[12], mpuT[12], insp[12];
    const int n = snprintf(
        buf_, sizeof(buf_),
        "{\"seq\":%lu,\"ts\":%lu,\"run\":%d,\"t\":%s,\"rh\":%s,\"dht_ok\":%d,"
        "\"a_rms\":%.4f,\"a_peak\":%.4f,\"cf\":%.2f,\"v_rms\":%.3f,\"zone\":\"%c\","
        "\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,\"mpu_t\":%s,\"mpu_ok\":%d,"
        "\"run_h\":%.2f,\"svc_h\":%.2f,\"starts\":%lu,"
        "\"hi\":%.1f,\"wear\":%.1f,\"eq_h\":%.1f,\"rate\":%.2f,"
        "\"state\":\"%s\",\"cause\":\"%s\",\"insp_h\":%s,\"drv\":\"%s\","
        "\"t_tr\":%.3f,\"t_r2\":%.2f,\"v_tr\":%.3f,\"v_r2\":%.2f,"
        "\"alarms\":%u,\"unack\":%d,\"faults\":%u,\"ovr\":%lu,\"rssi\":%d,\"heap\":%lu}",
        (unsigned long)d.seq, (unsigned long)d.timestampMs, s.running ? 1 : 0,
        jsonNum(t, sizeof(t), d.climate.valid ? d.climate.tempC : NAN, 1),
        jsonNum(rh, sizeof(rh), d.climate.valid ? d.climate.humidityPct : NAN, 1),
        (s.faults & kFaultDht) ? 0 : 1, d.vib.aRmsG, d.vib.aPeakG, d.vib.crest, d.vib.vRmsMmS, s.zone, d.accelG[0],
        d.accelG[1], d.accelG[2], jsonNum(mpuT, sizeof(mpuT), d.mpuTempC, 1), d.mpuOk ? 1 : 0, s.runTotalH,
        s.runSinceServiceH, (unsigned long)s.starts, s.healthIndex, s.lifePct, s.equivalentH, s.wearRate,
        pdm::levelCode(pdm::Param::kMaint, s.status), pdm::causeCode(s.cause),
        jsonNum(insp, sizeof(insp), s.hoursToInspection, 1), pdm::driverText(s.driver), s.tempSlope, s.tempR2,
        s.vibSlope, s.vibR2, (unsigned)s.alarmMask, s.unacknowledged ? 1 : 0, (unsigned)s.faults,
        (unsigned long)d.overruns, (int)WiFi.RSSI(), (unsigned long)ESP.getFreeHeap());
    if (n > 0 && n < static_cast<int>(sizeof(buf_)) && mqtt_.publish(topicTelemetry_, buf_)) {
      sent_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void publishEvent(const Event& e) {
    if (!mqtt_.connected()) return;  // zdarzenie pozostaje w dzienniku sterownika (historia w RAM)
    if (e.type == EventType::LevelChange) {
      const pdm::Param p = static_cast<pdm::Param>(e.param);
      snprintf(buf_, sizeof(buf_),
               "{\"seq\":%lu,\"ts\":%lu,\"type\":\"%s\",\"param\":\"%s\",\"level\":\"%s\",\"value\":%.2f}",
               (unsigned long)e.seq, (unsigned long)e.timestampMs, toString(e.type), pdm::paramCode(p),
               pdm::levelCode(p, static_cast<pdm::Level>(e.level)), e.value);
    } else {
      snprintf(buf_, sizeof(buf_), "{\"seq\":%lu,\"ts\":%lu,\"type\":\"%s\",\"value\":%.3f}", (unsigned long)e.seq,
               (unsigned long)e.timestampMs, toString(e.type), e.value);
    }
    if (mqtt_.publish(topicEvent_, buf_)) sent_.fetch_add(1, std::memory_order_relaxed);
  }

  /** @brief Obsługa poleceń z tematu <base>/cmd (w kontekście zadania komunikacji). */
  void onMessage(char* /*topic*/, uint8_t* payload, unsigned int len) {
    char cmd[32];
    const unsigned int n = (len < sizeof(cmd) - 1) ? len : sizeof(cmd) - 1;
    memcpy(cmd, payload, n);  // kopia przed publikacją (bufor klienta jest współdzielony)
    cmd[n] = '\0';
    logMsg("[MQTT] Polecenie: \"%s\"", cmd);
    if (strcmp(cmd, "ping") == 0) {
      mqtt_.publish(topicEvent_, "{\"type\":\"PONG\"}");
    } else if (strcmp(cmd, "status") == 0) {
      forceTelemetry_ = true;
    } else if (strcmp(cmd, "ack") == 0) {
      const RemoteCommand rc = RemoteCommand::Ack;  // wykonanie w zadaniu sterownika
      xQueueSend(rtos::commandQueue, &rc, 0);
      xTaskNotifyGive(rtos::controllerTask);
    } else {
      logMsg("[MQTT] Nieznane polecenie (obsługiwane: ping, status, ack)");
    }
  }

  WiFiClient net_;
  PubSubClient mqtt_{net_};
  char buf_[cfg::kMqttBufferSize];
  char clientId_[24];
  char topicTelemetry_[48], topicEvent_[48], topicStatus_[48], topicCmd_[48];
  std::atomic<bool> wifiUp_{false};
  std::atomic<bool> mqttUp_{false};
  std::atomic<uint32_t> sent_{0};
  bool wifiStarted_ = false;
  bool forceTelemetry_ = false;
  uint8_t hostIndex_ = 0;
  uint32_t wifiAttemptMs_ = 0;
  uint32_t mqttAttemptMs_ = 0;
  uint32_t backoffMs_ = 0;
  uint32_t lastOnlineMs_ = 0;
};

// ============================================================================
// 10. AKWIZYCJA (zadanie na rdzeniu 1, priorytet 3)
// ============================================================================

/**
 * @brief Zadanie akwizycji: próbkuje MPU6050 w takt timera, co 2,5 s czyta DHT22,
 *        wyznacza cechy drgań i publikuje migawkę do skrzynki sterownika.
 * @details Zadanie śpi na powiadomieniu z ISR (ulTaskNotifyTake) – zero aktywnego czekania.
 *          Licznik powiadomień > 1 oznacza pominięty takt (przeciążenie) i jest raportowany.
 */
class AcquisitionService {
 public:
  AcquisitionService() : mpu_(Wire, cfg::kMpuAddress), climate_(cfg::kPinDht) {}

  /** @brief Inicjalizuje czujniki, tworzy zadanie i uruchamia timer próbkowania. */
  bool begin() {
    Wire.begin(cfg::kPinI2c0Sda, cfg::kPinI2c0Scl, cfg::kI2cClockHz);
    Wire.setTimeOut(cfg::kI2cTimeoutMs);
    snap_.mpuOk = mpu_.begin();
    logMsg("[AKW] MPU6050: %s", snap_.mpuOk ? "OK (±4 g, ±500 °/s, DLPF 44 Hz)" : "BRAK ODPOWIEDZI");
    climate_.begin();
    if (xTaskCreatePinnedToCore(&AcquisitionService::taskEntry, "acq", cfg::kAcqStack, this, cfg::kAcqPriority,
                                &rtos::acqTask, 1) != pdPASS) {
      return false;
    }
    return timer_.begin(cfg::kSampleRateHz, rtos::acqTask);
  }

 private:
  static void taskEntry(void* arg) { static_cast<AcquisitionService*>(arg)->run(); }

  void run() {
    const bool wdt = (esp_task_wdt_add(nullptr) == ESP_OK);  // nadzór watchdoga zadania
    uint32_t lastDhtMs = millis() - cfg::kDhtPeriodMs;      // pierwszy odczyt DHT od razu
    uint32_t lastRetryMs = 0;
    uint8_t mpuErrors = 0;
    for (;;) {
      const uint32_t ticks = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
      if (wdt) esp_task_wdt_reset();
      if (ticks == 0) continue;  // brak taktu timera w ciągu 100 ms
      if (ticks > 1) snap_.overruns += ticks - 1;
      const uint32_t nowMs = millis();
      bool publish = false;

      if (snap_.mpuOk) {
        Mpu6050::Sample sample;
        if (mpu_.read(sample)) {
          mpuErrors = 0;
          memcpy(snap_.accelG, sample.accG, sizeof(snap_.accelG));
          snap_.mpuTempC = sample.tempC;
          ++snap_.samples;
          if (vib_.addSample(sample.accG, rtos::machineRunning.load(std::memory_order_relaxed))) {
            ++snap_.windows;
            publish = true;
          }
        } else if (++mpuErrors >= cfg::kMpuFaultStreak) {
          snap_.mpuOk = false;  // usterka – kolejne próby co kMpuRetryMs
          lastRetryMs = nowMs;
          publish = true;
        }
      } else if (nowMs - lastRetryMs >= cfg::kMpuRetryMs) {
        lastRetryMs = nowMs;
        mpuErrors = 0;
        snap_.mpuOk = mpu_.begin();
        ++snap_.windows;  // analiza trwa także bez drgań (pozostałe parametry)
        publish = true;
      }

      if (nowMs - lastDhtMs >= cfg::kDhtPeriodMs) {
        lastDhtMs = nowMs;
        climate_.poll();
        publish = true;
      }

      if (publish) {
        snap_.vib = vib_.features();
        snap_.climate = climate_.reading();
        snap_.timestampMs = nowMs;
        ++snap_.seq;
        xQueueOverwrite(rtos::sensorMailbox, &snap_);
        xTaskNotifyGive(rtos::controllerTask);  // obudź sterownik – są nowe dane
      }
    }
  }

  Mpu6050 mpu_;
  ClimateSensor climate_;
  VibrationAnalyzer vib_;
  SampleTimer timer_;
  SensorSnapshot snap_;
};

// ============================================================================
// 11. STEROWNIK (Arduino loop(), rdzeń 1, priorytet 1)
// ============================================================================

/**
 * @brief Logika aplikacji: wejścia operatora, licznik czasu pracy, analiza predykcyjna,
 *        alarmy, historia, pamięć nieulotna, sygnalizacja, wyświetlacz i konsola szeregowa.
 *
 * Konsola (monitor portu szeregowego, 115200 bd): pomoc, stan, historia [n], zdarzenia [n],
 * ack, serwis, zuzycie <%>, zapisz, restart.
 */
class MachineController {
 public:
  MachineController()
      : btnAck_(cfg::kPinBtnAck, "ACK"),
        btnService_(cfg::kPinBtnService, "SERWIS"),
        runInput_(cfg::kPinRun, "RUN"),
        display_(Wire1),
        engine_(makeEngineConfig()) {}

  /** @brief Inicjalizacja w kontekście zadania loop() (musi poprzedzać start akwizycji). */
  void begin(const MqttLink* link) {
    link_ = link;
    rtos::controllerTask = xTaskGetCurrentTaskHandle();
    DebouncedInput::setNotifyTask(rtos::controllerTask);
    btnAck_.begin();
    btnService_.begin();
    runInput_.begin();
    const uint64_t now64 = esp_timer_get_time();
    hours_.begin(runInput_.active(), now64);
    rtos::machineRunning.store(runInput_.active());
    sys_.running = runInput_.active();

    indicators_.begin(millis());
    Wire1.begin(cfg::kPinI2c1Sda, cfg::kPinI2c1Scl, cfg::kI2cClockHz);
    Wire1.setTimeOut(cfg::kI2cTimeoutMs);
    logMsg("[HMI] OLED: %s", display_.begin() ? "OK (I2C1)" : "BRAK ODPOWIEDZI");

    emit(EventType::Boot, 0.0f);
    restoreCounters();
    lastAnalysisTotalH_ = hours_.totalHours(now64);
    if (sys_.running) logMsg("[RUN] Maszyna pracuje w chwili startu sterownika");
    logMsg("[KONSOLA] Wpisz \"pomoc\", aby zobaczyć listę poleceń");
  }

  /** @brief Jedna iteracja: czekaj na zdarzenie (ISR/dane) maks. 10 ms, potem obsłuż. */
  void tick() {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(cfg::kControllerTickMs));
    const uint32_t nowMs = millis();
    const uint32_t nowUs = micros();
    const uint64_t now64 = esp_timer_get_time();

    handleRemoteCommands(now64);
    pollConsole(now64);
    handleInputs(nowUs, nowMs, now64);
    handleSensors(now64);
    refreshIndicators();
    indicators_.update(nowMs);

    if (nowMs - lastDisplayMs_ >= cfg::kDisplayPeriodMs) {
      lastDisplayMs_ = nowMs;
      refreshSystem(now64);
      display_.render(sys_, history_, link_->wifiUp(), link_->mqttUp(), nowMs);
    }
    if (nowMs - lastStatusMs_ >= cfg::kStatusPeriodMs) {
      lastStatusMs_ = nowMs;
      printStatus();
    }
    if (nowMs - lastHistoryMs_ >= cfg::kHistoryPeriodMs) {
      lastHistoryMs_ = nowMs;
      recordHistory(nowMs);
    }
    if (nowMs - lastPersistMs_ >= cfg::kPersistPeriodMs) {
      lastPersistMs_ = nowMs;
      persist(now64, false);
    }
    if (nowMs - lastDiagMs_ >= cfg::kDiagPeriodMs) {
      lastDiagMs_ = nowMs;
      printDiagnostics();
    }
  }

 private:
  static pdm::EngineConfig makeEngineConfig() {
    pdm::EngineConfig c;
    c.stepHours = cfg::kStepHours;  // odstęp próbek trendów w godzinach czasu maszyny
    return c;
  }

  // --------------------------------------------------------------- wejścia
  void handleInputs(uint32_t nowUs, uint32_t nowMs, uint64_t now64) {
    if (btnAck_.update(nowUs, nowMs) && btnAck_.active()) acknowledge("przycisk");

    if (btnService_.update(nowUs, nowMs)) {
      serviceArmed_ = btnService_.active();  // naciśnięcie uzbraja, zwolnienie anuluje
      if (serviceArmed_) {
        logMsg("[SERWIS] Przytrzymaj %lu s, aby potwierdzić wykonanie serwisu",
               (unsigned long)(cfg::kServiceHoldMs / 1000));
        indicators_.beep(1800, 40, 1);
      }
    }
    if (serviceArmed_ && btnService_.heldMs(nowMs) >= cfg::kServiceHoldMs) {
      serviceArmed_ = false;
      confirmService(now64, "przycisk");
    }

    if (runInput_.update(nowUs, nowMs)) {
      const bool running = runInput_.active();
      rtos::machineRunning.store(running, std::memory_order_relaxed);
      const double cycleH = hours_.setRunning(running, now64);
      sys_.running = running;
      if (running) {
        engine_.onMachineStart();  // zużycie rozruchu, nowe okno trendów
        emit(EventType::RunStart, static_cast<float>(hours_.starts()));
      } else {
        emit(EventType::RunStop, static_cast<float>(cycleH));
        persist(now64, true);
      }
    }
  }

  void handleRemoteCommands(uint64_t now64) {
    RemoteCommand cmd;
    while (xQueueReceive(rtos::commandQueue, &cmd, 0) == pdTRUE) {
      if (cmd == RemoteCommand::Ack) acknowledge("MQTT");
    }
    (void)now64;
  }

  /** @brief Kwitowanie alarmów (ISA-18.2) – wycisza sygnał, światło czerwone przechodzi w ciągłe. */
  void acknowledge(const char* source) {
    const size_t n = engine_.acknowledgeAll();
    logMsg("[ALARM] Kwitowanie (%s): potwierdzono %u kanał(ów)", source, (unsigned)n);
    emit(EventType::Ack, static_cast<float>(n));
    indicators_.beep(2500, 80, 1);
  }

  /** @brief Potwierdzenie wykonanego serwisu: zerowanie zużycia i licznika od serwisu. */
  void confirmService(uint64_t now64, const char* source) {
    const float lifeBefore = engine_.wear().lifeFraction() * 100.0f;
    const double sinceH = hours_.resetService(now64);
    engine_.serviceDone();
    ++services_;
    logMsg("[SERWIS] Potwierdzono (%s): zużycie przed serwisem %.0f%%, praca od poprzedniego %.1f h", source,
           lifeBefore, sinceH);
    emit(EventType::ServiceDone, lifeBefore);
    indicators_.beep(2000, 120, 2);
    persist(now64, true);
  }

  // --------------------------------------------------------------- czujniki i analiza
  void handleSensors(uint64_t now64) {
    SensorSnapshot s;
    if (xQueuePeek(rtos::sensorMailbox, &s, 0) != pdTRUE || s.seq == lastSensorSeq_) return;
    lastSensorSeq_ = s.seq;

    const uint8_t faults = (s.mpuOk ? 0 : kFaultMpu) | (s.climate.errorStreak >= cfg::kDhtFaultStreak ? kFaultDht : 0);
    const uint8_t raised = faults & ~sys_.faults;
    const uint8_t cleared = sys_.faults & ~faults;
    if (raised & kFaultMpu) emit(EventType::SensorFault, 1.0f);
    if (raised & kFaultDht) emit(EventType::SensorFault, 2.0f);
    if (cleared & kFaultMpu) emit(EventType::SensorOk, 1.0f);
    if (cleared & kFaultDht) emit(EventType::SensorOk, 2.0f);

    sys_.sensors = s;
    sys_.faults = faults;
    if (s.windows != lastWindows_) {  // nowe okno drgań = jeden krok analizy (1 s)
      lastWindows_ = s.windows;
      analyze(now64);
    }
    refreshSystem(now64);
  }

  /** @brief Krok analizy predykcyjnej: dane -> pdm::PredictiveEngine -> zdarzenia i stan systemu. */
  void analyze(uint64_t now64) {
    const double total = hours_.totalHours(now64);
    pdm::EngineInput in;
    in.running = sys_.running;
    in.workHours = static_cast<float>(total - lastAnalysisTotalH_);
    lastAnalysisTotalH_ = total;
    const ClimateReading& c = sys_.sensors.climate;
    const bool dhtOk = c.valid && !(sys_.faults & kFaultDht);
    in.tempC = c.tempC;
    in.tempValid = dhtOk;
    in.humidityPct = c.humidityPct;
    in.humidityValid = dhtOk;
    in.vibMmS = sys_.sensors.vib.vRmsMmS;
    in.vibValid = sys_.sensors.mpuOk;
    in.sensorFault = sys_.faults != 0;

    pdm::LevelChange changes[8];
    const size_t n = engine_.step(in, changes, 8);
    for (size_t i = 0; i < n; ++i) {
      const pdm::LevelChange& ch = changes[i];
      if (ch.param == pdm::Param::kSensor) continue;  // usterki raportują zdarzenia SENSOR_FAULT/OK
      if (ch.to == pdm::Level::kAlarm) ++alarms_;
      emitLevel(ch);
    }

    const pdm::EngineOutput& o = engine_.output();
    sys_.status = o.status;
    sys_.cause = o.cause;
    sys_.driver = o.driver;
    sys_.hoursToInspection = o.hoursToInspection;
    sys_.tempHoursToAlarm = o.tempHoursToAlarm;
    sys_.vibHoursToAlarm = o.vibHoursToAlarm;
    sys_.healthIndex = o.health.index;
    sys_.lifePct = o.lifeFraction * 100.0f;
    sys_.equivalentH = engine_.wear().equivalentHours();
    sys_.wearRate = o.wearRate;
    sys_.tempSlope = o.tempSlope;
    sys_.tempR2 = o.tempR2;
    sys_.vibSlope = o.vibSlope;
    sys_.vibR2 = o.vibR2;
    sys_.zone = o.zone;
    sys_.alarmMask = engine_.alarmMask();
    sys_.unacknowledged = engine_.anyUnacknowledged();
    for (size_t i = 0; i < pdm::kParamCount; ++i) sys_.levels[i] = engine_.channel(static_cast<pdm::Param>(i)).level();
  }

  /** @brief Aktualizuje liczniki czasu pracy i publikuje stan do skrzynki komunikacji. */
  void refreshSystem(uint64_t now64) {
    sys_.runTotalH = hours_.totalHours(now64);
    sys_.runSinceServiceH = hours_.sinceServiceHours(now64);
    sys_.starts = hours_.starts();
    xQueueOverwrite(rtos::systemMailbox, &sys_);
  }

  /**
   * @brief Sygnalizacja: zielona = OK (ciągła w pracy, puls na postoju), żółta = zaplanuj przegląd,
   *        czerwona = alarmy wg ISA-18.2 (szybkie miganie + dźwięk: niepotwierdzony,
   *        ciągła: potwierdzony, wolne miganie: ustąpił bez potwierdzenia).
   */
  void refreshIndicators() {
    const pdm::Level st = sys_.status;
    indicators_.set(Indicators::kGreen,
                    st == pdm::Level::kNormal ? (sys_.running ? Pattern::On : Pattern::Heartbeat) : Pattern::Off);
    indicators_.set(Indicators::kYellow, st == pdm::Level::kWarning ? Pattern::On : Pattern::Off);
    Pattern red = Pattern::Off;
    switch (engine_.worstAlarmState()) {
      case pdm::AlarmState::kUnackActive: red = Pattern::FastBlink; break;
      case pdm::AlarmState::kAckActive: red = Pattern::On; break;
      case pdm::AlarmState::kUnackCleared: red = Pattern::Blink; break;
      default: break;
    }
    indicators_.set(Indicators::kRed, red);
    indicators_.setAlarmSound(engine_.worstAlarmState() == pdm::AlarmState::kUnackActive);
  }

  // --------------------------------------------------------------- zdarzenia i historia
  void emit(EventType type, float value) { push(Event{0, 0, type, 0xFF, 0, value}); }

  void emitLevel(const pdm::LevelChange& ch) {
    push(Event{0, 0, EventType::LevelChange, static_cast<uint8_t>(ch.param), static_cast<uint8_t>(ch.to), ch.value});
  }

  /** @brief Nadaje numer i czas, zapisuje w dzienniku RAM, wypisuje i przekazuje do MQTT. */
  void push(Event e) {
    e.seq = ++eventSeq_;
    e.timestampMs = static_cast<uint32_t>(millis());
    eventLog_.push(e);
    char text[64];
    describe(e, text, sizeof(text));
    logMsg("[ZDARZENIE #%lu] %s", (unsigned long)e.seq, text);
    if (xQueueSend(rtos::eventQueue, &e, 0) != pdTRUE) ++eventsDropped_;
  }

  static void describe(const Event& e, char* out, size_t size) {
    if (e.type == EventType::LevelChange) {
      const pdm::Param p = static_cast<pdm::Param>(e.param);
      const pdm::Level l = static_cast<pdm::Level>(e.level);
      if (p == pdm::Param::kMaint) {
        snprintf(out, size, "STAN -> %s (kontrola za %.0f h)", pdm::levelCode(p, l), e.value);
      } else {
        snprintf(out, size, "%s -> %s (%.2f)", pdm::paramCode(p), pdm::levelCode(p, l), e.value);
      }
    } else {
      snprintf(out, size, "%s (%.2f)", toString(e.type), e.value);
    }
  }

  void recordHistory(uint32_t nowMs) {
    HistoryRecord r;
    const ClimateReading& c = sys_.sensors.climate;
    r.uptimeS = nowMs / 1000;
    r.machineHx10 = static_cast<uint32_t>(sys_.runTotalH * 10.0);
    r.tempX10 = c.valid ? static_cast<int16_t>(lroundf(c.tempC * 10.0f)) : INT16_MIN;
    r.vibX100 = static_cast<uint16_t>(pdm::clampf(sys_.sensors.vib.vRmsMmS * 100.0f, 0.0f, 65535.0f));
    r.humidity = c.valid ? static_cast<uint8_t>(pdm::clampf(c.humidityPct, 0.0f, 100.0f)) : 0xFF;
    r.health = static_cast<uint8_t>(pdm::clampf(sys_.healthIndex, 0.0f, 100.0f));
    r.lifePct = static_cast<uint8_t>(pdm::clampf(sys_.lifePct, 0.0f, 255.0f));
    r.flags = static_cast<uint8_t>((sys_.running ? 0x01 : 0) | (static_cast<uint8_t>(sys_.status) << 1) |
                                   (sys_.unacknowledged ? 0x08 : 0));
    history_.push(r);
  }

  // --------------------------------------------------------------- pamięć nieulotna
  void restoreCounters() {
    if (!store_.begin()) {
      logMsg("[NVS] Niedostępna – liczniki tylko w RAM");
      return;
    }
    PersistedCounters c;
    if (!store_.load(c)) {
      logMsg("[NVS] Brak zapisanych liczników – start od zera");
      return;
    }
    hours_.restore(c.totalH, c.sinceServiceH, c.starts);
    engine_.wear().setEquivalentHours(c.equivalentH);
    services_ = c.services;
    alarms_ = c.alarms;
    logMsg("[NVS] Odtworzono: %.1f mth, od serwisu %.1f h, zużycie %.1f h, starty %lu, serwisy %lu", c.totalH,
           c.sinceServiceH, c.equivalentH, (unsigned long)c.starts, (unsigned long)c.services);
    emit(EventType::Restored, static_cast<float>(c.totalH));
  }

  /** @brief Zapis liczników do NVS (okresowo lub natychmiast przy ważnym zdarzeniu). */
  void persist(uint64_t now64, bool force) {
    PersistedCounters c;
    c.totalH = hours_.totalHours(now64);
    c.sinceServiceH = hours_.sinceServiceHours(now64);
    c.equivalentH = engine_.wear().equivalentHours();
    c.starts = hours_.starts();
    c.services = services_;
    c.alarms = alarms_;
    if (!force && c.totalH == lastPersistedH_ && c.equivalentH == lastPersistedEq_) return;  // bez zmian
    if (store_.save(c)) {
      lastPersistedH_ = c.totalH;
      lastPersistedEq_ = c.equivalentH;
    } else {
      logMsg("[NVS] Błąd zapisu");
    }
  }

  // --------------------------------------------------------------- konsola szeregowa
  void pollConsole(uint64_t now64) {
    while (Serial.available() > 0) {
      const int ch = Serial.read();
      if (ch == '\n' || ch == '\r') {
        if (consoleLen_ > 0) {
          consoleBuf_[consoleLen_] = '\0';
          execute(consoleBuf_, now64);
          consoleLen_ = 0;
        }
      } else if (ch >= 0 && consoleLen_ < sizeof(consoleBuf_) - 1) {
        consoleBuf_[consoleLen_++] = static_cast<char>(ch);
      }
    }
  }

  void execute(char* line, uint64_t now64) {
    char* arg = strchr(line, ' ');
    if (arg != nullptr) {
      *arg++ = '\0';
      while (*arg == ' ') ++arg;
    }
    for (char* p = line; *p; ++p) *p = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
    const int n = (arg != nullptr && *arg) ? atoi(arg) : -1;

    if (strcmp(line, "pomoc") == 0 || strcmp(line, "help") == 0) {
      printLine("Polecenia konsoli:");
      printLine("  stan            - pelny raport (parametry, alarmy, prognoza, zuzycie)");
      printLine("  historia [n]    - ostatnie n rekordow historii (domyslnie 10, maks. %u)",
                (unsigned)cfg::kHistorySize);
      printLine("  zdarzenia [n]   - ostatnie n zdarzen (domyslnie 10, maks. %u)", (unsigned)cfg::kEventLogSize);
      printLine("  ack             - kwitowanie alarmow (jak przycisk ACK)");
      printLine("  serwis          - potwierdzenie serwisu (jak przytrzymanie przycisku SERWIS)");
      printLine("  zuzycie <proc>  - ustawienie zuzycia, np. zuzycie 85 (demonstracja/testy)");
      printLine("  zapisz          - natychmiastowy zapis licznikow do NVS");
      printLine("  restart         - zapis licznikow i restart sterownika (test pamieci nieulotnej)");
    } else if (strcmp(line, "stan") == 0) {
      printReport();
    } else if (strcmp(line, "historia") == 0) {
      printHistory(n > 0 ? static_cast<size_t>(n) : 10);
    } else if (strcmp(line, "zdarzenia") == 0) {
      printEvents(n > 0 ? static_cast<size_t>(n) : 10);
    } else if (strcmp(line, "ack") == 0) {
      acknowledge("konsola");
    } else if (strcmp(line, "serwis") == 0) {
      confirmService(now64, "konsola");
    } else if (strcmp(line, "zuzycie") == 0 && n >= 0) {
      const float pct = pdm::clampf(static_cast<float>(n), 0.0f, 150.0f);
      engine_.wear().setEquivalentHours(pct / 100.0f * engine_.wear().config().serviceIntervalH);
      logMsg("[KONSOLA] Ustawiono zużycie %.0f%% (%.1f h równoważnych)", pct, engine_.wear().equivalentHours());
    } else if (strcmp(line, "zapisz") == 0) {
      persist(now64, true);
      logMsg("[NVS] Zapisano liczniki");
    } else if (strcmp(line, "restart") == 0) {
      persist(now64, true);
      logMsg("[SYSTEM] Restart sterownika...");
      Serial.flush();
      ESP.restart();
    } else {
      logMsg("[KONSOLA] Nieznane polecenie \"%s\" – wpisz \"pomoc\"", line);
    }
  }

  void printReport() {
    const pdm::EngineOutput& o = engine_.output();
    const ClimateReading& c = sys_.sensors.climate;
    char a[16], b[16], f[16];
    printLine("================ RAPORT STANU MASZYNY ================");
    printLine("Praca: %s | motogodziny %.1f h | od serwisu %.1f h | starty %lu | serwisy %lu | alarmy %lu",
              sys_.running ? "TAK" : "NIE", sys_.runTotalH, sys_.runSinceServiceH, (unsigned long)sys_.starts,
              (unsigned long)services_, (unsigned long)alarms_);
    for (size_t i = 0; i < 3; ++i) {
      const pdm::Param p = static_cast<pdm::Param>(i);
      const pdm::AlarmChannel& ch = engine_.channel(p);
      const float v = (p == pdm::Param::kTemp) ? c.tempC : (p == pdm::Param::kVib ? sys_.sensors.vib.vRmsMmS : c.humidityPct);
      printLine("  %-6s %8s  poziom %-11s alarm %-12s progi %.1f / %.1f", pdm::paramCode(p), formatValue(a, sizeof(a), v, 2),
                pdm::levelCode(p, ch.level()), alarmStateText(ch.state()), ch.thresholds().warn,
                ch.thresholds().alarm);
    }
    // Oceny cząstkowe niedostępnych czujników nie wchodzą do indeksu – wypisywane jako "---".
    printLine("Kondycja: %.0f%% (temp %s, drgania %s, wilg. %s, zuzycie %.2f) | strefa ISO %c", o.health.index,
              formatValue(a, sizeof(a), c.valid ? o.health.temp : NAN, 2),
              formatValue(b, sizeof(b), sys_.sensors.mpuOk ? o.health.vib : NAN, 2),
              formatValue(f, sizeof(f), c.valid ? o.health.hum : NAN, 2), o.health.wear, o.zone);
    printLine("Zuzycie: %.1f h rownowaznych = %.0f%% interwalu %.0f h | tempo x%.2f", engine_.wear().equivalentHours(),
              o.lifeFraction * 100.0f, engine_.wear().config().serviceIntervalH, o.wearRate);
    printLine("Trend temp.: %+.3f C/h (R2 %.2f) -> alarm za %s | trend drgan: %+.3f mm/s/h (R2 %.2f) -> alarm za %s",
              o.tempSlope, o.tempR2, formatForecast(a, sizeof(a), o.tempHoursToAlarm), o.vibSlope, o.vibR2,
              formatForecast(b, sizeof(b), o.vibHoursToAlarm));
    printLine("PROGNOZA: kontrola za %s (wg: %s) | STAN: %s | przyczyna: %s",
              formatForecast(f, sizeof(f), o.hoursToInspection), pdm::driverText(o.driver),
              pdm::levelCode(pdm::Param::kMaint, o.status), pdm::causeText(o.cause));
    printLine("======================================================");
  }

  static const char* alarmStateText(pdm::AlarmState s) {
    switch (s) {
      case pdm::AlarmState::kUnackActive: return "NIEPOTWIERDZ.";
      case pdm::AlarmState::kAckActive: return "POTWIERDZONY";
      case pdm::AlarmState::kUnackCleared: return "USTAPIL-NIEP.";
      default: return "-";
    }
  }

  void printHistory(size_t count) {
    const size_t n = history_.size() < count ? history_.size() : count;
    printLine("HISTORIA (ostatnie %u z %u rekordow, co %lu s):", (unsigned)n, (unsigned)history_.size(),
              (unsigned long)(cfg::kHistoryPeriodMs / 1000));
    printLine("   czas[s]   mth[h]   T[C]  RH[%%]  V[mm/s]  HI[%%]  zuz[%%]  praca  stan");
    for (size_t i = history_.size() - n; i < history_.size(); ++i) {
      const HistoryRecord& r = history_.at(i);
      char t[10], rh[8];
      if (r.tempX10 == INT16_MIN) {
        strlcpy(t, "---", sizeof(t));
      } else {
        snprintf(t, sizeof(t), "%.1f", r.tempX10 / 10.0f);
      }
      if (r.humidity == 0xFF) {
        strlcpy(rh, "---", sizeof(rh));
      } else {
        snprintf(rh, sizeof(rh), "%u", r.humidity);
      }
      const pdm::Level st = static_cast<pdm::Level>((r.flags >> 1) & 0x03);
      printLine("  %8lu  %7.1f  %5s  %5s  %7.2f  %5u  %6u  %5s  %s%s", (unsigned long)r.uptimeS, r.machineHx10 / 10.0, t,
                rh, r.vibX100 / 100.0, r.health, r.lifePct, (r.flags & 0x01) ? "tak" : "nie",
                pdm::levelCode(pdm::Param::kMaint, st), (r.flags & 0x08) ? " (alarm niepotw.)" : "");
    }
  }

  void printEvents(size_t count) {
    const size_t n = eventLog_.size() < count ? eventLog_.size() : count;
    printLine("ZDARZENIA (ostatnie %u z %u):", (unsigned)n, (unsigned)eventLog_.size());
    for (size_t i = eventLog_.size() - n; i < eventLog_.size(); ++i) {
      const Event& e = eventLog_.at(i);
      char text[64];
      describe(e, text, sizeof(text));
      printLine("  #%-4lu %8lu.%03lu s  %s", (unsigned long)e.seq, (unsigned long)(e.timestampMs / 1000),
                (unsigned long)(e.timestampMs % 1000), text);
    }
  }

  // --------------------------------------------------------------- dziennik okresowy
  void printStatus() {
    const SensorSnapshot& d = sys_.sensors;
    char t[12], rh[12], f[16];
    logMsg("%s | T=%s°C RH=%s%% | v=%.2fmm/s ISO %c (a=%.3fg CF=%.2f) | HI %.0f%% zużycie %.0f%% x%.1f | kontrola "
           "za %s (%s) | %s %s | mth %.1f, starty %lu | MQTT %s",
           sys_.running ? "PRACA " : "POSTÓJ", formatValue(t, sizeof(t), d.climate.valid ? d.climate.tempC : NAN, 1),
           formatValue(rh, sizeof(rh), d.climate.valid ? d.climate.humidityPct : NAN, 0), d.vib.vRmsMmS, sys_.zone,
           d.vib.aRmsG, d.vib.crest, sys_.healthIndex, sys_.lifePct, sys_.wearRate,
           formatForecast(f, sizeof(f), sys_.hoursToInspection), pdm::driverText(sys_.driver),
           pdm::levelCode(pdm::Param::kMaint, sys_.status), sys_.unacknowledged ? "[ALARM - wymagane ACK]" : "",
           sys_.runTotalH, (unsigned long)sys_.starts, link_->mqttUp() ? "online" : "offline");
  }

  void printDiagnostics() {
    logMsg("[DIAG] sterta %lu B (min %lu B) | wolny stos: acq %u B, comms %u B, loop %u B | próbki %lu, "
           "pominięte takty %lu | zbocza ACK=%lu SERWIS=%lu RUN=%lu | historia %u/%u, zdarzenia %u/%u | MQTT "
           "wysłane %lu, utracone %lu",
           (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
           (unsigned)uxTaskGetStackHighWaterMark(rtos::acqTask), (unsigned)uxTaskGetStackHighWaterMark(rtos::commsTask),
           (unsigned)uxTaskGetStackHighWaterMark(nullptr), (unsigned long)sys_.sensors.samples,
           (unsigned long)sys_.sensors.overruns, (unsigned long)btnAck_.edges(), (unsigned long)btnService_.edges(),
           (unsigned long)runInput_.edges(), (unsigned)history_.size(), (unsigned)cfg::kHistorySize,
           (unsigned)eventLog_.size(), (unsigned)cfg::kEventLogSize, (unsigned long)link_->sent(),
           (unsigned long)eventsDropped_);
  }

  DebouncedInput btnAck_;
  DebouncedInput btnService_;
  DebouncedInput runInput_;
  RunHourMeter hours_;
  Indicators indicators_;
  StatusDisplay display_;
  pdm::PredictiveEngine engine_;
  CounterStore store_;
  History history_;
  pdm::RingBuffer<Event, cfg::kEventLogSize> eventLog_;
  const MqttLink* link_ = nullptr;
  SystemSnapshot sys_;
  double lastAnalysisTotalH_ = 0.0;
  double lastPersistedH_ = -1.0;
  float lastPersistedEq_ = -1.0f;
  uint32_t lastSensorSeq_ = 0;
  uint32_t lastWindows_ = 0;
  uint32_t lastDisplayMs_ = 0;
  uint32_t lastStatusMs_ = 0;
  uint32_t lastHistoryMs_ = 0;
  uint32_t lastPersistMs_ = 0;
  uint32_t lastDiagMs_ = 0;
  uint32_t eventSeq_ = 0;
  uint32_t eventsDropped_ = 0;
  uint32_t services_ = 0;
  uint32_t alarms_ = 0;
  char consoleBuf_[48] = {};
  size_t consoleLen_ = 0;
  bool serviceArmed_ = false;
};

// ============================================================================
// 12. OBIEKTY GLOBALNE (alokacja statyczna) I PUNKTY WEJŚCIA ARDUINO
// ============================================================================
static MqttLink g_link;
static AcquisitionService g_acquisition;
static MachineController g_controller;

void setup() {
  Serial.begin(115200);
  logMsg("==================================================");
  logMsg(" WTP4 - Inteligentny system utrzymania ruchu maszyn");
  logMsg(" %s | arduino-esp32 %d.%d.%d | CPU %lu MHz", cfg::kFirmware, ESP_ARDUINO_VERSION_MAJOR,
         ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH, (unsigned long)getCpuFrequencyMhz());
  logMsg(" Czas maszyny przyspieszony x%lu (1 s = %.1f h pracy)", (unsigned long)cfg::kTimeScale, cfg::kStepHours);
  logMsg("==================================================");

  rtos::sensorMailbox = xQueueCreate(1, sizeof(SensorSnapshot));
  rtos::systemMailbox = xQueueCreate(1, sizeof(SystemSnapshot));
  rtos::eventQueue = xQueueCreate(cfg::kEventQueueLen, sizeof(Event));
  rtos::commandQueue = xQueueCreate(cfg::kCommandQueueLen, sizeof(RemoteCommand));
  if (!rtos::sensorMailbox || !rtos::systemMailbox || !rtos::eventQueue || !rtos::commandQueue) {
    logMsg("[BŁĄD] Brak pamięci na kolejki FreeRTOS");
  }

  g_controller.begin(&g_link);  // wejścia + HMI + NVS (uchwyt zadania loop() dla powiadomień)
  if (!g_acquisition.begin()) logMsg("[BŁĄD] Nie udało się uruchomić akwizycji");
  if (!g_link.begin()) logMsg("[BŁĄD] Nie udało się uruchomić zadania komunikacji");
  enableLoopWDT();  // watchdog nadzoruje także pętlę sterownika
  logMsg("[SYSTEM] Gotowy: próbkowanie %lu Hz, okno drgań %u próbek, analiza co %.0f s",
         (unsigned long)cfg::kSampleRateHz, (unsigned)cfg::kWindowSamples, cfg::kAnalysisPeriodS);
}

void loop() { g_controller.tick(); }
