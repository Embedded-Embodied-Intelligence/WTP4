/**
 * @file    main.cpp
 * @brief   Inteligentny system utrzymania ruchu maszyn (Predictive Maintenance) – ESP32.
 *
 * @details ETAP 2 – rdzeń oprogramowania:
 *  - akwizycja drgań (MPU6050, 100 Hz) taktowana przerwaniem timera sprzętowego,
 *  - wejścia operatora (ACK, SERWIS) i sygnał pracy maszyny (RUN) obsługiwane
 *    przerwaniami GPIO z programową eliminacją drgań styków,
 *  - podstawowe przetwarzanie: RMS, wartość szczytowa i współczynnik szczytu drgań,
 *    estymata prędkości drgań [mm/s], temperatura i wilgotność (DHT22),
 *    licznik czasu pracy (motogodziny) i liczby uruchomień,
 *  - HMI: OLED (osobna magistrala I2C1), kolumna LED, sygnalizator akustyczny,
 *  - telemetria MQTT (WiFi, LWT, ponawianie z wykładniczym opóźnieniem, broker zapasowy).
 *
 * Architektura (FreeRTOS, przepływ danych w jedną stronę, bez blokującego delay()):
 * @verbatim
 *  Timer ISR (100 Hz) --notify--> [Akwizycja: rdzeń 1, prio 3] --skrzynka--+
 *                                                                          v
 *  GPIO ISR (ACK/SERWIS/RUN) --notify--> [Sterownik loop(): rdzeń 1, prio 1] --> OLED/LED/buzzer/Serial
 *                                                   | skrzynka + kolejka zdarzeń
 *                                                   v
 *                                      [Komunikacja: rdzeń 0, prio 1] --> MQTT
 * @endverbatim
 *
 * Zasady: brak alokacji pamięci w pętli pracy (bufory statyczne, brak klasy String),
 * procedury ISR ograniczone do zapisu znacznika czasu i powiadomienia zadania.
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <atomic>
#include <math.h>
#include <stdarg.h>

// ============================================================================
// 1. KONFIGURACJA
// ============================================================================
namespace cfg {

constexpr char kFirmware[] = "WTP4-PdM 0.2.0 (Etap 2)";

// --- Piny (zgodne z diagram.json) ---
constexpr uint8_t kPinI2c0Sda = 21;     ///< I2C0 (Wire): MPU6050
constexpr uint8_t kPinI2c0Scl = 22;
constexpr uint8_t kPinI2c1Sda = 33;     ///< I2C1 (Wire1): OLED SSD1306
constexpr uint8_t kPinI2c1Scl = 32;
constexpr uint8_t kPinDht = 16;         ///< DHT22 (na GPIO4 model Wokwi zwraca TIMEOUT)
constexpr uint8_t kPinBtnAck = 19;      ///< przycisk ACK – kwitowanie
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
constexpr float kDominantFreqHz = 24.7f;  ///< 1x obroty silnika 4-biegunowego (~1480 obr/min)
constexpr float kStandardGravity = 9.80665f;

// --- Wejścia ---
constexpr uint32_t kDebounceUs = 30000;    ///< wymagany czas stabilnego stanu (30 ms)
constexpr uint32_t kServiceHoldMs = 3000;  ///< potwierdzenie serwisu przytrzymaniem 3 s

// --- HMI ---
constexpr uint32_t kControllerTickMs = 10;  ///< maks. czas oczekiwania sterownika na zdarzenie
constexpr uint32_t kDisplayPeriodMs = 500;
constexpr uint32_t kStatusPeriodMs = 1000;
constexpr uint32_t kDiagPeriodMs = 30000;
constexpr uint32_t kLampTestMs = 1500;  ///< test lampek po starcie (praktyka przemysłowa)

// --- Czas pracy ---
constexpr uint32_t kTimeScale = 1;  ///< przyspieszenie zegara maszyny w symulacji (Etap 3)

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
constexpr uint16_t kMqttBufferSize = 512;
constexpr uint16_t kMqttKeepAliveS = 30;

// --- Zadania FreeRTOS (rozmiary stosu w bajtach) ---
constexpr uint32_t kAcqStack = 4096;
constexpr UBaseType_t kAcqPriority = 3;
constexpr uint32_t kCommsStack = 6144;
constexpr UBaseType_t kCommsPriority = 1;
constexpr uint8_t kEventQueueLen = 16;

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
  bool running = false;            ///< sygnał pracy maszyny (po filtracji)
  uint64_t runTotalMs = 0;         ///< łączny czas pracy (motogodziny)
  uint64_t runSinceServiceMs = 0;  ///< czas pracy od ostatniego serwisu
  uint32_t starts = 0;             ///< liczba uruchomień maszyny
  uint8_t faults = 0;              ///< maska FaultBit
};

/** @brief Typy zdarzeń systemowych (dziennik + publikacja MQTT). */
enum class EventType : uint8_t { Boot, RunStart, RunStop, Ack, ServiceDone, SensorFault, SensorOk };

/** @brief Zdarzenie systemowe. Znaczenie pola value zależy od typu (patrz MachineController). */
struct Event {
  uint32_t seq;
  uint32_t timestampMs;
  EventType type;
  float value;
};

/** @brief Nazwa zdarzenia używana w dzienniku i w komunikatach MQTT. */
const char* toString(EventType type) {
  switch (type) {
    case EventType::Boot: return "BOOT";
    case EventType::RunStart: return "RUN_START";
    case EventType::RunStop: return "RUN_STOP";
    case EventType::Ack: return "ACK";
    case EventType::ServiceDone: return "SERVICE_DONE";
    case EventType::SensorFault: return "SENSOR_FAULT";
    case EventType::SensorOk: return "SENSOR_OK";
  }
  return "UNKNOWN";
}

/** @brief Obiekty FreeRTOS współdzielone przez moduły. */
namespace rtos {
QueueHandle_t sensorMailbox = nullptr;  ///< akwizycja -> sterownik (1 element, nadpisywany)
QueueHandle_t systemMailbox = nullptr;  ///< sterownik -> komunikacja (1 element, nadpisywany)
QueueHandle_t eventQueue = nullptr;     ///< sterownik -> komunikacja (FIFO zdarzeń)
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

/** @brief Zamienia czas [ms] na tekst "G:MM:SS". */
void formatHms(uint64_t ms, char* out, size_t size) {
  const uint64_t s = ms / 1000ULL;
  snprintf(out, size, "%lu:%02u:%02u", (unsigned long)(s / 3600ULL), (unsigned)((s / 60ULL) % 60ULL),
           (unsigned)(s % 60ULL));
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
 * @details Czas z 64-bitowego zegara esp_timer [µs] – brak przepełnienia w praktyce.
 *          Bieżący odcinek pracy jest dopisywany do liczników przy zmianie stanu lub serwisie.
 */
class RunHourMeter {
 public:
  /** @brief Ustala stan początkowy; praca w chwili startu sterownika nie jest liczona jako start. */
  void begin(bool running, uint64_t nowUs) {
    running_ = running;
    segmentStartUs_ = cycleStartUs_ = nowUs;
  }

  /** @return czas zakończonego cyklu pracy [ms] przy zatrzymaniu, w pozostałych przypadkach 0 */
  uint64_t setRunning(bool running, uint64_t nowUs) {
    if (running == running_) return 0;
    if (running) {
      running_ = true;
      segmentStartUs_ = cycleStartUs_ = nowUs;
      ++starts_;
      return 0;
    }
    commit(nowUs);
    running_ = false;
    return (nowUs - cycleStartUs_) * cfg::kTimeScale / 1000ULL;
  }

  /** @brief Potwierdzenie serwisu. @return czas pracy od poprzedniego serwisu [ms]. */
  uint64_t resetService(uint64_t nowUs) {
    commit(nowUs);
    const uint64_t beforeMs = sinceServiceUs_ / 1000ULL;
    sinceServiceUs_ = 0;
    return beforeMs;
  }

  uint64_t totalMs(uint64_t nowUs) const { return (totalUs_ + openUs(nowUs)) / 1000ULL; }
  uint64_t sinceServiceMs(uint64_t nowUs) const { return (sinceServiceUs_ + openUs(nowUs)) / 1000ULL; }
  uint32_t starts() const { return starts_; }
  bool running() const { return running_; }

 private:
  uint64_t openUs(uint64_t nowUs) const { return running_ ? (nowUs - segmentStartUs_) * cfg::kTimeScale : 0; }

  void commit(uint64_t nowUs) {
    const uint64_t seg = openUs(nowUs);
    totalUs_ += seg;
    sinceServiceUs_ += seg;
    segmentStartUs_ = nowUs;
  }

  bool running_ = false;
  uint64_t segmentStartUs_ = 0;
  uint64_t cycleStartUs_ = 0;
  uint64_t totalUs_ = 0;
  uint64_t sinceServiceUs_ = 0;
  uint32_t starts_ = 0;
};

// ============================================================================
// 7. HMI: SYGNALIZACJA I WYŚWIETLACZ
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
};

/** @brief Ekran statusu na OLED SSD1306 (128x64, osobna magistrala I2C1). */
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

  /** @brief Rysuje ekran statusu (21 znaków x 6 wierszy). */
  void render(const SystemSnapshot& s, bool wifiUp, bool mqttUp) {
    if (!ok_) return;
    char line[24];
    char hms[16];
    const ClimateReading& c = s.sensors.climate;
    const VibrationFeatures& v = s.sensors.vib;

    oled_.clearDisplay();
    oled_.setTextSize(1);
    snprintf(line, sizeof(line), "%-6s   %s %s", s.running ? "PRACA" : "POSTOJ", wifiUp ? "WiFi" : "----",
             mqttUp ? "MQTT" : "----");
    printAt(0, 0, line);
    oled_.drawFastHLine(0, 10, 128, SSD1306_WHITE);

    if (c.valid) {
      snprintf(line, sizeof(line), "T %5.1fC   RH %3.0f%%", c.tempC, c.humidityPct);
    } else {
      snprintf(line, sizeof(line), "T  ---     RH ---");
    }
    printAt(0, 14, line);
    snprintf(line, sizeof(line), "Vrms %6.2f mm/s", v.vRmsMmS);
    printAt(0, 24, line);
    snprintf(line, sizeof(line), "a %5.3fg  CF %4.2f", v.aRmsG, v.crest);
    printAt(0, 34, line);
    formatHms(s.runTotalMs, hms, sizeof(hms));
    snprintf(line, sizeof(line), "Praca %s St%lu", hms, (unsigned long)s.starts);
    printAt(0, 44, line);
    snprintf(line, sizeof(line), "MPU %s  DHT %s", (s.faults & kFaultMpu) ? "ERR" : "OK",
             (s.faults & kFaultDht) ? "ERR" : "OK");
    printAt(0, 55, line);
    oled_.display();  // ~1 KB przez I2C1 – nie koliduje z magistralą czujników
  }

 private:
  void printAt(int16_t x, int16_t y, const char* text) {
    oled_.setCursor(x, y);
    oled_.print(text);
  }

  Adafruit_SSD1306 oled_;
  bool ok_ = false;
};

// ============================================================================
// 8. KOMUNIKACJA: WiFi + MQTT (zadanie na rdzeniu 0)
// ============================================================================

/**
 * @brief Łącze IoT: utrzymanie połączenia WiFi/MQTT, publikacja telemetrii i zdarzeń.
 * @details Pracuje we własnym zadaniu na rdzeniu 0 (razem ze stosem WiFi), dlatego blokujące
 *          operacje sieciowe (DNS, TCP, CONNECT) nie wpływają na akwizycję na rdzeniu 1.
 *          Broker zapasowy, wykładnicze opóźnienie ponowień i LWT ("offline", retained).
 *
 * Tematy: <base>/telemetry (co 5 s), <base>/event, <base>/status (retained), <base>/cmd.
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
    char t[12], rh[12], mpuT[12];
    const int n = snprintf(
        buf_, sizeof(buf_),
        "{\"seq\":%lu,\"ts\":%lu,\"run\":%d,\"t\":%s,\"rh\":%s,\"dht_ok\":%d,"
        "\"a_rms\":%.4f,\"a_peak\":%.4f,\"cf\":%.2f,\"v_rms\":%.3f,"
        "\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,\"mpu_t\":%s,\"mpu_ok\":%d,"
        "\"run_h\":%.4f,\"svc_h\":%.4f,\"starts\":%lu,\"faults\":%u,"
        "\"ovr\":%lu,\"rssi\":%d,\"heap\":%lu}",
        (unsigned long)d.seq, (unsigned long)d.timestampMs, s.running ? 1 : 0,
        jsonNum(t, sizeof(t), d.climate.valid ? d.climate.tempC : NAN, 1),
        jsonNum(rh, sizeof(rh), d.climate.valid ? d.climate.humidityPct : NAN, 1),
        (s.faults & kFaultDht) ? 0 : 1, d.vib.aRmsG, d.vib.aPeakG, d.vib.crest, d.vib.vRmsMmS, d.accelG[0],
        d.accelG[1], d.accelG[2], jsonNum(mpuT, sizeof(mpuT), d.mpuTempC, 1), d.mpuOk ? 1 : 0,
        s.runTotalMs / 3600000.0, s.runSinceServiceMs / 3600000.0, (unsigned long)s.starts, (unsigned)s.faults,
        (unsigned long)d.overruns, (int)WiFi.RSSI(), (unsigned long)ESP.getFreeHeap());
    if (n > 0 && n < static_cast<int>(sizeof(buf_)) && mqtt_.publish(topicTelemetry_, buf_)) {
      sent_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void publishEvent(const Event& e) {
    if (!mqtt_.connected()) return;  // dziennik lokalny prowadzi sterownik (Etap 3: bufor zdarzeń)
    snprintf(buf_, sizeof(buf_), "{\"seq\":%lu,\"ts\":%lu,\"type\":\"%s\",\"value\":%.3f}", (unsigned long)e.seq,
             (unsigned long)e.timestampMs, toString(e.type), e.value);
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
    } else {
      logMsg("[MQTT] Nieznane polecenie (obsługiwane: ping, status)");
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
// 9. AKWIZYCJA (zadanie na rdzeniu 1, priorytet 3)
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
          publish = vib_.addSample(sample.accG, rtos::machineRunning.load(std::memory_order_relaxed));
        } else if (++mpuErrors >= cfg::kMpuFaultStreak) {
          snap_.mpuOk = false;  // usterka – kolejne próby co kMpuRetryMs
          lastRetryMs = nowMs;
          publish = true;
        }
      } else if (nowMs - lastRetryMs >= cfg::kMpuRetryMs) {
        lastRetryMs = nowMs;
        mpuErrors = 0;
        snap_.mpuOk = mpu_.begin();
        publish = snap_.mpuOk;
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
// 10. STEROWNIK (Arduino loop(), rdzeń 1, priorytet 1)
// ============================================================================

/**
 * @brief Logika aplikacji: wejścia operatora, licznik czasu pracy, wykrywanie usterek
 *        czujników, zdarzenia, sygnalizacja, wyświetlacz i dziennik.
 *
 * Wartość pola Event::value: RUN_START – numer uruchomienia, RUN_STOP – czas cyklu [s],
 * SERVICE_DONE – czas pracy od poprzedniego serwisu [h], SENSOR_* – 1 = MPU6050, 2 = DHT22.
 */
class MachineController {
 public:
  MachineController()
      : btnAck_(cfg::kPinBtnAck, "ACK"),
        btnService_(cfg::kPinBtnService, "SERWIS"),
        runInput_(cfg::kPinRun, "RUN"),
        display_(Wire1) {}

  /** @brief Inicjalizacja w kontekście zadania loop() (musi poprzedzać start akwizycji). */
  void begin(const MqttLink* link) {
    link_ = link;
    rtos::controllerTask = xTaskGetCurrentTaskHandle();
    DebouncedInput::setNotifyTask(rtos::controllerTask);
    btnAck_.begin();
    btnService_.begin();
    runInput_.begin();
    hours_.begin(runInput_.active(), esp_timer_get_time());
    rtos::machineRunning.store(runInput_.active());
    sys_.running = runInput_.active();

    indicators_.begin(millis());
    Wire1.begin(cfg::kPinI2c1Sda, cfg::kPinI2c1Scl, cfg::kI2cClockHz);
    Wire1.setTimeOut(cfg::kI2cTimeoutMs);
    logMsg("[HMI] OLED: %s", display_.begin() ? "OK (I2C1)" : "BRAK ODPOWIEDZI");
    emit(EventType::Boot, 0.0f);
    if (sys_.running) logMsg("[RUN] Maszyna pracuje w chwili startu sterownika");
  }

  /** @brief Jedna iteracja: czekaj na zdarzenie (ISR/dane) maks. 10 ms, potem obsłuż. */
  void tick() {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(cfg::kControllerTickMs));
    const uint32_t nowMs = millis();
    const uint32_t nowUs = micros();
    const uint64_t now64 = esp_timer_get_time();

    handleInputs(nowUs, nowMs, now64);
    handleSensors(now64);
    refreshIndicators();
    indicators_.update(nowMs);

    if (nowMs - lastDisplayMs_ >= cfg::kDisplayPeriodMs) {
      lastDisplayMs_ = nowMs;
      refreshSystem(now64);
      display_.render(sys_, link_->wifiUp(), link_->mqttUp());
    }
    if (nowMs - lastStatusMs_ >= cfg::kStatusPeriodMs) {
      lastStatusMs_ = nowMs;
      printStatus();
    }
    if (nowMs - lastDiagMs_ >= cfg::kDiagPeriodMs) {
      lastDiagMs_ = nowMs;
      printDiagnostics();
    }
  }

 private:
  void handleInputs(uint32_t nowUs, uint32_t nowMs, uint64_t now64) {
    if (btnAck_.update(nowUs, nowMs) && btnAck_.active()) {
      emit(EventType::Ack, 0.0f);
      indicators_.beep(2500, 80, 1);
    }

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
      const uint64_t beforeMs = hours_.resetService(now64);
      emit(EventType::ServiceDone, static_cast<float>(beforeMs / 3600000.0));
      indicators_.beep(2000, 120, 2);
    }

    if (runInput_.update(nowUs, nowMs)) {
      const bool running = runInput_.active();
      rtos::machineRunning.store(running, std::memory_order_relaxed);
      const uint64_t cycleMs = hours_.setRunning(running, now64);
      sys_.running = running;
      if (running) {
        emit(EventType::RunStart, static_cast<float>(hours_.starts()));
      } else {
        emit(EventType::RunStop, static_cast<float>(cycleMs / 1000.0));
      }
    }
  }

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
    refreshSystem(now64);
  }

  /** @brief Aktualizuje liczniki czasu pracy i publikuje stan do skrzynki komunikacji. */
  void refreshSystem(uint64_t now64) {
    sys_.runTotalMs = hours_.totalMs(now64);
    sys_.runSinceServiceMs = hours_.sinceServiceMs(now64);
    sys_.starts = hours_.starts();
    xQueueOverwrite(rtos::systemMailbox, &sys_);
  }

  /** @brief Semantyka lampek w Etapie 2 (Etap 3 doda alarmy i ostrzeżenia predykcyjne). */
  void refreshIndicators() {
    indicators_.set(Indicators::kGreen, sys_.running ? Pattern::On : Pattern::Heartbeat);
    indicators_.set(Indicators::kYellow, Pattern::Off);
    indicators_.set(Indicators::kRed, sys_.faults ? Pattern::FastBlink : Pattern::Off);
  }

  void emit(EventType type, float value) {
    const Event event{++eventSeq_, static_cast<uint32_t>(millis()), type, value};
    logMsg("[ZDARZENIE #%lu] %s (wartość %.3f)", (unsigned long)event.seq, toString(type), value);
    if (xQueueSend(rtos::eventQueue, &event, 0) != pdTRUE) ++eventsDropped_;
  }

  void printStatus() {
    const SensorSnapshot& d = sys_.sensors;
    const VibrationFeatures& v = d.vib;
    char t[12], rh[12], total[16], svc[16];
    formatHms(sys_.runTotalMs, total, sizeof(total));
    formatHms(sys_.runSinceServiceMs, svc, sizeof(svc));
    logMsg("%s | T=%s°C RH=%s%% | a_rms=%.3fg pk=%.3fg CF=%.2f v_rms=%.2fmm/s | praca %s (od serwisu %s) starty=%lu | "
         "MPU %s DHT %s | MQTT %s",
         sys_.running ? "PRACA " : "POSTÓJ", formatValue(t, sizeof(t), d.climate.valid ? d.climate.tempC : NAN, 1),
         formatValue(rh, sizeof(rh), d.climate.valid ? d.climate.humidityPct : NAN, 0), v.aRmsG, v.aPeakG, v.crest,
         v.vRmsMmS, total, svc, (unsigned long)sys_.starts, (sys_.faults & kFaultMpu) ? "ERR" : "OK",
         (sys_.faults & kFaultDht) ? "ERR" : "OK", link_->mqttUp() ? "online" : "offline");
  }

  void printDiagnostics() {
    logMsg("[DIAG] sterta %lu B (min %lu B) | wolny stos: acq %u B, comms %u B, loop %u B | próbki %lu, "
         "pominięte takty %lu | zbocza ACK=%lu SERWIS=%lu RUN=%lu | MQTT wysłane %lu, zdarzenia utracone %lu",
         (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
         (unsigned)uxTaskGetStackHighWaterMark(rtos::acqTask), (unsigned)uxTaskGetStackHighWaterMark(rtos::commsTask),
         (unsigned)uxTaskGetStackHighWaterMark(nullptr), (unsigned long)sys_.sensors.samples,
         (unsigned long)sys_.sensors.overruns, (unsigned long)btnAck_.edges(), (unsigned long)btnService_.edges(),
         (unsigned long)runInput_.edges(), (unsigned long)link_->sent(), (unsigned long)eventsDropped_);
  }

  DebouncedInput btnAck_;
  DebouncedInput btnService_;
  DebouncedInput runInput_;
  RunHourMeter hours_;
  Indicators indicators_;
  StatusDisplay display_;
  const MqttLink* link_ = nullptr;
  SystemSnapshot sys_;
  uint32_t lastSensorSeq_ = 0;
  uint32_t lastDisplayMs_ = 0;
  uint32_t lastStatusMs_ = 0;
  uint32_t lastDiagMs_ = 0;
  uint32_t eventSeq_ = 0;
  uint32_t eventsDropped_ = 0;
  bool serviceArmed_ = false;
};

// ============================================================================
// 11. OBIEKTY GLOBALNE (alokacja statyczna) I PUNKTY WEJŚCIA ARDUINO
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
  logMsg("==================================================");

  rtos::sensorMailbox = xQueueCreate(1, sizeof(SensorSnapshot));
  rtos::systemMailbox = xQueueCreate(1, sizeof(SystemSnapshot));
  rtos::eventQueue = xQueueCreate(cfg::kEventQueueLen, sizeof(Event));
  if (!rtos::sensorMailbox || !rtos::systemMailbox || !rtos::eventQueue) {
    logMsg("[BŁĄD] Brak pamięci na kolejki FreeRTOS");
  }

  g_controller.begin(&g_link);  // wejścia + HMI (uchwyt zadania loop() dla powiadomień)
  if (!g_acquisition.begin()) logMsg("[BŁĄD] Nie udało się uruchomić akwizycji");
  if (!g_link.begin()) logMsg("[BŁĄD] Nie udało się uruchomić zadania komunikacji");
  enableLoopWDT();  // watchdog nadzoruje także pętlę sterownika
  logMsg("[SYSTEM] Gotowy: próbkowanie %lu Hz, okno drgań %u próbek", (unsigned long)cfg::kSampleRateHz,
       (unsigned)cfg::kWindowSamples);
}

void loop() { g_controller.tick(); }
