#include "FileManager.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>

namespace {
WebServer server(80);
File uploadFile;
bool uploadFailed = false;
String uploadError;
String telemetryJson = "{}";

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
            "<p>Only one file can be uploaded at a time. <a href='/'>Home</a></p>"
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
  server.on("/", HTTP_GET, handleRoot);
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
