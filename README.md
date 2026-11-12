# ESP32 DALY BMS Monitor 🔋

Повнофункціональна система моніторингу DALY A5 BMS (Battery Management System) на базі ESP32 з підтримкою Wi-Fi, Bluetooth Low Energy та хмарних сервісів.

<div align="center">

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Language](https://img.shields.io/badge/language-C%2B%2B-brightgreen.svg)]()
[![Platform](https://img.shields.io/badge/platform-ESP32-orange.svg)]()

[🇺🇦 Українська](#українська) | [📖 Документація](./doc) | [🚀 Швидкий старт](#швидкий-старт)

</div>

---

## 📋 Українська

### Про проект

**esp32-daly-bms-monitor** — це модульна система для моніторингу акумуляторних батарей DALY A5 через:

- 🔌 **Bluetooth Low Energy (BLE)** — прямий зв'язок з BMS
- 📡 **Wi-Fi** — передача даних по мережі
- ☁️ **Cloud Integration** — відправка метрики на сервер
- 📊 **Web Dashboard** — веб-інтерфейс для перегляду даних
- 📱 **JSON API** — простий API для інтеграцій

### Основні можливості

✅ **Моніторинг BMS:**
- Напруга акумулятора (V)
- Струм заряду/розряду (A)
- Стан заряду (SOC %)
- Температура (T1, T2)
- Напруга кожної комірки (до 48)
- Кількість циклів заряджання
- Статус алармів

✅ **Конфігурація пристроїв:**
- Auto-scan режим (автоматичне виявлення)
- Ручна конфігурація з іменами та ID
- Підтримка до 8 BMS одночасно

✅ **Мережа:**
- Wi-Fi та MQTT
- Хмарні сервіси (Blynk, Home Assistant, тощо)
- JSON API для інтеграцій

✅ **Користувацькі інтерфейси:**
- Вбудований веб-сервер з дашбордом
- UART/Serial моніторинг
- REST API для мобільних додатків

### 🚀 Швидкий старт

#### Вимоги

- **ESP32** (dev board або модуль)
- **Arduino IDE** 1.8.0+ або PlatformIO
- **Бібліотеки:**
  - [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)
  - [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
  - Arduino WiFi
  - Arduino HTTP Server

#### Встановлення

1. **Клонуйте репозиторій:**
```bash
git clone https://github.com/lagidnyj/esp32-daly-bms-monitor.git
cd esp32-daly-bms-monitor
```

2. **Відкрийте в Arduino IDE:**
```bash
# Відкрийте файл:
example2/example2.ino
```

3. **Налаштуйте конфігурацію в `example2/BmsConfig.h`:**

**Варіант A - Auto-scan (рекомендується):**
```cpp
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {nullptr, nullptr, nullptr}
};
```

**Варіант B - Ручна конфігурація:**
```cpp
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {"02:11:22:33:44:55", "Батарея-Дім", "Main"},
  {"02:AA:BB:CC:DD:EE", "Батарея-Гараж", "Garage"},
  {nullptr, nullptr, nullptr}
};
```

4. **Завантажте на ESP32:**
- Вибрати плату: `Tools → Board → ESP32 Dev Module`
- Вибрати порт: `Tools → Port → COM*`
- Натиснути `Upload` (Ctrl+U)

5. **Перевірте в Serial Monitor:**
```
[scan] 8000 ms...
02:aa:bb:cc:dd:ee  rssi -45  type 0  DALY-BMS [FFF0]
[scan] 1 devices
[cfg] 1 BMS target(s)
```

### 📁 Структура проекту

```
esp32-daly-bms-monitor/
├── README.md                    ← Цей файл
├── doc/                         ← 📚 Документація
│   ├── README.md               ← Англійська документація
│   ├── QUICK_START.md          ← Швидкий старт
│   ├── BMS_CONFIG_GUIDE.md     ← Посібник конфігурації
│   ├── ARCHITECTURE.md         ← Архітектура системи
│   ├── LOGIC_REVIEW.md         ← Перевірка логіки
│   └── ... ще документи
│
├── firmware/
│   └── daly-profile/           ← Основна прошивка (read-only профіль)
│       ├── daly-profile.ino
│       ├── DalyParser.h/cpp
│       ├── BmsTypes.h
│       ├── BmsConfig.h
│       └── ... ще файли
│
├── example2/                    ← ⭐ Основна реалізація (РЕКОМЕНДУЄТЬСЯ)
│   ├── example2.ino            ← Основна точка входу
│   ├── BmsConfig.h             ← Конфігурація ✨ НОВО
│   ├── BmsTypes.h              ← Структури даних ✨ НОВО
│   ├── BmsOutput.cpp           ← JSON генерація ✨ НОВО
│   ├── BmsBle.cpp              ← BLE логіка ✨ НОВО
│   ├── DalyParser.h/cpp        ← Парсер DALY протоколу
│   ├── CloudReporter.h/cpp     ← Відправка в хмару
│   ├── NetworkService.h/cpp    ← Wi-Fi/мережа
│   ├── FileManager.h/cpp       ← Робота з файлами
│   ├── BmsOutput.h             ← JSON вивід
│   ├── WebAssets.h             ← Веб-ресурси
│   ├── index.html              ← Веб-дашборд
│   ├── bms.css                 ← Стилі дашборду
│   ├── config.json.example     ← Приклад конфіки
│   └── README.md               ← Документація example2
│
├── tools/
│   └── daly_ble_probe/         ← BLE сканер/тестер
│       └── daly_ble_probe.ino
│
└── ... налаштування
```

### ⭐ Що нового (BMS Configuration Update)

Нещодавно додана гнучка система конфігурації пристроїв з підтримкою:

✨ **Auto-generated Device IDs** — послідні 4 символи MAC адреси  
✨ **Custom Device Names** — людиночитаємі імена для дашборду  
✨ **Optional Configuration** — auto-scan або ручна конфігурація  
✨ **Clean JSON Output** — без пустих полів, тільки потрібні дані

**Три правила конфігурації:**
1. Auto-scan: ID автоматично з MAC, ім'я пусто
2. Config mode: ID та ім'я з конфіга (опціонально)
3. Optional fields: поля опускаються якщо пусті

Детальніше: [doc/BMS_CONFIG_GUIDE.md](./doc/BMS_CONFIG_GUIDE.md)

### 🔧 Конфігурація

#### Wi-Fi
```cpp
// В example2/BmsConfig.h
static const char* const WIFI_SSID = "YOUR_WIFI_SSID";
static const char* const WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

#### OTA Update 🔐

Для безпечного оновлення прошивки по мережі використовується HTTP Basic Auth:

| Параметр | Значення |
|----------|----------|
| **Username** | `vendor` |
| **Password** | Встановлюється в BmsConfig.h |

**Налаштування пароля:**

```cpp
// В example2/BmsConfig.h
static const char* const OTA_PASSWORD = "CHANGE_ME_OTA_PASSWORD";
```

⚠️ **Правила безпеки:**
- Пароль повинен бути **мінімум 16 символів**
- За замовчуванням OTA вимкнено (placeholder)
- Змініть пароль перед розгортанням на продакшн
- Приклад: `"MySecureOTA2024"`

**Увімкнення OTA:**
1. Открийте `example2/BmsConfig.h`
2. Замініть `"CHANGE_ME_OTA_PASSWORD"` на ваш пароль (16+ символів)
3. Перезавантажте на ESP32
4. OTA буде доступна за адресою `http://<ESP32_IP>:8080`

#### BLE Параметри
```cpp
#define SCAN_MS           8000      // Тривалість сканування (мс)
#define RESCAN_INTERVAL_MS (15UL * 60UL * 1000UL) // Інтервал пересканування
#define CONNECT_MS        10000     // Таймаут підключення
#define POLL_INTERVAL_MS  10000     // Інтервал опитування
#define MAX_BMS           8         // Макс кількість BMS
```

#### Хмара
```cpp
// Налаштування для Blynk, Home Assistant, MQTT тощо
// Див. example2/example2.ino
```

### 📊 JSON API

#### Формат вивідних даних

**Auto-scan режим:**
```json
{
  "02:aa:bb:cc:dd:ee": {
    "id": "ddee",
    "connected": true,
    "battery_voltage": 48.5,
    "charge_current": 0.2,
    "percent_remain": 65.4,
    ...
  }
}
```

**Config режим з іменем:**
```json
{
  "02:11:22:33:44:55": {
    "id": "Main",
    "name": "Батарея-Дім",
    "connected": true,
    "battery_voltage": 48.5,
    ...
  }
}
```

### 🐛 Розробка

#### Компіляція
```bash
# Arduino IDE
Tools → Upload

# PlatformIO
pio run -t upload
```

#### Відлагодження
```
Serial Monitor: 9600 baud
Log messages: [tag] message format
```

#### Модульне тестування
```bash
# Тест ID генерації (дивись doc/LOGIC_REVIEW.md)
./test_id_extraction.sh
```

### 📚 Документація

Повна документація на **англійській мові** доступна в папці [`doc/`](./doc/):

- **[QUICK_START.md](./doc/QUICK_START.md)** — 5-хвилинний старт
- **[BMS_CONFIG_GUIDE.md](./doc/BMS_CONFIG_GUIDE.md)** — Посібник конфігурації
- **[ARCHITECTURE.md](./doc/ARCHITECTURE.md)** — Архітектура системи
- **[IMPLEMENTATION_SUMMARY.md](./doc/IMPLEMENTATION_SUMMARY.md)** — Технічні деталі
- **[LOGIC_REVIEW.md](./doc/LOGIC_REVIEW.md)** — Перевірка логіки
- **[INDEX.md](./doc/INDEX.md)** — Повний індекс документації

### 🔌 Підтримуване обладнання

**ESP32:**
- ESP32 Dev Kit
- ESP32-WROOM
- ESP32-S3
- Інші варіанти (тестування потрібне)

**BMS:**
- DALY A5 (основний фокус)
- Інші DALY моделі (можливо, потребують адаптації)

### ⚙️ Залежності

```cpp
#include <NimBLEDevice.h>    // Bluetooth Low Energy
#include <WiFi.h>            // Wi-Fi
#include <ArduinoJson.h>     // JSON парсинг
#include <WebServer.h>       // Вбудований веб-сервер
// Див. example2/example2.ino
```

### 🔐 Безпека

⚠️ **Важливо:**
- Змініть пароль OTA перед розгортанням!
- Не публікуйте Wi-Fi пароль в коді
- Використовуйте HTTPS для хмарних сервісів

### 📱 Інтеграції

Проект легко інтегрується з:

- 📊 **Home Assistant** — через MQTT або HTTP
- 🎮 **Blynk** — IoT платформа
- 📉 **InfluxDB** — база даних часових рядів
- 🔔 **Telegram** — сповіщення
- 📱 **Спеціальні мобільні додатки** — через JSON API

### 🤝 Контрибюції

Ви можете допомогти проекту:

1. 🐛 **Знайти баги** — відкрити issue
2. 💡 **Пропозиції** — обговорити в discussions
3. 💻 **Код** — зробити fork, змінити, pull request
4. 📖 **Документація** — поліпшити посібники
5. 🌍 **Переклад** — локалізація на інші мови

### 📝 Ліцензія

MIT License — див. [LICENSE](LICENSE)

### 📞 Підтримка

- 📖 Документація: [doc/](./doc/)
- 🐛 Баги: [Issues](../../issues)
- 💬 Обговорення: [Discussions](../../discussions)
- ✉️ Email: [контактна інформація]

### 🙏 Подяки

Спасибі за використання проекту!

---

## English

### About

**esp32-daly-bms-monitor** is a comprehensive monitoring system for DALY A5 BMS (Battery Management System) based on ESP32.

**Key Features:**
- 🔌 Bluetooth Low Energy (BLE) connectivity
- 📡 Wi-Fi integration
- ☁️ Cloud services support (Blynk, Home Assistant, MQTT)
- 📊 Web dashboard with real-time monitoring
- 📱 JSON API for integrations
- 🔧 Flexible device configuration with names and IDs

### Quick Start

1. **Clone repository:**
```bash
git clone https://github.com/lagidnyj/esp32-daly-bms-monitor.git
```

2. **Open in Arduino IDE:** `example2/example2.ino`

3. **Configure in example2/BmsConfig.h:**
```cpp
static const struct BmsDeviceConfig BMS_DEVICES[] = {
  {nullptr, nullptr, nullptr}  // Auto-scan mode
};
```

4. **Upload to ESP32 and monitor serial output**

### Documentation

Full documentation available in [doc/](./doc/) folder (English).

### License

MIT License

---

**v1.0** | Last updated: 2026-11-12
