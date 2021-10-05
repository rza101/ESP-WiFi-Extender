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
unsigned int blinkChars = 500;

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

const int napt        = 256;
const int napt_port   = 32;

//  SERVER

AsyncWebServer server(80);
char key[129];
char mainJS[34];

//  STATS

unsigned long freeHeap = 0;
unsigned long heapFragPerc = 0;
unsigned long maxHeapBlock = 0;

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
        Serial.println("Connecting to " + WiFi.SSID() + "...");
      }
      
      if(reconnectFlag){
        Serial.println("Reconnecting to " + WiFi.SSID() + "...");
      }
    }
    
    for(int i = 1; i <= blinkChars; i++){
      Serial.print((char) 0); //NULL, not printable
    }
    
    lastBlink = millis();
  }
}

void configInit(){
  String configJSON = readFile(LittleFS, configPath);

  if(configJSON != ""){
    deserializeJson(configDoc, configJSON);
  }else{
    // default config
    Serial.println("No Config File Found");
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

String generateJSON(boolean success, String msgKey, String msgValue, boolean withKey = false){
  StaticJsonDocument<256> doc;
  String result = "";

  doc["success"] = success;
  doc[msgKey] = msgValue;

  if(withKey){
    generateRandomString(128).toCharArray(key, 129);
    doc["key"] = key;
  }

  serializeJson(doc, result);

  return result;
}

String generateJSONWithConfig(boolean success, String msgKey, String msgValue){
  StaticJsonDocument<1024> doc;
  String result = "";

  generateRandomString(128).toCharArray(key, 129);

  doc["success"] = success;
  doc[msgKey] = msgValue;
  doc["config"] = generateConfig();
  doc["key"] = key;

  serializeJson(doc, result);

  return result;
}

String generateRandomString(byte len){
  String randString = "";

  for(byte i = 1; i <= len; i++){
    byte randomValue = map(ESP.random()/16777215, 0, 255, 0, 63);
    char letter = randomValue + 'a';
    
    if(randomValue >= 26 && randomValue <= 35){
      letter = (randomValue - 26) + '0';
    }else if(randomValue >= 36 && randomValue <= 61){
      letter = (randomValue - 36) + 'A';
    }else if(randomValue >= 62){
      letter = '_';
    }
    
    randString += letter;
  }

  randomSeed(analogRead(A0)*ESP.random() + micros()*ESP.random());

  return randString;
}

String generateConfig(){
  String temp = "";
  StaticJsonDocument<768> doc = configDoc;

  String appass = doc["appass"].as<String>();
  String stapass = doc["stapass"].as<String>();

  if(appass.length() != 0){
    for(int i = 2; i < appass.length() - 2; i++){
      appass.setCharAt(i, '*');
    }
  }

  if(stapass.length() != 0){
    for(int i = 2; i < stapass.length() - 2; i++){
      stapass.setCharAt(i, '*');
    }
  }

  doc["appass"] = appass;

  doc["stassid"] = doc["stassid"].as<String>();
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

  serializeJson(doc, temp);

  return temp;
}

String generateStats(){
  String stats = "";
  String staStatus = "";
  String wlanMode = "";
  String wlanPhyMode = "";
  
  StaticJsonDocument<768> doc;

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

  serializeJson(doc, stats);

  return stats;
}

boolean ipValid(String& ip){
  unsigned long num = toul(ip);
  return ip.equals(String(num)) && num > 0 && num < 4294967295;
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

String readFile(fs::FS &fs, const char* path){
  File file = fs.open(path, "r");
  
  if(!file || file.isDirectory()){
    return String();
  }

  String result;
  
  while(file.available()){
    result += (char) file.read();
  }
  
  return result;
}

void saveConfig(){
  String temp = "";
  serializeJson(configDoc, temp);
  deserializeJson(configDoc, temp);
  
  if(writeFile(LittleFS, configPath, temp.c_str())){
    Serial.println("Configuration Saved");
  }
}

void statsLoop(){
  if(millis() - lastStatsUpdate >= 1000){
    freeHeap = ESP.getFreeHeap();
    heapFragPerc = ESP.getHeapFragmentation();
    maxHeapBlock = ESP.getMaxFreeBlockSize();

    lastStatsUpdate = millis();
  }
}

void uptimeLoop(){
  if(millis() - lastUptimeUpdate >= 1000){
    uptime++;
    lastUptimeUpdate = millis();
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


String getContentType(String filename){
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
  if(var.equals("MAIN_JS")){
    return String(mainJS);
  }else if(var.equals("CONFIG")){
    return generateConfig();
  }else if(var.equals("STATS")){
    return generateStats();
  }else if(var.equals("KEY")){
    return String(key);
  }
  
  return String();
}

unsigned long toul(String str){
  return strtoul(str.c_str(), nullptr, 0);
}

void indexHandler(AsyncWebServerRequest *request){
  if(!request->authenticate("admin", configDoc["dbpass"].as<const char*>())){
    return request->requestAuthentication();
  }
  
  generateRandomString(128).toCharArray(key, 129);
  request->send(LittleFS, "/index.html", "text/html", false, templateProcessor);
}

void mainJSHandler(AsyncWebServerRequest *request){
  AsyncWebServerResponse* response = request->beginResponse(LittleFS, "/main.js.gzp", "application/javascript");
  response->addHeader("Cache-Control", "max-age=86400");
  response->addHeader("Content-Encoding", "gzip");
  
  request->send(response);
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

  String msg = generateJSON(false, "message", "PARAM INVALID", true);
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
          msg = generateJSONWithConfig(true, "message", "OK");
        }else{
          msg = generateJSON(false, "message", "SSID OR PASS LENGTH INVALID", true);
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
          msg = generateJSONWithConfig(true, "message", "OK");
        }else{
          if(request -> hasParam("ip", true) && request -> hasParam("gw", true) && request -> hasParam("sn", true)){
            String ip = request -> getParam("ip", true)->value() == nullptr ? "" : request -> getParam("ip", true)->value();
            String gw = request -> getParam("gw", true)->value() == nullptr ? "" : request -> getParam("gw", true)->value();
            String sn = request -> getParam("sn", true)->value() == nullptr ? "" : request -> getParam("sn", true)->value();
            
            if(ipValid(ip) && ipValid(gw)&& ipValid(sn)){
              configDoc["apIP"] = toul(ip);
              configDoc["apGW"] = toul(gw);
              configDoc["apSN"] = toul(sn);
              
              save = true;
              msg = generateJSONWithConfig(true, "message", "OK");
            }else{
              msg = generateJSON(false, "message", "IP DATA INVALID", true);
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

  String msg = generateJSON(false, "message", "PARAM INVALID", true);
  boolean save = false;

  if(request->hasParam("set", true)){
    String setValue = request -> getParam("set", true)->value() == nullptr ? "" : request -> getParam("set", true)->value();

    if(setValue.equals("sta")){
      if(request -> hasParam("stassid", true) && request -> hasParam("stapass", true)){
        String stassid = request -> getParam("stassid", true)->value() == nullptr ? "" : request -> getParam("stassid", true)->value();
        String stapass = request -> getParam("stapass", true)->value() == nullptr ? "" : request -> getParam("stapass", true)->value();
  
        if(stassid.length() >= 0 && stassid.length() <= 32 && (stapass.length() == 0 || (stapass.length() >= 8 && stapass.length() <= 63))){
          configDoc["stassid"] = stassid;
          configDoc["stapass"] = stapass;
          
          save = true;
          msg = generateJSONWithConfig(true, "message", "OK");
        }else{
          msg = generateJSON(false, "message", "SSID OR PASS LENGTH INVALID", true);
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
          msg = generateJSONWithConfig(true, "message", "OK");
        }else{
          if(request -> hasParam("ip", true) && request -> hasParam("gw", true) && request -> hasParam("sn", true)){
            String ip = request -> getParam("ip", true)->value() == nullptr ? "" : request -> getParam("ip", true)->value();
            String gw = request -> getParam("gw", true)->value() == nullptr ? "" : request -> getParam("gw", true)->value();
            String sn = request -> getParam("sn", true)->value() == nullptr ? "" : request -> getParam("sn", true)->value();
            
            if(ipValid(ip) && ipValid(gw)&& ipValid(sn)){
              configDoc["staIP"] = toul(ip);
              configDoc["staGW"] = toul(gw);
              configDoc["staSN"] = toul(sn);
              
              save = true;
              msg = generateJSONWithConfig(true, "message", "OK");
            }else{
              msg = generateJSON(false, "message", "IP DATA INVALID", true);
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

  String msg = generateJSON(false, "message", "PARAM INVALID", true);
  boolean save = false;

  if(request->hasParam("dhcp", true)){
    boolean dhcp = (request -> getParam("dhcp", true)->value() == nullptr ? "" : request -> getParam("dhcp", true)->value()).equals("true") ? true : false;

    if(dhcp){
      configDoc["dns1"] = nullptr;
      configDoc["dns2"] = nullptr;

      save = true;
      msg = generateJSONWithConfig(true, "message", "OK");
    }else{
      if(request -> hasParam("dns1", true) && request -> hasParam("dns2", true)){
        String dns1 = request -> getParam("dns1", true)->value() == nullptr ? "" : request -> getParam("dns1", true)->value();
        String dns2 = request -> getParam("dns2", true)->value() == nullptr ? "" : request -> getParam("dns2", true)->value();
        
        if(ipValid(dns1) && ipValid(dns2)){
          configDoc["dns1"] = toul(dns1);
          configDoc["dns2"] = toul(dns2);
          
          save = true;
          msg = generateJSONWithConfig(true, "message", "OK");
        }else{
          msg = generateJSON(false, "message", "IP DATA INVALID", true);
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

  String msg = generateJSON(false, "message", "PARAM INVALID", true);
  boolean powerSet = false;
  boolean protocolSet = false;

  if(request->hasParam("set", true)){
    String setValue = request -> getParam("set", true)->value() == nullptr ? "" : request -> getParam("set", true)->value();

    if(setValue.equals("power")){
      if(request->hasParam("value", true)){
        float power = (request -> getParam("value", true)->value() == nullptr ? "-1" : request -> getParam("value", true)->value()).toFloat();

        if(power >= 0 && power <= 20.5){
          configDoc["power"] = power;
          
          msg = generateJSONWithConfig(true, "message", "OK");
          powerSet = true;
        }else{
          msg = generateJSON(false, "message", "POWER INVALID", true);
        }
      }
    }else if(setValue.equals("protocol")){
      if(request->hasParam("value", true)){
        byte protocol = (request -> getParam("value", true)->value() == nullptr ? "0" : request -> getParam("value", true)->value()).toInt();

        if(protocol == 1 || protocol == 2 || protocol == 3){
          configDoc["protocol"] = protocol;
          
          msg = generateJSONWithConfig(true, "message", "OK");
          protocolSet = true;
        }else{
          msg = generateJSON(false, "message", "PROTOCOL INVALID", true);
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

  String msg = generateJSON(false, "message", "PARAM INVALID", true);
  boolean save = false;

  if(request->hasParam("oldpass", true) && request->hasParam("newpass", true)){
    String oldpass = request -> getParam("oldpass", true)->value() == nullptr ? "" : request -> getParam("oldpass", true)->value();
    String newpass = request -> getParam("newpass", true)->value() == nullptr ? "" : request -> getParam("newpass", true)->value();
    
    if(oldpass.equals(configDoc["dbpass"].as<String>())){
      if(newpass.length() >= 5 && newpass.length() <= 32){
        configDoc["dbpass"] = newpass;

        save = true;
        msg = generateJSON(true, "message", "OK", true);
      }else{
        msg = generateJSON(false, "message", "NEW PASSWORD LENGTH INVALID", true);
      }
    }else{
      msg = generateJSON(false, "message", "OLD PASSWORD NOT MATCH", true);
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

    if(!keyParam.equals(String(key))){
      request -> send(404);
      return;
    }
  }

  StaticJsonDocument<1024> doc;
  String result = "";

  generateRandomString(128).toCharArray(key, 129);

  doc["success"] = true;
  doc["message"] = generateConfig();
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

    if(!keyParam.equals(String(key))){
      request -> send(404);
      return;
    }
  }

  StaticJsonDocument<1024> doc;
  String result = "";

  generateRandomString(128).toCharArray(key, 129);

  doc["success"] = true;
  doc["message"] = generateStats();
  doc["key"] = key;

  serializeJson(doc, result);
  
  request -> send(200, "application/json", result);
}

void getKeyHandler(AsyncWebServerRequest *request){
  if(!request->authenticate("admin", configDoc["dbpass"].as<const char*>())){
    return request->requestAuthentication();
  }
  
  request -> send(200, "application/json", key);
}

void getMainJS(AsyncWebServerRequest *request){
  if(!request->authenticate("admin", configDoc["dbpass"].as<const char*>())){
    return request->requestAuthentication();
  }
  
  request -> send(200, "application/json", mainJS);
}

void resetHandler(AsyncWebServerRequest *request){
  if(!request->hasParam("key", true)){
    request -> send(404);
    return;
  }else{
    String keyParam = request -> getParam("key", true)->value() == nullptr ? "" : request -> getParam("key", true)->value();

    if(!keyParam.equals(String(key))){
      request -> send(404);
      return;
    }
  }
  
  request -> send(200, "application/json", generateJSON(true, "message", "Resetting Config and restarting"));
  configResetTrigger = true;
}

void restartHandler(AsyncWebServerRequest *request){
  if(!request->hasParam("key", true)){
    request -> send(404);
    return;
  }else{
    String keyParam = request -> getParam("key", true)->value() == nullptr ? "" : request -> getParam("key", true)->value();

    if(!keyParam.equals(String(key))){
      request -> send(404);
      return;
    }
  }
  
  request -> send(200, "application/json", generateJSON(true, "message", "Restarting"));
  espRestartTrigger = true;
}

void notFound(AsyncWebServerRequest *request){
  if(request->url() == "/main.js.gzp"){
     request->send(404);
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
  WiFi.hostname("ESP8266");

  uint8_t currapmac[6];
  uint8_t currstamac[6];

  WiFi.softAPmacAddress(currapmac);
  WiFi.macAddress(currstamac);
  
  uint8_t apmac[] = {0x60, 0x01, 0x94, currapmac[3], currapmac[4], currapmac[5]};
  uint8_t stamac[] = {0x60, 0x01, 0x94, currstamac[3], currstamac[4], currstamac[5] + 1};
  
  wifi_set_macaddr(SOFTAP_IF, &apmac[0]);
  wifi_set_macaddr(STATION_IF, &stamac[0]);
  
  err_t initNAPT = ip_napt_init(napt, napt_port);
  
  if(initNAPT == ERR_OK){
    Serial.println("NAPT initialization with " + String(napt) + " NAT entries and " + String(napt_port) + " port entries success (Error : " + String(initNAPT) + ")");
    
    err_t enableNAPT = ip_napt_enable_no(SOFTAP_IF, true);
    
    if(enableNAPT == ERR_OK){
      Serial.println("NAPT enabling success (Error : " + String(enableNAPT) + ")");
      Serial.println("AP has been NATed behind station");
    }else{
      Serial.println("NAPT enabling failed (Error : " + String(enableNAPT) + ")");
    }
  }else{
    Serial.println("NAPT initialization failed (Error : " + String(initNAPT) + ")");
  }

  generateRandomString(128).toCharArray(key, 129);
  
  String mainjsurl = "/";
  mainjsurl.concat(generateRandomString(32));
  mainjsurl.toCharArray(mainJS, 34);

  server.on("/", HTTP_GET, indexHandler);
  server.on("/index", HTTP_GET, indexHandler);
  server.on("/index.html", HTTP_GET, indexHandler);
  server.on("/logout", HTTP_GET, logoutHandler);
  server.on("/logout.html", HTTP_GET, logoutHandler);
  server.on(mainJS, HTTP_GET, mainJSHandler);

  server.on("/setap", HTTP_POST, setAPHandler);
  server.on("/setsta", HTTP_POST, setStationHandler);
  server.on("/setdns", HTTP_POST, setDNSHandler);
  server.on("/setwlan", HTTP_POST, setWLANHandler);
  server.on("/setcredential", HTTP_POST, setCredentialHandler);

  server.on("/getconfig", HTTP_POST, getConfigHandler);
  server.on("/getstats", HTTP_POST, getStatsHandler);

  server.on("/getkey", HTTP_GET, getKeyHandler);
  server.on("/getmainjs", HTTP_GET, getMainJS);

  server.on("/reset", HTTP_POST, resetHandler);
  server.on("/restart", HTTP_POST, restartHandler);

  server.onNotFound(notFound);
  
  server.begin();

  Serial.println("Free Heap: " + String(ESP.getFreeHeap()));
  Serial.println("Heap Fragmentation: " + String(ESP.getHeapFragmentation()));
  Serial.println("Max Heap Block: " + String(ESP.getMaxFreeBlockSize()));

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

    String apSSID = configDoc["apssid"];
    String apPW = configDoc["appass"];

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

    if(configDoc["appass"].as<String>() == ""){
      WiFi.softAP(configDoc["apssid"].as<String>());
    }else{
      WiFi.softAP(configDoc["apssid"].as<String>(), configDoc["appass"].as<String>());
    }

    Serial.println("AP " + configDoc["apssid"].as<String>() + " (IP : " + WiFi.softAPIP().toString() + ") initialized");

    setAPTrigger = false;
  }

  if(setStaTrigger){
    WiFi.disconnect();

    delay(100);

    if(configDoc["stassid"].as<String>() == ""){
      Serial.println("WLAN NO STATION");
      noStation = true;
    }else{
      if(configDoc["staIP"] != nullptr && configDoc["staGW"] != nullptr && configDoc["staSN"] != nullptr){
        WiFi.config(ntoip(configDoc["staIP"].as<unsigned long>()), ntoip(configDoc["staGW"].as<unsigned long>()), ntoip(configDoc["staSN"].as<unsigned long>()));
      }

      if(configDoc["stapass"].as<String>() == ""){
        WiFi.begin(configDoc["stassid"].as<String>());
      }else{
        WiFi.begin(configDoc["stassid"].as<String>(), configDoc["stapass"].as<String>());
      }

      Serial.println("Station set to " + configDoc["stassid"].as<String>());

      noStation = false;
      firstConnect = true;
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

      Serial.println("DNS set custom 1 : " + ntoip(configDoc["dns1"].as<unsigned long>()).toString() + " 2 : " + ntoip(configDoc["dns2"].as<unsigned long>()).toString());

      setDNSTrigger = false;
    }
  }

  if(setPowerTrigger){
    float power = configDoc["power"].as<float>();
    WiFi.setOutputPower(power);

    Serial.println("WiFi Tx power set to " + String(power));

    setPowerTrigger = false;
  }

  if(setProtocolTrigger){
    WiFiPhyMode_t protocol = (WiFiPhyMode_t) configDoc["protocol"].as<byte>();

    String wlanPhyMode = "";

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

    Serial.println("WiFi Protocol set to " + wlanPhyMode);

    setProtocolTrigger = false;
  }

  blinkLedLoop();
  statsLoop();
  uptimeLoop();

  if((firstConnect || reconnectFlag) && WiFi.isConnected()){
    if(firstConnect){
       Serial.println("Connected to " + WiFi.SSID() + " (IP : " + WiFi.localIP().toString() + ")");
       firstConnect = false;
       setDNSTrigger = true;
    }
    
    if(reconnectFlag){
       Serial.println("Reconnected to " + WiFi.SSID() + " (IP : " + WiFi.localIP().toString() + ")");
       reconnectFlag = false;
       setDNSTrigger = true;
    }
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
