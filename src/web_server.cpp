#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FS.h>                       // SPIFFS file-system: store web server html, CSS etc.

#include "emonesp.h"
#include "web_server.h"
#include "config.h"
#include "wifi.h"
#include "mqtt.h"
#include "input.h"
#include "emoncms.h"
//#include "ota.h"
#include "debug.h"

AsyncWebServer server(80);          //Create class for Web server

bool enableCors = true;

// Event timeouts
unsigned long wifiRestartTime = 0;
unsigned long mqttRestartTime = 0;
unsigned long systemRestartTime = 0;
unsigned long systemRebootTime = 0;

// Get running firmware version from build tag environment variable
#define TEXTIFY(A) #A
#define ESCAPEQUOTE(A) TEXTIFY(A)
String currentfirmware = ESCAPEQUOTE(BUILD_TAG);

// -------------------------------------------------------------------
// Helper function to perform the standard operations on a request
// -------------------------------------------------------------------
bool requestPreProcess(AsyncWebServerRequest *request, AsyncResponseStream *&response, const char *contentType = "application/json")
{
  if(www_username!="" && !request->authenticate(www_username.c_str(), www_password.c_str())) {
    request->requestAuthentication();
    return false;
  }

  response = request->beginResponseStream(contentType);
  if(enableCors) {
    response->addHeader("Access-Control-Allow-Origin", "*");
  }

  return true;
}

// -------------------------------------------------------------------
// Load Home page
// url: /
// -------------------------------------------------------------------
void
handleHome(AsyncWebServerRequest *request) {
  if (www_username != ""
      && !request->authenticate(www_username.c_str(),
                              www_password.c_str())
      && wifi_mode == WIFI_MODE_STA) {
    return request->requestAuthentication();
  }

  if (SPIFFS.exists("/home.html")) {
    request->send(SPIFFS, "/home.html");
  } else {
    request->send(200, "text/plain",
                  "/home.html not found, have you flashed the SPIFFS?");
  }
}

// -------------------------------------------------------------------
// Wifi scan /scan not currently used
// url: /scan
//
// First request will return 0 results unless you start scan from somewhere else (loop/setup)
// Do not request more often than 3-5 seconds
// -------------------------------------------------------------------
void
handleScan(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  String json = "[";
  int n = WiFi.scanComplete();
  if(n == -2) {
    WiFi.scanNetworks(true);
  } else if(n) {
    for (int i = 0; i < n; ++i) {
      if(i) json += ",";
      json += "{";
      json += "\"rssi\":"+String(WiFi.RSSI(i));
      json += ",\"ssid\":\""+WiFi.SSID(i)+"\"";
      json += ",\"bssid\":\""+WiFi.BSSIDstr(i)+"\"";
      json += ",\"channel\":"+String(WiFi.channel(i));
      json += ",\"secure\":"+String(WiFi.encryptionType(i));
      json += ",\"hidden\":"+String(WiFi.isHidden(i)?"true":"false");
      json += "}";
    }
    WiFi.scanDelete();
    if(WiFi.scanComplete() == -2){
      WiFi.scanNetworks(true);
    }
  }
  json += "]";
  request->send(200, "text/json", json);
}

// -------------------------------------------------------------------
// Handle turning Access point off
// url: /apoff
// -------------------------------------------------------------------
void
handleAPOff(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  response->setCode(200);
  response->print("Turning AP Off");
  request->send(response);

  DBUGLN("Turning AP Off");
  systemRebootTime = millis() + 1000;
}

// -------------------------------------------------------------------
// Save selected network to EEPROM and attempt connection
// url: /savenetwork
// -------------------------------------------------------------------
void
handleSaveNetwork(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  String qsid = request->arg("ssid");
  String qpass = request->arg("pass");

  if (qsid != 0) {
    config_save_wifi(qsid, qpass);

    response->setCode(200);
    response->print("saved");
    wifiRestartTime = millis() + 2000;
  } else {
    response->setCode(400);
    response->print("No SSID");
  }

  request->send(response);
}

// -------------------------------------------------------------------
// Save Emoncms
// url: /saveemoncms
// -------------------------------------------------------------------
void
handleSaveEmoncms(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  config_save_emoncms(request->arg("server"),
                      request->arg("node"),
                      request->arg("apikey"),
                      request->arg("fingerprint"));

  char tmpStr[200];
  snprintf(tmpStr, sizeof(tmpStr), "Saved: %s %s %s %s",
           emoncms_server.c_str(),
           emoncms_node.c_str(),
           emoncms_apikey.c_str(),
           emoncms_fingerprint.c_str());
  DBUGLN(tmpStr);

  response->setCode(200);
  response->print(tmpStr);
  request->send(response);
}

// -------------------------------------------------------------------
// Save MQTT Config
// url: /savemqtt
// -------------------------------------------------------------------
void
handleSaveMqtt(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  config_save_mqtt(request->arg("server"),
                   request->arg("topic"),
                   request->arg("user"),
                   request->arg("pass"));

  char tmpStr[200];
  snprintf(tmpStr, sizeof(tmpStr), "Saved: %s %s %s %s", mqtt_server.c_str(),
          mqtt_topic.c_str(), mqtt_user.c_str(), mqtt_pass.c_str());
  DBUGLN(tmpStr);

  response->setCode(200);
  response->print(tmpStr);
  request->send(response);

  mqttRestartTime = millis();
}

// -------------------------------------------------------------------
// Save the web site user/pass
// url: /saveadmin
// -------------------------------------------------------------------
void
handleSaveAdmin(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  String quser = request->arg("user");
  String qpass = request->arg("pass");

  config_save_admin(quser, qpass);

  response->setCode(200);
  response->print("saved");
  request->send(response);
}

// -------------------------------------------------------------------
// Save the Ohm keyto EEPROM
// url: /handleSaveOhmkey
// -------------------------------------------------------------------
void
handleSaveOhmkey(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  String qohm = request->arg("ohm");

  config_save_ohm(qohm);

  response->setCode(200);
  response->print("saved");
  request->send(response);
}

// -------------------------------------------------------------------
// Returns status json
// url: /status
// -------------------------------------------------------------------
void
handleStatus(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  String s = "{";
  if (wifi_mode == WIFI_MODE_STA) {
    s += "\"mode\":\"STA\",";
  } else if (wifi_mode == WIFI_MODE_AP_STA_RETRY
             || wifi_mode == WIFI_MODE_AP_ONLY) {
    s += "\"mode\":\"AP\",";
  } else if (wifi_mode == WIFI_MODE_AP_AND_STA) {
    s += "\"mode\":\"STA+AP\",";
  }
  s += "\"networks\":[" + st + "],";
  s += "\"rssi\":[" + rssi + "],";

  s += "\"srssi\":\"" + String(WiFi.RSSI()) + "\",";
  s += "\"ipaddress\":\"" + ipaddress + "\",";
  s += "\"emoncms_connected\":\"" + String(emoncms_connected) + "\",";
  s += "\"packets_sent\":\"" + String(packets_sent) + "\",";
  s += "\"packets_success\":\"" + String(packets_success) + "\",";

  s += "\"mqtt_connected\":\"" + String(mqtt_connected()) + "\",";

  s += "\"ohm_hour\":\"" + ohm_hour + "\",";

  s += "\"free_heap\":\"" + String(ESP.getFreeHeap()) + "\"";

#ifdef ENABLE_LEGACY_API
  s += ",\"version\":\"" + currentfirmware + "\"";
  s += ",\"ssid\":\"" + esid + "\"";
  //s += ",\"pass\":\""+epass+"\""; security risk: DONT RETURN PASSWORDS
  s += ",\"emoncms_server\":\"" + emoncms_server + "\"";
  s += ",\"emoncms_node\":\"" + emoncms_node + "\"";
  //s += ",\"emoncms_apikey\":\""+emoncms_apikey+"\""; security risk: DONT RETURN APIKEY
  s += ",\"emoncms_fingerprint\":\"" + emoncms_fingerprint + "\"";
  s += ",\"mqtt_server\":\"" + mqtt_server + "\"";
  s += ",\"mqtt_topic\":\"" + mqtt_topic + "\"";
  s += ",\"mqtt_user\":\"" + mqtt_user + "\"";
  //s += ",\"mqtt_pass\":\""+mqtt_pass+"\""; security risk: DONT RETURN PASSWORDS
  s += ",\"www_username\":\"" + www_username + "\"";
  //s += ",\"www_password\":\""+www_password+"\""; security risk: DONT RETURN PASSWORDS
  s += ",\"ohmkey\":\"" + ohm + "\"";
#endif
  s += "}";

  response->setCode(200);
  response->print(s);
  request->send(response);
}

// -------------------------------------------------------------------
// Returns OpenEVSE Config json
// url: /config
// -------------------------------------------------------------------
void
handleConfig(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  String s = "{";
  s += "\"firmware\":\"" + firmware + "\",";
  s += "\"protocol\":\"" + protocol + "\",";
  s += "\"espflash\":\"" + String(espflash) + "\",";
  s += "\"version\":\"" + currentfirmware + "\",";
  s += "\"diodet\":\"" + String(diode_ck) + "\",";
  s += "\"gfcit\":\"" + String(gfci_test) + "\",";
  s += "\"groundt\":\"" + String(ground_ck) + "\",";
  s += "\"relayt\":\"" + String(stuck_relay) + "\",";
  s += "\"ventt\":\"" + String(vent_ck) + "\",";
  s += "\"tempt\":\"" + String(temp_ck) + "\",";
  s += "\"service\":\"" + String(service) + "\",";
  s += "\"l1min\":\"" + current_l1min + "\",";
  s += "\"l1max\":\"" + current_l1max + "\",";
  s += "\"l2min\":\"" + current_l2min + "\",";
  s += "\"l2max\":\"" + current_l2max + "\",";
  s += "\"scale\":\"" + current_scale + "\",";
  s += "\"offset\":\"" + current_offset + "\",";
  s += "\"gfcicount\":\"" + gfci_count + "\",";
  s += "\"nogndcount\":\"" + nognd_count + "\",";
  s += "\"stuckcount\":\"" + stuck_count + "\",";
  s += "\"kwhlimit\":\"" + kwh_limit + "\",";
  s += "\"timelimit\":\"" + time_limit + "\",";
  s += "\"ssid\":\"" + esid + "\",";
  //s += "\"pass\":\""+epass+"\","; security risk: DONT RETURN PASSWORDS
  s += "\"emoncms_server\":\"" + emoncms_server + "\",";
  s += "\"emoncms_node\":\"" + emoncms_node + "\",";
  // s += "\"emoncms_apikey\":\""+emoncms_apikey+"\","; security risk: DONT RETURN APIKEY
  s += "\"emoncms_fingerprint\":\"" + emoncms_fingerprint + "\",";
  s += "\"mqtt_server\":\"" + mqtt_server + "\",";
  s += "\"mqtt_topic\":\"" + mqtt_topic + "\",";
  s += "\"mqtt_user\":\"" + mqtt_user + "\",";
  //s += "\"mqtt_pass\":\""+mqtt_pass+"\","; security risk: DONT RETURN PASSWORDS
  s += "\"www_username\":\"" + www_username + "\"";
  //s += "\"www_password\":\""+www_password+"\","; security risk: DONT RETURN PASSWORDS
  s += "}";
  s.replace(" ", "");

  response->setCode(200);
  response->print(s);
  request->send(response);
}

 // -------------------------------------------------------------------
// Returns Updates JSON
// url: /rapiupdate
// -------------------------------------------------------------------
void
handleUpdate(AsyncWebServerRequest *request) {

  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  String s = "{";
  s += "\"comm_sent\":\"" + String(comm_sent) + "\",";
  s += "\"comm_success\":\"" + String(comm_success) + "\",";
#ifdef ENABLE_LEGACY_API
  s += "\"ohmhour\":\"" + ohm_hour + "\",";
  s += "\"espfree\":\"" + String(espfree) + "\",";
  s += "\"packets_sent\":\"" + String(packets_sent) + "\",";
  s += "\"packets_success\":\"" + String(packets_success) + "\",";
#endif
  s += "\"amp\":\"" + String(amp) + "\",";
  s += "\"pilot\":\"" + String(pilot) + "\",";
  s += "\"temp1\":\"" + String(temp1) + "\",";
  s += "\"temp2\":\"" + String(temp2) + "\",";
  s += "\"temp3\":\"" + String(temp3) + "\",";
  s += "\"estate\":\"" + String(estate) + "\",";
  s += "\"wattsec\":\"" + wattsec + "\",";
  s += "\"watthour\":\"" + watthour_total + "\"";
  s += "}";
  s.replace(" ", "");

  response->setCode(200);
  response->print(s);
  request->send(response);
}

// -------------------------------------------------------------------
// Reset config and reboot
// url: /reset
// -------------------------------------------------------------------
void
handleRst(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  config_reset();
  ESP.eraseConfig();

  response->setCode(200);
  response->print("1");
  request->send(response);

  systemRebootTime = millis() + 1000;
}

// -------------------------------------------------------------------
// Restart (Reboot)
// url: /restart
// -------------------------------------------------------------------
void
handleRestart(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  response->setCode(200);
  response->print("1");
  request->send(response);

  systemRestartTime = millis() + 1000;
}

/*
// -------------------------------------------------------------------
// Check for updates and display current version
// url: /firmware
// -------------------------------------------------------------------
/*
String handleUpdateCheck() {
  DEBUG.println("Running: " + currentfirmware);
  // Get latest firmware version number
  String latestfirmware = ota_get_latest_version();
  DEBUG.println("Latest: " + latestfirmware);
  // Update web interface with firmware version(s)
  String s = "{";
  s += "\"current\":\""+currentfirmware+"\",";
  s += "\"latest\":\""+latestfirmware+"\"";
  s += "}";
  server.send(200, "text/html", s);
  return (latestfirmware);
}
*/

// -------------------------------------------------------------------
// Update firmware
// url: /update
// -------------------------------------------------------------------
/*
void handleUpdate() {
  DEBUG.println("UPDATING...");
  delay(500);

  t_httpUpdate_return ret = ota_http_update();

  String str="error";
  switch(ret) {
    case HTTP_UPDATE_FAILED:
      str = DEBUG.printf("Update failed error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      str = "No update, running latest firmware";
      break;
    case HTTP_UPDATE_OK:
      retCode = 200;
      str = "Update done!";
      break;
  }
  server.send(retCode, "text/plain", str);
  DEBUG.println(str);
}
*/

// -------------------------------------------------------------------
// Update firmware
// url: /update
// -------------------------------------------------------------------
void
handleUpdateGet(AsyncWebServerRequest *request) {
  request->send(200, "text/html", "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>");
}

void
handleUpdatePost(AsyncWebServerRequest *request) {
  bool shouldReboot = !Update.hasError();
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : "FAIL");
  response->addHeader("Connection", "close");
  request->send(response);

  if(shouldReboot) {
    systemRestartTime = millis() + 1000;
  }
}

void
handleUpdateUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if(!index){
    DBUGF("Update Start: %s\n", filename.c_str());
    Update.runAsync(true);
    if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)){
#ifdef ENABLE_DEBUG
      Update.printError(DEBUG_PORT);
#endif
    }
  }
  if(!Update.hasError()){
    if(Update.write(data, len) != len){
#ifdef ENABLE_DEBUG
      Update.printError(DEBUG_PORT);
#endif
    }
  }
  if(final){
    if(Update.end(true)){
      DBUGF("Update Success: %uB\n", index+len);
    } else {
#ifdef ENABLE_DEBUG
      Update.printError(DEBUG_PORT);
#endif
    }
  }
}

void
handleRapi(AsyncWebServerRequest *request) {
  bool json = request->hasArg("json");

  AsyncResponseStream *response;
  if(false == requestPreProcess(request, response, json ? "application/json" : "text/html")) {
    return;
  }

  String s;

  if(false == json) {
    s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p>";
    s += "<b>Open Source Hardware</b><p>RAPI Command Sent<p>Common Commands:<p>";
    s += "Set Current - $SC XX<p>Set Service Level - $SL 1 - $SL 2 - $SL A<p>";
    s += "Get Real-time Current - $GG<p>Get Temperatures - $GP<p>";
    s += "<p>";
    s += "<form method='get' action='r'><label><b><i>RAPI Command:</b></i></label>";
    s += "<input name='rapi' length=32><p><input type='submit'></form>";
  }

  if(request->hasArg("rapi"))
  {
    String rapiString;
    String rapi = request->arg("rapi");

    // BUG: Really we should do this in the main loop not here...
    Serial.flush();
    Serial.println(rapi);
    delay(commDelay);
    while (Serial.available()) {
      rapiString = Serial.readStringUntil('\r');
    }

    if(json) {
      s = "{\"cmd\":\""+rapi+"\",\"ret\":\""+rapiString+"\"}";
    } else {
      s += rapi;
      s += "<p>&gt;";
      s += rapiString;
    }
  }
  if(false == json) {
    s += "<p></html>\r\n\r\n";
  }

  response->setCode(200);
  response->print(s);
  request->send(response);
}

void handleNotFound(AsyncWebServerRequest *request)
{
  DBUG("NOT_FOUND: ");
  if(request->method() == HTTP_GET) {
    DBUGF("GET");
  } else if(request->method() == HTTP_POST) {
    DBUGF("POST");
  } else if(request->method() == HTTP_DELETE) {
    DBUGF("DELETE");
  } else if(request->method() == HTTP_PUT) {
    DBUGF("PUT");
  } else if(request->method() == HTTP_PATCH) {
    DBUGF("PATCH");
  } else if(request->method() == HTTP_HEAD) {
    DBUGF("HEAD");
  } else if(request->method() == HTTP_OPTIONS) {
    DBUGF("OPTIONS");
  } else {
    DBUGF("UNKNOWN");
  }
  DBUGF(" http://%s%s", request->host().c_str(), request->url().c_str());

  if(request->contentLength()){
    DBUGF("_CONTENT_TYPE: %s", request->contentType().c_str());
    DBUGF("_CONTENT_LENGTH: %u", request->contentLength());
  }

  int headers = request->headers();
  int i;
  for(i=0; i<headers; i++) {
    AsyncWebHeader* h = request->getHeader(i);
    DBUGF("_HEADER[%s]: %s", h->name().c_str(), h->value().c_str());
  }

  int params = request->params();
  for(i = 0; i < params; i++) {
    AsyncWebParameter* p = request->getParam(i);
    if(p->isFile()){
      DBUGF("_FILE[%s]: %s, size: %u", p->name().c_str(), p->value().c_str(), p->size());
    } else if(p->isPost()){
      DBUGF("_POST[%s]: %s", p->name().c_str(), p->value().c_str());
    } else {
      DBUGF("_GET[%s]: %s", p->name().c_str(), p->value().c_str());
    }
  }

  request->send(404);
}

void
web_server_setup() {
  SPIFFS.begin(); // mount the fs

  // Setup the static files
  server.serveStatic("/", SPIFFS, "/")
    .setDefaultFile("index.html")
    .setAuthentication(www_username.c_str(), www_password.c_str());

  // Start server & server root html /
  server.on("/",handleHome);

  // Handle HTTP web interface button presses
  server.on("/generate_204", handleHome);  //Android captive portal. Maybe not needed. Might be handled by notFound
  server.on("/fwlink", handleHome);  //Microsoft captive portal. Maybe not needed. Might be handled by notFound
  server.on("/status", handleStatus);
  server.on("/rapiupdate", handleUpdate);
  server.on("/config", handleConfig);

  server.on("/savenetwork", handleSaveNetwork);
  server.on("/saveemoncms", handleSaveEmoncms);
  server.on("/savemqtt", handleSaveMqtt);
  server.on("/saveadmin", handleSaveAdmin);
  server.on("/saveohmkey", handleSaveOhmkey);

  server.on("/reset", handleRst);
  server.on("/restart", handleRestart);

  server.on("/rapi", handleRapi);
  server.on("/r", handleRapi);

  server.on("/scan", handleScan);
  server.on("/apoff", handleAPOff);

  // Simple Firmware Update Form
  server.on("/update", HTTP_GET, handleUpdateGet);
  server.on("/update", HTTP_POST, handleUpdatePost, handleUpdateUpload);

//  server.on("/firmware", [](){
//    if(requestPreProcess())   handleUpdateCheck();
//  });
//
//  server.on("/update", [](){
//    if(requestPreProcess()) handleUpdate();
//  });

  server.onNotFound(handleNotFound);
  server.begin();

  DEBUG.println("Server started");
}

void
web_server_loop() {
  // Do we need to restart the WiFi?
  if(wifiRestartTime > 0 && millis() > wifiRestartTime) {
    wifiRestartTime = 0;
    wifi_restart();
  }

  // Do we need to restart MQTT?
  if(mqttRestartTime > 0 && millis() > mqttRestartTime) {
    mqttRestartTime = 0;
    mqtt_restart();
  }

  // Do we need to restart the system?
  if(systemRestartTime > 0 && millis() > systemRestartTime) {
    systemRestartTime = 0;
    wifi_disconnect();
    ESP.restart();
  }

  // Do we need to reboot the system?
  if(systemRebootTime > 0 && millis() > systemRebootTime) {
    systemRebootTime = 0;
    wifi_disconnect();
    ESP.reset();
  }
}
