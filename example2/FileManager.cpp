#include "FileManager.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <string.h>

#include "BmsConfig.h"
#include "NetworkService.h"
#include "WebAssets.h"

namespace {
WebServer server(80);
File uploadFile;
bool uploadFailed = false;
String uploadError;
String telemetryJson = "{}";
bool otaUploadStarted = false;
bool otaUploadFailed = false;
bool otaUploadFinished = false;
String otaUploadError;

constexpr char OTA_USERNAME[] = "vendor";
constexpr char OTA_PASSWORD_PLACEHOLDER[] = "CHANGE_ME_OTA_PASSWORD";

bool isOtaPasswordConfigured() {
  return OTA_PASSWORD != nullptr && strlen(OTA_PASSWORD) >= 16 &&
         strcmp(OTA_PASSWORD, OTA_PASSWORD_PLACEHOLDER) != 0;
}

bool requireOtaAuthentication() {
  if (!isOtaPasswordConfigured()) {
    server.send(503, "text/plain; charset=utf-8",
                "OTA is disabled. Set OTA_PASSWORD in BmsConfig.h to a "
                "non-placeholder value of at least 16 characters.");
    return false;
  }
  if (!server.authenticate(OTA_USERNAME, OTA_PASSWORD)) {
    server.requestAuthentication(BASIC_AUTH, "ESP32 OTA");
    return false;
  }
  return true;
}

bool installDefaultFileIfMissing(const char* path, const char* content,
                                 size_t size) {
  if (LittleFS.exists(path)) return true;

  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.printf("Could not create default web file %s.\n", path);
    return false;
  }

  const size_t written = file.write(
      reinterpret_cast<const uint8_t*>(content), size);
  file.close();
  if (written != size) {
    LittleFS.remove(path);
    Serial.printf("Could not fully write default web file %s.\n", path);
    return false;
  }

  Serial.printf("Installed default web file %s (%u bytes).\n", path,
                static_cast<unsigned>(size));
  return true;
}

String htmlEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); i++) {
    switch (value[i]) {
      case '&': escaped += F("&amp;"); break;
      case '<': escaped += F("&lt;"); break;
      case '>': escaped += F("&gt;"); break;
      case '"': escaped += F("&quot;"); break;
      case '\'': escaped += F("&#39;"); break;
      default: escaped += value[i]; break;
    }
  }
  return escaped;
}

String urlEncode(const String& value) {
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  String encoded;
  for (size_t i = 0; i < value.length(); i++) {
    const uint8_t ch = static_cast<uint8_t>(value[i]);
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' ||
        ch == '~' || ch == '/') {
      encoded += static_cast<char>(ch);
    } else {
      encoded += '%';
      encoded += HEX_DIGITS[ch >> 4];
      encoded += HEX_DIGITS[ch & 0x0F];
    }
  }
  return encoded;
}

bool getSafeFilePath(String& path) {
  path = server.arg("file");
  path.trim();
  if (path.length() == 0 || path == "/" || path.indexOf("..") >= 0 ||
      path.indexOf('\\') >= 0) {
    return false;
  }
  if (!path.startsWith("/")) path = "/" + path;
  return path.indexOf('/') == 0 && path.length() <= 128;
}

bool isSafeUploadName(const String& filename) {
  if (filename.length() == 0 || filename.length() > 64 || filename == "." ||
      filename == ".." || filename[0] == '.') {
    return false;
  }
  for (size_t i = 0; i < filename.length(); i++) {
    const char ch = filename[i];
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-')) {
      return false;
    }
  }
  return true;
}

String formatSize(size_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  if (bytes < 1024 * 1024) return String(bytes / 1024.0f, 1) + " KB";
  return String(bytes / (1024.0f * 1024.0f), 1) + " MB";
}

void redirectToFileList() {
  server.sendHeader("Location", "/fs");
  server.send(303, "text/plain", "");
}

void handleFileList() {
  String rows;
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) {
    server.send(500, "text/plain", "Unable to open LittleFS root.");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String path = file.name();
      if (!path.startsWith("/")) path = "/" + path;
      const String encodedPath = urlEncode(path);
      const String escapedPath = htmlEscape(path);
      rows += F("<tr><td>");
      rows += escapedPath;
      rows += F("</td><td>");
      rows += formatSize(file.size());
      rows += F("</td><td><a href='/fs/view?file=");
      rows += encodedPath;
      rows += F("'>View</a> | <a href='/fs/download?file=");
      rows += encodedPath;
      rows += F("'>Download</a> | <form class='inline' method='post' action='/fs/delete'>");
      rows += F("<input type='hidden' name='file' value='");
      rows += escapedPath;
      rows += F("'><button onclick=\"return confirm('Delete this file?')\">Delete</button></form></td></tr>");
    }
    file = root.openNextFile();
  }

  String page = F(
      "<!doctype html><html><head><meta name='viewport' content='width=device-width,"
      "initial-scale=1'><title>LittleFS files</title><style>"
      "body{font:16px sans-serif;max-width:900px;margin:2em auto;padding:0 1em}"
      "table{width:100%;border-collapse:collapse}td,th{padding:.6em;border-bottom:1px solid #ccc;text-align:left}"
      ".inline{display:inline}button,input{padding:.4em}pre{white-space:pre-wrap;overflow-wrap:anywhere}"
      "</style></head><body><h1>LittleFS files</h1><table><tr><th>File</th><th>Size</th><th>Actions</th></tr>");
  if (rows.length()) {
    page += rows;
  } else {
    page += F("<tr><td colspan='3'>No files found.</td></tr>");
  }
  page += F("</table><h2>Upload files</h2><form method='post' action='/fs/upload' "
            "enctype='multipart/form-data'><input type='file' name='file' required>"
            "<button type='submit'>Upload</button></form>"
            "<p>Only one file can be uploaded at a time. <a href='/'>Home</a> | "
            "<a href='/wifi'>Wi-Fi setup</a> | <a href='/update'>Firmware update</a></p>"
            "</body></html>");
  server.send(200, "text/html; charset=utf-8", page);
}

void handleRoot() {
  if (LittleFS.exists("/index.html")) {
    File file = LittleFS.open("/index.html", "r");
    if (file) {
      server.streamFile(file, "text/html; charset=utf-8");
      file.close();
      return;
    }
    server.send(500, "text/plain", "Could not open /index.html.");
    return;
  }
  handleFileList();
}

void handleWifiSetup() {
  String page = F(
      "<!doctype html><html><head><meta name='viewport' content='width=device-width,"
      "initial-scale=1'><title>Wi-Fi setup</title><style>"
      "body{font:16px sans-serif;max-width:560px;margin:2em auto;padding:0 1em}"
      "label,input,button{display:block;width:100%;box-sizing:border-box;margin:.6em 0}"
      "input,button{padding:.7em}</style></head><body><h1>Wi-Fi setup</h1>");
  if (NetworkService::isProvisioning()) {
    page += F("<p>Setup access point is active. Open this page at "
              "<code>http://192.168.4.1/wifi</code>.</p>"
              "<p>The device restarts if setup is not completed within five minutes.</p>");
  }
  page += F(
      "<form method='post' action='/wifi/save'>"
      "<label for='ssid'>Wi-Fi name (SSID)</label>"
      "<input id='ssid' name='ssid' maxlength='32' required autocomplete='off'>"
      "<label for='password'>Wi-Fi password</label>"
      "<input id='password' name='password' type='password' maxlength='63' "
      "autocomplete='new-password'>"
      "<p>Leave the password empty only for an open Wi-Fi network. Saving "
      "restarts the device.</p><button type='submit'>Save and restart</button>"
      "</form><p><a href='/'>Home</a> | <a href='/fs'>Files</a></p>"
      "</body></html>");
  server.send(200, "text/html; charset=utf-8", page);
}

void handleWifiSetupSave() {
  String error;
  if (!NetworkService::saveWifiCredentials(server.arg("ssid"),
                                            server.arg("password"), error)) {
    server.send(400, "text/plain; charset=utf-8", error);
    return;
  }
  server.send(200, "text/html; charset=utf-8",
              "<!doctype html><html><body><p>Wi-Fi credentials saved. "
              "The device is restarting.</p></body></html>");
}

void handleOtaPage() {
  if (!requireOtaAuthentication()) return;

  const String page = F(
      "<!doctype html><html><head><meta name='viewport' content='width=device-width,"
      "initial-scale=1'><title>Firmware update</title><style>"
      "body{font:16px sans-serif;max-width:560px;margin:2em auto;padding:0 1em}"
      "input,button{display:block;width:100%;box-sizing:border-box;margin:.8em 0;padding:.7em}"
      "</style></head><body><h1>Firmware update</h1>"
      "<p>Choose an ESP32 firmware .bin file. Do not disconnect power during update.</p>"
      "<form method='post' action='/update' enctype='multipart/form-data'>"
      "<input type='file' name='firmware' accept='.bin' required>"
      "<button type='submit'>Upload and install</button></form>"
      "<p><a href='/'>Home</a> | <a href='/fs'>Files</a></p>"
      "</body></html>");
  server.send(200, "text/html; charset=utf-8", page);
}

void handleOtaUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaUploadStarted = false;
    otaUploadFailed = false;
    otaUploadFinished = false;
    otaUploadError = "";
    if (!isOtaPasswordConfigured()) {
      otaUploadFailed = true;
      otaUploadError = "OTA password is not configured.";
      return;
    }
    if (!server.authenticate(OTA_USERNAME, OTA_PASSWORD)) {
      otaUploadFailed = true;
      otaUploadError = "OTA authentication failed.";
      return;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      otaUploadFailed = true;
      otaUploadError = "Could not start firmware update.";
      Update.printError(Serial);
      return;
    }
    otaUploadStarted = true;
    Serial.printf("OTA upload started: %s\n", upload.filename.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaUploadStarted && !otaUploadFailed) {
      const size_t written = Update.write(upload.buf, upload.currentSize);
      if (written != upload.currentSize) {
        otaUploadFailed = true;
        otaUploadError = "Firmware write failed.";
        Update.printError(Serial);
        Update.abort();
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (otaUploadStarted && !otaUploadFailed) {
      if (Update.end(true)) {
        otaUploadFinished = true;
        Serial.printf("OTA upload completed: %u bytes.\n",
                      static_cast<unsigned>(upload.totalSize));
      } else {
        otaUploadFailed = true;
        otaUploadError = "Firmware image validation or finalization failed.";
        Update.printError(Serial);
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (otaUploadStarted) Update.abort();
    otaUploadStarted = false;
    otaUploadFailed = true;
    otaUploadError = "Firmware upload was aborted.";
  }
}

void handleOtaUploadComplete() {
  if (!requireOtaAuthentication()) return;
  if (otaUploadFailed || !otaUploadFinished) {
    const String error = otaUploadError.length()
                             ? otaUploadError
                             : "Firmware update did not complete.";
    server.send(400, "text/plain; charset=utf-8", error);
    return;
  }

  server.send(200, "text/plain; charset=utf-8",
              "Firmware updated successfully. Restarting device.");
  delay(1000);
  ESP.restart();
}

void handleTelemetry() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", telemetryJson);
}

String contentTypeForPath(const String& path) {
  if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".js")) return "application/javascript; charset=utf-8";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".gif")) return "image/gif";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".txt")) return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

void handleStaticFile() {
  if (server.method() != HTTP_GET) {
    server.send(404, "text/plain", "Not found.");
    return;
  }

  String path = server.uri();
  if (path.length() == 0 || path == "/" || path.indexOf("..") >= 0 ||
      path.indexOf('\\') >= 0) {
    server.send(400, "text/plain", "Invalid path.");
    return;
  }
  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory()) {
    server.send(404, "text/plain", "File not found.");
    return;
  }
  server.streamFile(file, contentTypeForPath(path));
  file.close();
}

void handleFileView() {
  String path;
  if (!getSafeFilePath(path)) {
    server.send(400, "text/plain", "Invalid file path.");
    return;
  }
  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory()) {
    server.send(404, "text/plain", "File not found.");
    return;
  }
  constexpr size_t MAX_VIEW_SIZE = 8192;
  if (file.size() > MAX_VIEW_SIZE) {
    file.close();
    server.send(413, "text/plain", "File is too large to preview; use Download instead.");
    return;
  }

  String content;
  content.reserve(file.size());
  while (file.available()) content += static_cast<char>(file.read());
  file.close();

  String page = F("<!doctype html><html><head><meta name='viewport' content='width=device-width,"
                  "initial-scale=1'><title>File preview</title></head><body><p><a href='/fs'>"
                  "Back to files</a></p><h1>");
  page += htmlEscape(path);
  page += F("</h1><pre>");
  page += htmlEscape(content);
  page += F("</pre></body></html>");
  server.send(200, "text/html; charset=utf-8", page);
}

void handleFileDownload() {
  String path;
  if (!getSafeFilePath(path)) {
    server.send(400, "text/plain", "Invalid file path.");
    return;
  }
  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory()) {
    server.send(404, "text/plain", "File not found.");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment");
  server.streamFile(file, "application/octet-stream");
  file.close();
}

void handleFileDelete() {
  String path;
  if (!getSafeFilePath(path)) {
    server.send(400, "text/plain", "Invalid file path.");
    return;
  }
  if (!LittleFS.exists(path)) {
    server.send(404, "text/plain", "File not found.");
    return;
  }
  if (!LittleFS.remove(path)) {
    server.send(500, "text/plain", "Failed to delete file.");
    return;
  }
  redirectToFileList();
}

void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadFailed = false;
    uploadError = "";
    if (!isSafeUploadName(upload.filename)) {
      uploadFailed = true;
      uploadError = "Invalid filename. Use letters, digits, dot, underscore, or hyphen.";
      return;
    }
    uploadFile = LittleFS.open("/" + upload.filename, "w");
    if (!uploadFile) {
      uploadFailed = true;
      uploadError = "Could not open destination file.";
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!uploadFailed && uploadFile) {
      const size_t written = uploadFile.write(upload.buf, upload.currentSize);
      if (written != upload.currentSize) {
        uploadFailed = true;
        uploadError = "Filesystem write failed.";
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    uploadFailed = true;
    uploadError = "Upload was aborted.";
  }
}

void handleFileUploadComplete() {
  if (uploadFile) uploadFile.close();
  if (uploadFailed) {
    server.send(400, "text/plain", uploadError);
    return;
  }
  redirectToFileList();
}
}

namespace FileManager {
void begin() {
  installDefaultFileIfMissing("/index.html", WebAssets::BMS_INDEX_HTML,
                              sizeof(WebAssets::BMS_INDEX_HTML) - 1);
  installDefaultFileIfMissing("/bms.css", WebAssets::BMS_STYLE_CSS,
                              sizeof(WebAssets::BMS_STYLE_CSS) - 1);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi", HTTP_GET, handleWifiSetup);
  server.on("/wifi/save", HTTP_POST, handleWifiSetupSave);
  server.on("/update", HTTP_GET, handleOtaPage);
  server.on("/update", HTTP_POST, handleOtaUploadComplete, handleOtaUpload);
  server.on("/data", HTTP_GET, handleTelemetry);
  server.on("/fdata", HTTP_GET, handleTelemetry);
  server.on("/fs", HTTP_GET, handleFileList);
  server.on("/fs/view", HTTP_GET, handleFileView);
  server.on("/fs/download", HTTP_GET, handleFileDownload);
  server.on("/fs/delete", HTTP_POST, handleFileDelete);
  server.on("/fs/upload", HTTP_POST, handleFileUploadComplete, handleFileUpload);
  server.onNotFound(handleStaticFile);
  server.begin();
  Serial.println("LittleFS web files are served from /; telemetry JSON is available at /data and /fdata.");
  Serial.println("LittleFS file manager available at /fs.");
}

void loop() {
  server.handleClient();
}

void setTelemetryJson(const String& json) {
  telemetryJson = json.length() ? json : "{}";
}
}
