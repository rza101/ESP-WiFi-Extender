#define ARDUINOJSON_USE_LONG_LONG 1

#include <ArduinoJson.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <lwip/dns.h>
#include <lwip/napt.h>
#include <LwipDhcpServer.h>

//  BLINK

unsigned long lastBlink = 0;
unsigned long blinkConnectedInterval = 3000;
unsigned long blinkDisconnectedInterval = 500;
const short blinkChars = 500;

//  CONFIG

DynamicJsonDocument configDoc(768);
const char* configPath  = "/cfg";

//  FLAGS

boolean configResetTrigger = false;
boolean espRestartTrigger = false;
boolean firstConnect = true;
boolean reconnectFlag = false;

boolean setAPTrigger = true;
boolean setStaTrigger = true;
boolean setDNSTrigger = false;
boolean setPowerTrigger = true;
boolean setProtocolTrigger = true;

boolean noStation = true;

//  NAPT SETTINGS

const short napt       = 384;
const byte napt_port   = 32;

//  SERVER

AsyncWebServer server(80);
char key[65];

//  STATS

unsigned int freeHeap = 0;
byte heapFragPerc = 0;
unsigned int maxHeapBlock = 0;

unsigned long lastStatsUpdate = 0;

//  UPTIME

unsigned long lastUptimeUpdate = 0;
unsigned long uptime = 0;

// WLAN

IPAddress apIP;
IPAddress apGateway;
IPAddress apSubnet;
IPAddress apDNS1;
IPAddress apDNS2;

unsigned long lastScan = 0;

DynamicJsonDocument wifiScan(1024);

//  METHODS

void blinkLedLoop(){
  unsigned long interval = 0;
  
  if(WiFi.isConnected()){
    interval = blinkConnectedInterval;
  }else if(!firstConnect && !noStation){
    reconnectFlag = true;
    interval = blinkDisconnectedInterval;
  }else{
    interval = blinkDisconnectedInterval;
  }
  
  if(millis() - lastBlink >= interval){
    if((reconnectFlag || firstConnect) && !noStation){
      if(firstConnect){
        Serial.print("Connecting to ");
        Serial.println(WiFi.SSID());
      }
      
      if(reconnectFlag){
        Serial.print("Reconnecting to ");
        Serial.println(WiFi.SSID());
      }
    }
    
    for(int i = 1; i <= blinkChars; i++){
      Serial.print('\0');
      ESP.wdtFeed();
    }
    
    lastBlink = millis();
  }
}

void configInit(){
  char configJSON[768];
  readFile(LittleFS, configPath, configJSON, 768);

  DeserializationError err = deserializeJson(configDoc, (const char*) configJSON);

  if(configJSON[0] == 0 || err.code() != DeserializationError::Ok){
    // default config
    Serial.print("No Config File Found or config invalid/corrupted : ");
    Serial.println(err.c_str());
    Serial.println("Creating...");

    configDoc["apssid"] = "ESP8266 Repeater";
    configDoc["appass"] = "";
    configDoc["apIP"] = nullptr;
    configDoc["apGW"] = nullptr;
    configDoc["apSN"] = nullptr;
    
    configDoc["stassid"] = "";
    configDoc["stapass"] = "";
    configDoc["staIP"] = nullptr;
    configDoc["staGW"] = nullptr;
    configDoc["staSN"] = nullptr;

    configDoc["dns1"] = nullptr;
    configDoc["dns2"] = nullptr;
    
    configDoc["dbuser"] = "admin";
    configDoc["dbpass"] = "admin";
    
    configDoc["power"] = 20.5;
    configDoc["protocol"] = 3;

    saveConfig();
  }else{
    Serial.println("Config initialized from flash successfully");
  }
}

void espRestart(){
  WiFi.disconnect();
  WiFi.softAPdisconnect();

  espRestartTrigger = false;
  
  Serial.println("Restarting...");
  server.end();
  
  ESP.restart();
}

void generateConfig(char* buffer, short bufferSize){
  StaticJsonDocument<768> doc = configDoc;

  char* appass = (char*) doc["appass"].as<const char*>(); 
  char* stapass = (char*) doc["stapass"].as<const char*>();

  if(strlen(appass) != 0){
    for(int i = 2; i < strlen(appass) - 2; i++){
      appass[i] = '*';
    }
  }

  if(strlen(stapass) != 0){
    for(int i = 2; i < strlen(stapass) - 2; i++){
      stapass[i] = '*';
    }
  }

  doc["appass"] = appass;
  doc["stapass"] = stapass;
  
  doc["apIP"] = doc["apIP"] == nullptr ? "192.168.4.1"    : ntoip(doc["apIP"].as<unsigned long>()).toString();
  doc["apGW"] = doc["apGW"] == nullptr ? "192.168.4.1"    : ntoip(doc["apGW"].as<unsigned long>()).toString();
  doc["apSN"] = doc["apSN"] == nullptr ? "255.255.255.0"  : ntoip(doc["apSN"].as<unsigned long>()).toString();
  
  doc["staIP"] = doc["staIP"] == nullptr ? "DHCP" : ntoip(doc["staIP"].as<unsigned long>()).toString();
  doc["staGW"] = doc["staGW"] == nullptr ? "DHCP" : ntoip(doc["staGW"].as<unsigned long>()).toString();
  doc["staSN"] = doc["staSN"] == nullptr ? "DHCP" : ntoip(doc["staSN"].as<unsigned long>()).toString();
  
  doc["dns1"] = doc["dns1"] == nullptr ? "DHCP" : ntoip(doc["dns1"].as<unsigned long>()).toString();
  doc["dns2"] = doc["dns2"] == nullptr ? "DHCP" : ntoip(doc["dns2"].as<unsigned long>()).toString();

  doc.remove("dbuser");
  doc.remove("dbpass");

  serializeJson(doc, buffer, bufferSize);
}

void generateJSON(boolean success, const char* msgKey, const char* msgValue, char* buffer, short bufferSize, boolean withKey = false){
  StaticJsonDocument<256> doc;

  doc["success"] = success;
  doc[msgKey] = msgValue;

  if(withKey){
    generateRandomString(64, key);
    doc["key"] = key;
  }

  serializeJson(doc, buffer, bufferSize);
}

void generateJSONWithConfig(boolean success, char* msgKey, char* msgValue, char* buffer, short bufferSize){
  generateConfig(buffer, bufferSize);
  generateRandomString(64, key);

  StaticJsonDocument<1024> doc;

  doc["success"] = success;
  doc[msgKey] = msgValue;
  doc["config"] = buffer;
  doc["key"] = key;

  serializeJson(doc, buffer, bufferSize);
}

void generateRandomString(byte len, char* buffer){
  for(byte i = 0; i < len; i++){
    byte randomValue = map(ESP.random()/16777215, 0, 255, 0, 63);
    char letter = randomValue + 'a';
    
    if(randomValue >= 26 && randomValue <= 35){
      letter = (randomValue - 26) + '0';
    }else if(randomValue >= 36 && randomValue <= 61){
      letter = (randomValue - 36) + 'A';
    }else if(randomValue >= 62){
      letter = '_';
    }
    
    buffer[i] = letter;
  }

  buffer[len] = '\0';

  randomSeed(analogRead(A0)*ESP.random() + micros()*ESP.random());
}

void generateStats(char* buffer, short bufferSize){
  StaticJsonDocument<768> doc;
  
  const char* staStatus;
  const char* wlanMode;
  const char* wlanPhyMode;

  switch(WiFi.status()){
    case WL_IDLE_STATUS :
      staStatus = "Idle";
      break;
    case WL_NO_SSID_AVAIL :
      staStatus = "SSID Not Available";
      break;
    case WL_CONNECTED :
      staStatus = "Connected";
      break;
    case WL_CONNECT_FAILED :
      staStatus = "Connect Failed";
      break;
    case WL_CONNECTION_LOST :
      staStatus = "Connection Lost";
      break;
    case WL_WRONG_PASSWORD :
      staStatus = "Wrong Password";
      break;
    case WL_DISCONNECTED :
      staStatus = "SSID Not Set";
      break;
  }

  switch(WiFi.getMode()){
     case WIFI_OFF :
      wlanMode = "Off";
      break;
    case WIFI_STA :
      wlanMode = "Station";
      break;
    case WIFI_AP :
      wlanMode = "AP";
      break;
    case WIFI_AP_STA :
      wlanMode = "AP + Station";
      break;
  }

  switch(WiFi.getPhyMode()){
    case WIFI_PHY_MODE_11B :
      wlanPhyMode = "802.11b";
      break;
    case WIFI_PHY_MODE_11G :
      wlanPhyMode = "802.11g";
      break;
    case WIFI_PHY_MODE_11N :
      wlanPhyMode = "802.11n";
      break;
  }

  doc["wlanMode"] = wlanMode;
  doc["freeHeap"] = freeHeap;
  doc["heapFrag"] = heapFragPerc;
  doc["maxHeapBlock"] = maxHeapBlock;
  doc["uptime"] = uptime;

  doc["apSSID"] = WiFi.softAPSSID();
  doc["apClients"] = WiFi.softAPgetStationNum();
  doc["apMAC"] = WiFi.softAPmacAddress();
  doc["apIP"] = apIP.toString();
  doc["apGW"] = apGateway.toString();
  doc["apSN"] = apSubnet.toString();
  doc["apDNS1"] = apDNS1.toString();
  doc["apDNS2"] = apDNS2.toString();

  doc["staStatus"] = staStatus;
  doc["staSSID"] = WiFi.SSID();
  doc["staRSSI"] = WiFi.RSSI();
  doc["staMAC"] = WiFi.macAddress();
  doc["staIP"] = WiFi.localIP().toString();
  doc["staGW"] = WiFi.gatewayIP().toString();
  doc["staSN"] = WiFi.subnetMask().toString();
  doc["staDNS1"] = WiFi.dnsIP(0).toString();
  doc["staDNS2"] = WiFi.dnsIP(1).toString();

  serializeJson(doc, buffer, bufferSize);
}

boolean ipValid(const char* ip){
  unsigned long num = toul(ip);
  char buf[65];
  ultoa(num, buf, 10);

  return strcmp(ip, buf) == 0 && num > 0 && num < 4294967295;
}

IPAddress ntoip(unsigned long num){
  byte arr[4]; 
  byte arridx = 0;
  unsigned long divider = 16777216;

  while(divider >= 1){
      arr[arridx++] = num / divider;
      
      num %= divider;
      divider = divider >> 8;
  }

  return IPAddress(arr[0], arr[1], arr[2], arr[3]);
}

void readFile(fs::FS &fs, const char* path, char* buffer, short bufferSize){
  File file = fs.open(path, "r");
  
  if(!file || file.isDirectory()){
    buffer = "";
    return;
  }

  short i = 0;
  
  while(file.available() && i < bufferSize){
    buffer[i++] = (char) file.read();
  }

  buffer[i] = '\0';
}

void saveConfig(){
  char temp[768];

  serializeJson(configDoc, temp);
  deserializeJson(configDoc, (const char*) temp);

  if(writeFile(LittleFS, configPath, temp)){
    Serial.println("Configuration Saved");
  }
}

void statsLoop(){
  if(millis() - lastStatsUpdate >= 250){
    freeHeap = ESP.getFreeHeap();
    heapFragPerc = ESP.getHeapFragmentation();
    maxHeapBlock = ESP.getMaxFreeBlockSize();

    lastStatsUpdate = millis();
  }
}

unsigned long toul(const char* str){
  return strtoul(str, nullptr, 0);
}

void uptimeLoop(){
  if(millis() - lastUptimeUpdate >= 1000){
    uptime++;
    lastUptimeUpdate = millis();
  }
}

void wifiScanLoop(){
  if(noStation){
    if(millis() - lastScan >= 5000){
      WiFi.scanNetworks(true);
      lastScan = millis();
    }

    short n = WiFi.scanComplete();
    
    if(n >= 0){
      wifiScan.clear();
      
      for (int i = 0; i < n && i < 10; i++){
        JsonObject doc = wifiScan.createNestedObject();
        
        doc["ssid"] = WiFi.SSID(i);
        doc["open"] = WiFi.encryptionType(i) == ENC_TYPE_NONE ? 1 : 0;
        doc["rssi"] = WiFi.RSSI(i);
      }
      
      WiFi.scanDelete();
    }
  }
}

boolean writeFile(fs::FS &fs, const char* path, const char* message){
  File file = fs.open(path, "w");
  
  if(!file){
    return false;
  }
  
  if(file.print(message)){
    return true;
  }else{
    return false;
  }
}

//  SERVER METHODS

const char* getContentType(String filename){
  if(filename.endsWith(".gzp")){
    filename = filename.substring(0, filename.length() - 4);
  }
  
  if(filename.endsWith(".html")){
    return "text/html";
  }else if(filename.endsWith(".css")){
    return "text/css";
  }else if(filename.endsWith(".jpg")){
    return "image/jpeg";
  }else if(filename.endsWith(".png")){
    return "image/png";
  }else if(filename.endsWith(".js")){
    return "application/javascript";
  }
  
  return "text/plain";
}

String templateProcessor(const String& var){
  if(var.equals("CONFIG")){
    char configTemp[768];
    generateConfig(configTemp, 768);

    return configTemp;
  }else if(var.equals("STATS")){
    char statsTemp[768];
    generateStats(statsTemp, 768);

    return statsTemp;
  }else if(var.equals("SCAN")){
    char temp[1024];
    serializeJson(wifiScan, temp);

    return temp;
  }else if(var.equals("KEY")){
    return key;
  }
  
  return "";
}

void indexHandler(AsyncWebServerRequest *request){
  if(!request->authenticate("admin", configDoc["dbpass"].as<const char*>())){
    return request->requestAuthentication();
  }
  
  generateRandomString(64, key);
  request->send(LittleFS, "/index.html", "text/html", false, templateProcessor);
}

void logoutHandler(AsyncWebServerRequest *request){
  AsyncWebServerResponse* response = request->beginResponse(LittleFS, "/logout.html", "text/html");
  response->setCode(401);
  request->send(response);
}

void setAPHandler(AsyncWebServerRequest *request){
  if(!request->hasParam("key", true)){
    request -> send(404);
    return;
  }else{
    String keyParam = request -> getParam("key", true)->value() == nullptr ? "" : request -> getParam("key", true)->value();

    if(!keyParam.equals(key)){
      request -> send(404);
      return;
    }
  }

  char msg[1024];
  generateJSON(false, "message", "PARAM INVALID", msg, 1024, true);

  boolean save = false;

  if(request->hasParam("set", true)){
    String setValue = request -> getParam("set", true)->value() == nullptr ? "" : request -> getParam("set", true)->value();

    if(setValue.equals("ap")){
      if(request -> hasParam("apssid", true) && request -> hasParam("appass", true)){
        String apssid = request -> getParam("apssid", true)->value() == nullptr ? "" : request -> getParam("apssid", true)->value();
        String appass = request -> getParam("appass", true)->value() == nullptr ? "" : request -> getParam("appass", true)->value();

        if(apssid.length() >= 1 && apssid.length() <= 32 && (appass.length() == 0 || (appass.length() >= 8 && appass.length() <= 63))){
          configDoc["apssid"] = apssid;
          configDoc["appass"] = appass;
          
          save = true;
          generateJSONWithConfig(true, "message", "OK", msg, 1024);
        }else{
          generateJSON(false, "message", "SSID OR PASS LENGTH INVALID", msg, 1024, true);
        }
      }
    }else if(setValue.equals("ip")){
      if(request -> hasParam("defaultip", true)){
        boolean defaultip = (request -> getParam("defaultip", true)->value() == nullptr ? "" : request -> getParam("defaultip", true)->value()).equals("true") ? true : false;
        
        if(defaultip){
          configDoc["apIP"] = nullptr;
          configDoc["apGW"] = nullptr;
          configDoc["apSN"] = nullptr;
          
          save = true;
          generateJSONWithConfig(true, "message", "OK", msg, 1024);
        }else{
          if(request -> hasParam("ip", true) && request -> hasParam("gw", true) && request -> hasParam("sn", true)){
            const char* ip = request -> getParam("ip", true)->value() == nullptr ? "" : request -> getParam("ip", true)->value().c_str();
            const char* gw = request -> getParam("gw", true)->value() == nullptr ? "" : request -> getParam("gw", true)->value().c_str();
            const char* sn = request -> getParam("sn", true)->value() == nullptr ? "" : request -> getParam("sn", true)->value().c_str();
            
            if(ipValid(ip) && ipValid(gw)&& ipValid(sn)){
              configDoc["apIP"] = toul(ip);
              configDoc["apGW"] = toul(gw);
              configDoc["apSN"] = toul(sn);
              
              save = true;
              generateJSONWithConfig(true, "message", "OK", msg, 1024);
            }else{
              generateJSON(false, "message", "IP DATA INVALID", msg, 1024, true);
            }
          }
        }
      }
    }
  }

  request -> send(200, "application/json", msg);

  if(save){
    saveConfig();
    setAPTrigger = true;
  }
}

void setStationHandler(AsyncWebServerRequest *request){
  if(!request->hasParam("key", true)){
    request -> send(404);
    return;
  }else{
    String keyParam = request -> getParam("key", true)->value() == nullptr ? "" : request -> getParam("key", true)->value();

    if(!keyParam.equals(key)){
      request -> send(404);
      return;
    }
  }

  char msg[1024];
  generateJSON(false, "message", "PARAM INVALID", msg, 1024, true);

  boolean save = false;

  if(request->hasParam("set", true)){
      String setValue = request -> getParam("set", true)->value() == nullptr ? "" : request -> getParam("set", true)->value();

    if(setValue.equals("sta")){
      if(request -> hasParam("stassid", true) && request -> hasParam("stapass", true)){
        String stassid = request -> getParam("stassid", true)->value() == nullptr ? "" : request -> getParam("stassid", true)->value();
        String stapass = request -> getParam("stapass", true)->value() == nullptr ? "" : request -> getParam("stapass", true)->value();
  
        if(stassid.length() >= 0 && stassid.length() <= 32 && stapass.length() == 0 || (stapass.length() >= 8 && stapass.length() <= 63)){
          configDoc["stassid"] = stassid;
          configDoc["stapass"] = stapass;
          
          save = true;
          generateJSONWithConfig(true, "message", "OK", msg, 1024);
        }else{
          generateJSON(false, "message", "SSID OR PASS LENGTH INVALID", msg, 1024, true);
        }
      }
    }else if(setValue.equals("ip")){
      if(request -> hasParam("dhcp", true)){
        boolean dhcp = (request -> getParam("dhcp", true)->value() == nullptr ? "" : request -> getParam("dhcp", true)->value()).equals("true") ? true : false;
        
        if(dhcp){
          configDoc["staIP"] = nullptr;
          configDoc["staGW"] = nullptr;
          configDoc["staSN"] = nullptr;
          
          save = true;
          generateJSONWithConfig(true, "message", "OK", msg, 1024);
        }else{
          if(request -> hasParam("ip", true) && request -> hasParam("gw", true) && request -> hasParam("sn", true)){
            const char* ip = request -> getParam("ip", true)->value() == nullptr ? "" : request -> getParam("ip", true)->value().c_str();
            const char* gw = request -> getParam("gw", true)->value() == nullptr ? "" : request -> getParam("gw", true)->value().c_str();
            const char* sn = request -> getParam("sn", true)->value() == nullptr ? "" : request -> getParam("sn", true)->value().c_str();
            
            if(ipValid(ip) && ipValid(gw)&& ipValid(sn)){
              configDoc["staIP"] = toul(ip);
              configDoc["staGW"] = toul(gw);
              configDoc["staSN"] = toul(sn);
              
              save = true;
              generateJSONWithConfig(true, "message", "OK", msg, 1024);
            }else{
              generateJSON(false, "message", "IP DATA INVALID", msg, 1024, true);
            }
          }
        }
      }
    }
  }
  
  request -> send(200, "application/json", msg);

  if(save){
    saveConfig();
    setStaTrigger = true;
  }
}

void setDNSHandler(AsyncWebServerRequest *request){
  if(!request->hasParam("key", true)){
    request -> send(404);
    return;
  }else{
    String keyParam = request -> getParam("key", true)->value() == nullptr ? "" : request -> getParam("key", true)->value();

    if(!keyParam.equals(key)){
      request -> send(404);
      return;
    }
  }

  char msg[1024];
  generateJSON(false, "message", "PARAM INVALID", msg, 1024, true);

  boolean save = false;

  if(request->hasParam("dhcp", true)){
    boolean dhcp = (request -> getParam("dhcp", true)->value() == nullptr ? "" : request -> getParam("dhcp", true)->value()).equals("true") ? true : false;

    if(dhcp){
      configDoc["dns1"] = nullptr;
      configDoc["dns2"] = nullptr;

      save = true;
      generateJSONWithConfig(true, "message", "OK", msg, 1024);
    }else{
      if(request -> hasParam("dns1", true) && request -> hasParam("dns2", true)){
        const char* dns1 = request -> getParam("dns1", true)->value() == nullptr ? "" : request -> getParam("dns1", true)->value().c_str();
        const char* dns2 = request -> getParam("dns2", true)->value() == nullptr ? "" : request -> getParam("dns2", true)->value().c_str();
        
        if(ipValid(dns1) && ipValid(dns2)){
          configDoc["dns1"] = toul(dns1);
          configDoc["dns2"] = toul(dns2);
          
          save = true;
          generateJSONWithConfig(true, "message", "OK", msg, 1024);
        }else{
          generateJSON(false, "message", "IP DATA INVALID", msg, 1024, true);
        }
      }
    }
  }

  request -> send(200, "application/json", msg);
  
  if(save){
    saveConfig();
    setDNSTrigger = true;
  }
}

void setWLANHandler(AsyncWebServerRequest *request){
  if(!request->hasParam("key", true)){
    request -> send(404);
    return;
  }else{
    String keyParam = request -> getParam("key", true)->value() == nullptr ? "" : request -> getParam("key", true)->value();

    if(!keyParam.equals(key)){
      request -> send(404);
      return;
    }
  }

  char msg[1024];
  generateJSON(false, "message", "PARAM INVALID", msg, 1024, true);

  boolean powerSet = false;
  boolean protocolSet = false;

  if(request->hasParam("set", true)){
    String setValue = request -> getParam("set", true)->value() == nullptr ? "" : request -> getParam("set", true)->value();

    if(setValue.equals("power")){
      if(request->hasParam("value", true)){
        float power = (request -> getParam("value", true)->value() == nullptr ? "-1" : request -> getParam("value", true)->value()).toFloat();

        if(power >= 0 && power <= 20.5){
          configDoc["power"] = power;
          
          generateJSONWithConfig(true, "message", "OK", msg, 1024);
          powerSet = true;
        }else{
          generateJSON(false, "message", "POWER INVALID", msg, 1024, true);
        }
      }
    }else if(setValue.equals("protocol")){
      if(request->hasParam("value", true)){
        byte protocol = (request -> getParam("value", true)->value() == nullptr ? "0" : request -> getParam("value", true)->value()).toInt();

        if(protocol == 1 || protocol == 2 || protocol == 3){
          configDoc["protocol"] = protocol;
          
          generateJSONWithConfig(true, "message", "OK", msg, 1024);
          protocolSet = true;
        }else{
          generateJSON(false, "message", "PROTOCOL INVALID", msg, 1024, true);
        }
      }
    }
  }

  request -> send(200, "application/json", msg);

  if(powerSet){
    saveConfig();
    setPowerTrigger = true;
  }

  if(protocolSet){
    saveConfig();
    setProtocolTrigger = true;
  }
}

void setCredentialHandler(AsyncWebServerRequest *request){
  if(!request->hasParam("key", true)){
    request -> send(404);
    return;
  }else{
    String keyParam = request -> getParam("key", true)->value() == nullptr ? "" : request -> getParam("key", true)->value();

    if(!keyParam.equals(key)){
      request -> send(404);
      return;
    }
  }

  char msg[1024];
  generateJSON(false, "message", "PARAM INVALID", msg, 1024, true);

  boolean save = false;

  if(request->hasParam("oldpass", true) && request->hasParam("newpass", true)){
    String oldpass = request -> getParam("oldpass", true)->value() == nullptr ? "" : request -> getParam("oldpass", true)->value();
    String newpass = request -> getParam("newpass", true)->value() == nullptr ? "" : request -> getParam("newpass", true)->value();
    
    if(oldpass.equals(configDoc["dbpass"].as<String>())){
      if(newpass.length() >= 5 && newpass.length() <= 32){
        configDoc["dbpass"] = newpass;

        save = true;
        generateJSON(true, "message", "OK", msg, 1024, true);
      }else{
        generateJSON(false, "message", "NEW PASSWORD LENGTH INVALID", msg, 1024, true);
      }
    }else{
      generateJSON(false, "message", "OLD PASSWORD NOT MATCH", msg, 1024, true);
    }
  }

  request -> send(200, "application/json", msg);

  if(save){
    saveConfig();
  }
}

void getConfigHandler(AsyncWebServerRequest *request){
  if(!request->hasParam("key", true)){
    request -> send(404);
    return;
  }else{
    String keyParam = request -> getParam("key", true)->value() == nullptr ? "" : request -> getParam("key", true)->value();

    if(!keyParam.equals(key)){
      request -> send(404);
      return;
    }
  }

  StaticJsonDocument<1024> doc;
  char result[1024];

  generateConfig(result, 1024);
  generateRandomString(64, key);

  doc["success"] = true;
  doc["message"] = result;
  doc["key"] = key;

  serializeJson(doc, result);
  
  request -> send(200, "application/json", result);
}

void getStatsHandler(AsyncWebServerRequest *request){
  if(!request->hasParam("key", true)){
    request -> send(404);
    return;
  }else{
    String keyParam = request -> getParam("key", true)->value() == nullptr ? "" : request -> getParam("key", true)->value();

    if(!keyParam.equals(key)){
      request -> send(404);
      return;
    }
  }

  StaticJsonDocument<1024> doc;
  char result[1024];

  generateStats(result, 1024);
  generateRandomString(64, key);

  doc["success"] = true;
  doc["message"] = result;
  doc["key"] = key;

  serializeJson(doc, result);
  
  request -> send(200, "application/json", result);
}

void getWifiScanHandler(AsyncWebServerRequest *request){
  if(!request->hasParam("key", true)){
    request -> send(404);
    return;
  }else{
    String keyParam = request -> getParam("key", true)->value() == nullptr ? "" : request -> getParam("key", true)->value();

    if(!keyParam.equals(key)){
      request -> send(404);
      return;
    }
  }

  char temp[1280];
  serializeJson(wifiScan, temp);

  StaticJsonDocument<1280> doc;

  generateRandomString(64, key);

  doc["success"] = true;
  doc["message"] = temp;
  doc["key"] = key;
  
  serializeJson(doc, temp);
  
  request -> send(200, "application/json", temp);
}

void getKeyHandler(AsyncWebServerRequest *request){
  if(!request->authenticate("admin", configDoc["dbpass"].as<const char*>())){
    return request->requestAuthentication();
  }
  
  request -> send(200, "application/json", key);
}

void resetHandler(AsyncWebServerRequest *request){
  if(!request->hasParam("key", true)){
    request -> send(404);
    return;
  }else{
    String keyParam = request -> getParam("key", true)->value() == nullptr ? "" : request -> getParam("key", true)->value();

    if(!keyParam.equals(key)){
      request -> send(404);
      return;
    }
  }

  char data[256];
  generateJSON(true, "message", "Resetting Config and restarting", data, 256);
  
  request -> send(200, "application/json", data);
  configResetTrigger = true;
}

void restartHandler(AsyncWebServerRequest *request){
  if(!request->hasParam("key", true)){
    request -> send(404);
    return;
  }else{
    String keyParam = request -> getParam("key", true)->value() == nullptr ? "" : request -> getParam("key", true)->value();

    if(!keyParam.equals(key)){
      request -> send(404);
      return;
    }
  }

  char data[256];
  generateJSON(true, "message", "Restarting", data, 256);
  
  request -> send(200, "application/json", data);
  espRestartTrigger = true;
}

void notFound(AsyncWebServerRequest *request){
  if(request->url() == "/main.js.gzp"){
    if(!request->hasParam("key")){
      request -> send(404);
      return;
    }else{
      String keyParam = request -> getParam("key")->value() == nullptr ? "" : request -> getParam("key")->value();

      if(!keyParam.equals(key)){
        request -> send(404);
        return;
      }
    }

    AsyncWebServerResponse* response = request->beginResponse(LittleFS, "/main.js.gzp", getContentType(request->url()), false);
    response->addHeader("Cache-Control", "max-age=86400");
    response->addHeader("Content-Encoding", "gzip");
    
    request->send(response);
  }else{
    AsyncWebServerResponse* response = request->beginResponse(LittleFS, request->url(), getContentType(request->url()), false);
  
    if(response == NULL){
      request->send(404);
    }else{
      response->addHeader("Cache-Control", "max-age=86400");
      
      if(request->url().endsWith(".gzp")){
        response->addHeader("Content-Encoding", "gzip");
      }
      
      request->send(response);
    }
  }
}

void setup() {
  Serial.begin(115200);

  Serial.println("WiFi Extender");

  if(LittleFS.begin()){
    FSInfo fs_info;
    LittleFS.info(fs_info);
    
    Serial.println("LittleFS Mount Success");
    Serial.print("Space Used : ");
    Serial.println(fs_info.usedBytes);
  }else{
    Serial.println("LittleFS Mount Fail");
    espRestart();
  }
  
  configInit();

  for(int i = 1; i <= 100; i++){
    randomSeed(analogRead(A0)*ESP.random() + micros()*ESP.random());
  }

  Serial.println("Random initialized");

  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);

  uint8_t currapmac[6];
  uint8_t currstamac[6];

  WiFi.softAPmacAddress(currapmac);
  WiFi.macAddress(currstamac);
  
  uint8_t apmac[] = {0x60, 0x01, 0x94, currapmac[3], currapmac[4], currapmac[5]};
  uint8_t stamac[] = {0x60, 0x01, 0x94, currstamac[3], currstamac[4], currstamac[5]};
  
  wifi_set_macaddr(SOFTAP_IF, &apmac[0]);
  wifi_set_macaddr(STATION_IF, &stamac[0]);
  
  err_t initNAPT = ip_napt_init(napt, napt_port);
  
  if(initNAPT == ERR_OK){
    Serial.print("NAPT initialization with ");
    Serial.print(napt);
    Serial.print(" NAT entries and ");
    Serial.print(napt_port);
    Serial.print(" port entries success (Error : ");
    Serial.print(initNAPT);
    Serial.println(")");
    
    err_t enableNAPT = ip_napt_enable_no(SOFTAP_IF, true);
    
    if(enableNAPT == ERR_OK){
      Serial.print("NAPT enabling success (Error : ");
      Serial.print(enableNAPT);
      Serial.println(")");
      
      Serial.println("AP has been NATed behind station");
    }else{
      Serial.print("NAPT enabling failed (Error : ");
      Serial.print(enableNAPT);
      Serial.println(")");
    }
  }else{
    Serial.print("NAPT initialization failed (Error : ");
    Serial.print(initNAPT);
    Serial.println(")");
  }

  generateRandomString(64, key);

  server.on("/", HTTP_GET, indexHandler);
  server.on("/index", HTTP_GET, indexHandler);
  server.on("/index.html", HTTP_GET, indexHandler);
  server.on("/logout", HTTP_GET, logoutHandler);
  server.on("/logout.html", HTTP_GET, logoutHandler);

  server.on("/setap", HTTP_POST, setAPHandler);
  server.on("/setsta", HTTP_POST, setStationHandler);
  server.on("/setdns", HTTP_POST, setDNSHandler);
  server.on("/setwlan", HTTP_POST, setWLANHandler);
  server.on("/setcredential", HTTP_POST, setCredentialHandler);

  server.on("/getconfig", HTTP_POST, getConfigHandler);
  server.on("/getstats", HTTP_POST, getStatsHandler);
  server.on("/getwifiscan", HTTP_POST, getWifiScanHandler);
  
  server.on("/getkey", HTTP_GET, getKeyHandler);

  server.on("/reset", HTTP_POST, resetHandler);
  server.on("/restart", HTTP_POST, restartHandler);

  server.onNotFound(notFound);
  
  server.begin();

  Serial.print("Free Heap: ");
  Serial.println(ESP.getFreeHeap(), DEC);

  Serial.print("Heap Fragmentation (%): ");
  Serial.println(ESP.getHeapFragmentation(), DEC);

  Serial.print("Max Heap Block: ");
  Serial.println(ESP.getMaxFreeBlockSize(), DEC);

  freeHeap = ESP.getFreeHeap();
  heapFragPerc = ESP.getHeapFragmentation();
  maxHeapBlock = ESP.getMaxFreeBlockSize();

  uptime = millis()/1000 + 1;
  lastUptimeUpdate = millis();
}

void loop() {
  if(setAPTrigger){
    WiFi.softAPdisconnect();

    delay(100);

    const char* apssid = configDoc["apssid"].as<const char*>();
    const char* appass = configDoc["appass"].as<const char*>();

    if(configDoc["apIP"] == nullptr || configDoc["apGW"] == nullptr || configDoc["apSN"] == nullptr){
      apIP = IPAddress(192, 168, 4, 1);
      apGateway = IPAddress(192, 168, 4, 1);
      apSubnet = IPAddress(255, 255, 255, 0);
    }else{
      apIP = ntoip(configDoc["apIP"].as<unsigned long>());
      apGateway = ntoip(configDoc["apGW"].as<unsigned long>());
      apSubnet = ntoip(configDoc["apSN"].as<unsigned long>());
    }

    WiFi.softAPConfig(apIP, apGateway, apSubnet);

    if(strcmp(appass, "") == 0){
      WiFi.softAP(apssid);
    }else{
      WiFi.softAP(apssid, appass);
    }

    Serial.print("AP ");
    Serial.print(apssid);
    Serial.print(" (IP : ");
    Serial.print(WiFi.softAPIP().toString());
    Serial.println(") initialized");

    setAPTrigger = false;
  }

  if(setStaTrigger){
    WiFi.disconnect();

    delay(100);

    const char* stassid = configDoc["stassid"].as<const char*>();
    const char* stapass = configDoc["stapass"].as<const char*>();

    if(strcmp(stassid, "") == 0){
      Serial.println("WLAN NO STATION");
      noStation = true;
    }else{
      if(configDoc["staIP"] != nullptr && configDoc["staGW"] != nullptr && configDoc["staSN"] != nullptr){
        WiFi.config(ntoip(configDoc["staIP"].as<unsigned long>()), ntoip(configDoc["staGW"].as<unsigned long>()), ntoip(configDoc["staSN"].as<unsigned long>()));
      }

      if(strcmp(stapass, "") == 0){
        WiFi.begin(stassid);
      }else{
        WiFi.begin(stassid, stapass);
      }

      Serial.print("Station set to ");
      Serial.println(WiFi.SSID());

      noStation = false;
      firstConnect = true;

      wifiScan.clear();
      wifiScan.to<JsonArray>();

      WiFi.scanDelete();
    }

    setStaTrigger = false;
  }

  if(setDNSTrigger){
    if(configDoc["dns1"] == nullptr && configDoc["dns2"] == nullptr){
      if(WiFi.isConnected()){
        apDNS1 = WiFi.dnsIP(0);
        apDNS2 = WiFi.dnsIP(1);

        dhcpSoftAP.dhcps_set_dns(0, WiFi.dnsIP(0));
        dhcpSoftAP.dhcps_set_dns(1, WiFi.dnsIP(1));

        Serial.println("DNS set DHCP");

        setDNSTrigger = false;
      }
    }else{
      apDNS1 = ntoip(configDoc["dns1"].as<unsigned long>());
      apDNS2 = ntoip(configDoc["dns2"].as<unsigned long>());

      dhcpSoftAP.dhcps_set_dns(0, ntoip(configDoc["dns1"].as<unsigned long>()));
      dhcpSoftAP.dhcps_set_dns(1, ntoip(configDoc["dns2"].as<unsigned long>()));

      Serial.print("DNS set custom 1 : ");
      Serial.print(ntoip(configDoc["dns1"].as<unsigned long>()).toString());
      Serial.print(" 2 : ");
      Serial.println(ntoip(configDoc["dns2"].as<unsigned long>()).toString());

      setDNSTrigger = false;
    }
  }

  if(setPowerTrigger){
    float power = configDoc["power"].as<float>();
    WiFi.setOutputPower(power);

    Serial.print("WiFi Tx power set to ");
    Serial.println(power);

    setPowerTrigger = false;
  }

  if(setProtocolTrigger){
    WiFiPhyMode_t protocol = (WiFiPhyMode_t) configDoc["protocol"].as<byte>();

    const char* wlanPhyMode;

    switch(protocol){
      case WIFI_PHY_MODE_11B :
        wlanPhyMode = "802.11b";
        break;
      case WIFI_PHY_MODE_11G :
        wlanPhyMode = "802.11g";
        break;
      case WIFI_PHY_MODE_11N :
        wlanPhyMode = "802.11n";
        break;
    }

    WiFi.setPhyMode(protocol);

    Serial.print("WiFi Protocol set to ");
    Serial.println(wlanPhyMode);

    setProtocolTrigger = false;
  }

  blinkLedLoop();
  statsLoop();
  uptimeLoop();
  wifiScanLoop();

  if((firstConnect || reconnectFlag) && WiFi.isConnected()){
    if(firstConnect){
       Serial.print("Connected to ");

       firstConnect = false;
       setDNSTrigger = true;
    }
    
    if(reconnectFlag){
       Serial.print("Reconnected to ");
       
       reconnectFlag = false;
       setDNSTrigger = true;
    }

    Serial.print(WiFi.SSID());
    Serial.print(" (IP : ");
    Serial.print(WiFi.localIP().toString());
    Serial.println(")");
  }

  if(Serial.available() > 0){
    String param = Serial.readStringUntil('\n');

    param.trim();

    if(param.equalsIgnoreCase("reset")){
      configResetTrigger = true;
    }else if(param.equalsIgnoreCase("restart")){
      espRestartTrigger = true;
    }else if(param.equalsIgnoreCase("help")){
      Serial.println("Serial commands are case insensitive");
      Serial.println("help - show commands");
      Serial.println("reset - reset all configuration");
      Serial.println("restart - restarts module");
    }else{
      Serial.println("PARAM INVALID");
    }
  }

  if(configResetTrigger){
    writeFile(LittleFS, configPath, "");
    
    Serial.println("Reset Config Success");
    
    espRestart();
  }else if(espRestartTrigger){
    espRestart();
  }
}
