#include "OledDisplay.h"

#include "BmsConfig.h"

#if OLED_DISPLAY_ENABLED
#include <U8g2lib.h>
#include <WiFi.h>
#include <Wire.h>

#include <stdio.h>

#include "CloudReporter.h"
#include "NetworkService.h"

namespace {
constexpr uint8_t OLED_SDA_PIN = 5;
constexpr uint8_t OLED_SCL_PIN = 6;
constexpr uint8_t OLED_I2C_ADDRESS = 0x3C;
constexpr uint8_t BOOT_BUTTON_PIN = 9;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
constexpr uint32_t PAGE_INTERVAL_MS = 2000;
constexpr uint8_t PAGE_COUNT = 3;

U8G2_SSD1306_72X40_ER_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
uint8_t currentPage = 0;
uint32_t lastPageAt = 0;
uint32_t buttonChangedAt = 0;
bool lastButtonReading = false;
bool buttonPressed = false;
bool displayOn = true;
bool initialized = false;

void drawCentered(const char* text, uint8_t baseline) {
  const int16_t width = display.getStrWidth(text);
  const int16_t x = width < 72 ? (72 - width) / 2 : 0;
  display.drawStr(x, baseline, text);
}

void drawIpAddress(const String& ip) {
  display.setFont(u8g2_font_5x7_tf);
  drawCentered(ip.c_str(), 29);
}

void showPage(size_t bmsTargetCount) {
  display.clearBuffer();
  display.setFont(u8g2_font_7x13_tf);

  if (currentPage == 0) {
    if (NetworkService::isProvisioning()) {
      display.setFont(u8g2_font_4x6_tf);
      drawCentered("WIFI SETUP", 6);
      drawCentered(NetworkService::provisioningSsid().c_str(), 16);
      drawCentered(NetworkService::provisioningPassword().c_str(), 26);
      const String ip = WiFi.softAPIP().toString();
      drawCentered(ip.c_str(), 36);
    } else if (WiFi.status() == WL_CONNECTED) {
      drawCentered("WIFI IP", 14);
      const String ip = WiFi.localIP().toString();
      drawIpAddress(ip);
    } else if (NetworkService::isConnecting()) {
      drawCentered("WIFI", 21);
      drawCentered("CONNECTING", 33);
    } else {
      drawCentered("WIFI FAULT", 22);
    }
  } else if (currentPage == 1) {
    char countText[16];
    snprintf(countText, sizeof(countText), "BMS %u",
             static_cast<unsigned>(bmsTargetCount));
    drawCentered("TARGETS", 14);
    drawCentered(countText, 29);
  } else {
    drawCentered("CLOUD", 9);
    switch (CloudReporter::status()) {
      case CloudReporter::Status::NotConfigured:
        drawCentered("NOT", 21);
        drawCentered("CONFIGURED", 33);
        break;
      case CloudReporter::Status::Waiting:
        drawCentered("WAITING", 29);
        break;
      case CloudReporter::Status::Ok:
        drawCentered("200 OK", 29);
        break;
      case CloudReporter::Status::Error:
        drawCentered("SEND", 21);
        drawCentered("ERROR", 33);
        break;
    }
  }

  display.sendBuffer();
}

void pollButton(size_t bmsTargetCount, uint32_t now) {
  const bool reading = digitalRead(BOOT_BUTTON_PIN) == LOW;
  if (reading != lastButtonReading) {
    lastButtonReading = reading;
    buttonChangedAt = now;
  }

  if (now - buttonChangedAt < BUTTON_DEBOUNCE_MS ||
      reading == buttonPressed) {
    return;
  }

  buttonPressed = reading;
  if (!buttonPressed) return;

  displayOn = !displayOn;
  display.setPowerSave(displayOn ? 0 : 1);
  if (displayOn) {
    lastPageAt = now;
    showPage(bmsTargetCount);
  }
}
}
#endif

namespace OledDisplay {
void begin() {
#if OLED_DISPLAY_ENABLED
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  lastButtonReading = digitalRead(BOOT_BUTTON_PIN) == LOW;
  buttonPressed = lastButtonReading;
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  display.setI2CAddress(OLED_I2C_ADDRESS << 1);
  if (!display.begin()) {
    DEBUG_SERIAL.println("OLED initialization failed.");
    return;
  }
  initialized = true;
  lastPageAt = millis();
  showPage(0);
#endif
}

void loop(size_t bmsTargetCount) {
#if OLED_DISPLAY_ENABLED
  if (!initialized) return;

  const uint32_t now = millis();
  pollButton(bmsTargetCount, now);
  if (NetworkService::isProvisioning()) {
    if (currentPage != 0 || now - lastPageAt >= PAGE_INTERVAL_MS) {
      currentPage = 0;
      lastPageAt = now;
      if (displayOn) showPage(bmsTargetCount);
    }
    return;
  }
  if (now - lastPageAt < PAGE_INTERVAL_MS) return;

  const uint32_t elapsedPages = (now - lastPageAt) / PAGE_INTERVAL_MS;
  currentPage = (currentPage + elapsedPages) % PAGE_COUNT;
  lastPageAt += elapsedPages * PAGE_INTERVAL_MS;
  if (displayOn) showPage(bmsTargetCount);
#else
  (void)bmsTargetCount;
#endif
}
}
