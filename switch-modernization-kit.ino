#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <Ticker.h>
#include <Servo.h>
#include "DHT.h"
// https://github.com/adafruit/DHT-sensor-library

extern "C" {
  // ADC read
  #include "user_interface.h"
}

// Pin assign
#define BTN1_PIN  12
#define BTN2_PIN  14
#define RSW1_PIN  5
#define RSW2_PIN  4
#define LED1_PIN  16
#define SRV1_PIN  13
#define SRV2_PIN  15
#define DHT1_PIN  0
#define DHT2_PIN  2

// Servo angle
#define ON    125
#define DEFO  100
#define OFF   65

// Network
const char* ssid = "YOUR_SSID"; // 2.4GHz ONLY
const char* password = "****************";
const char* influxdb_addr = "http://192.168.0.255:8086";
const char* domain = "bath";

// Wall switch device name (InfluxDB will use it)
const char* device1 = "fan";
const char* device2 = "light";

// Location id (InfluxDB will use it)
int location = 1;         // Sensor and switch device location
int location_sub = 1;     // Sub sensor location

// Initialize global variable
int now_brightness = 0;       // LED brightness
int door_parcent = 100;       // Door open parcent
bool now_switch1 = false;     // Wall switch1 state
bool now_switch2 = false;     // Wall switch2 state
bool ticker_flag = false;     // Every minute
String version_n = "20181110";// Version of SMK01.ino

// Initialize global instance
MDNSResponder mdns;
ESP8266WebServer server(80);
Ticker ticker;
Servo servo1;
Servo servo2;
DHT dht1(DHT1_PIN, DHT22);
DHT dht2(DHT2_PIN, DHT22);

/////////////////////////////////////////
//             Network

void wifi_connect_wait() {
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("  SSID: "); Serial.println(ssid);
  Serial.print("  IP:   "); Serial.println(WiFi.localIP());
}

void influx_post(String payload, bool mes_flg) {
  if (payload == "") {
    Serial.println("[ERROR] InfluxDB payload empty.");
    return;
  } else {
    if (mes_flg == true){ Serial.print(payload); }
  }

  HTTPClient http;
  http.begin(String(influxdb_addr) + "/write?db=home-sensor");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int http_code;
  for (int i = 0; i < 5; i++) {
    http_code = http.POST(payload);
    if (http_code == 204) {
      if (mes_flg == true){ Serial.printf("[ O K ] InfluxDB send!\n\n"); }
      break;
    } else {
      Serial.printf("[ERROR] Sending failure! %02dtimes.\n[ERROR] HTTP Status Code: %d\n", i, http_code); delay(1000);
    }
  }

  http.end();
}

void boot_log() {
  String payload = "iotboot,domain=" + String(domain) + ".local,ip=" + WiFi.localIP().toString() + ",location=" + String(location) + " value=1"+ "\n";
  influx_post(payload, false);
}

void set_switch_state(){
  receive_switch_state(1);
  receive_switch_state(2);
}

void receive_switch_state(int switch_n) {
  // Create query
  String device_name = (switch_n == 1) ? device1 : device2;
  String query = "SELECT%20value%20FROM%20swaction%20WHERE%20location=%27"+String(location)+"%27%20and%20device=%27"+String(device_name)+"%27%20order%20by%20desc%20limit%201";
  String url = String(influxdb_addr) + "/query?db=home-sensor&q=" + query;
  
  // Influx
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String result = "";
  int http_code;
  for (int i = 0; i < 5; i++) {
    http_code = http.GET();
    if (http_code > 1) {
      result = http.getString();
      break;
    } else {
       Serial.printf("[ERROR] Sending failure! %02dtimes.\n[ERROR] HTTP Status Code: %d\n", i, http_code); delay(1000);
    }
  }
  http.end();

  // Set switch state
  if (result.indexOf("\"on\"") > 0) {
    //Serial.println("on");
    (switch_n == 1) ? now_switch1 = true : now_switch2 = true;
  }else if (result.indexOf("\"off\"")) {
    //Serial.println("off");
    (switch_n == 1) ? now_switch1 = false : now_switch2 = false;
  }else{
    Serial.println("[ERROR] Receive failure! SW state.");
  }
  
}

void handle_root() {
  String s = "<!DOCTYPE html><html><head><meta charset=\"utf-8\">";
  s += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  s += "<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.11.2/jquery.min.js\"></script>";
  s += "<link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.5/css/bootstrap.min.css\">";
  s += "<script src=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.5/js/bootstrap.min.js\"></script>";
  s += "<script>function rest(device,cmd,method){var http = new XMLHttpRequest();var url = \"/api/v1/\"+device;http.open(method, url, true);http.send(\"c=\"+cmd);http.onreadystatechange=function(){if(http.readyState == 4 && http.status == 200){alert(http.responseText);}}}</script>";
  s += "</head><body>";
  s += "<div class=\"container\"><div class=\"row\"><div class=\"col-md-12 text-center\">";
  s += "<h1 class=\"page-header\">SMK01</h1>";
  s += "<h2>Light</h2>";
  s += "<div class=\"btn-group\" role=\"group\" aria-label=\"rear\">";
  s += "<button type=\"button\" class=\"btn btn-primary btn-lg\" onclick=\"rest('switch/2','on','POST');\">ON</button>";
  s += "<button type=\"button\" class=\"btn btn-primary btn-lg\" onclick=\"rest('switch/2','off','POST');\">OFF</button>";
  s += "</div><br>&nbsp;<br>";
  s += "<hr>";
  s += "<h2>Fan</h2>";
  s += "<div class=\"btn-group\" role=\"group\" aria-label=\"flow\">";
  s += "<button type=\"button\" class=\"btn btn-info btn-lg\" onclick=\"rest('switch/1','on','POST');\">ON</button>";
  s += "<button type=\"button\" class=\"btn btn-info btn-lg\" onclick=\"rest('switch/1','off','POST');\">OFF</button>";
  s += "</div>";
  s += "</div></div></div>";
  s += "<footer style=\"position:fixed;bottom:0;width:100%;text-align: center\">version: " + version_n + "</footer>";
  s += "</body></html>";
  server.send(200, "text/html", s);
}

void handle_404() {
  String message = "404 File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void handle_switch1() {
  handle_switch(1);
}

void handle_switch2() {
  handle_switch(2);
}

void handle_switch(int switch_n) {
  String s = "";
  if (server.method() == HTTP_POST) {
    if (server.arg("c") == "on") {
      Serial.println("Browser ON button pushed!");
      s += "switch " + String(switch_n) + " on";
      wall_switch(switch_n, true);
    } else if (server.arg("c") == "off") {
      Serial.println("Browser OFF button pushed!");
      s += "switch " + String(switch_n) + " off";
      wall_switch(switch_n, false);
    } else {
      s += "error";
    }
  } else {
    s += "error";
  }
  server.send(200, "text/plain", s);
}

void handle_information() {
  // Generate uptime
  unsigned long uptime_ms = millis();
  unsigned long uptime_d = (uptime_ms/87840000);
  unsigned long uptime_h = (uptime_ms/3660000) - (uptime_d * 24);
  unsigned long uptime_m = (uptime_ms/61000) - (uptime_d * 24 * 60) - (uptime_h * 60);
  String  d_s = (uptime_d > 1) ? "days" : "day";
  String  h_s = (uptime_h > 1) ? "hours" : "hour";
  String  m_s = (uptime_m > 1) ? "minutes" : "minute";

  String uptime;
  if (uptime_d > 0) {
    uptime = String(uptime_d) + d_s + " " + String(uptime_h) + h_s + " " + String(uptime_m) + m_s;
  } else if (uptime_h > 0) {
    uptime = String(uptime_h) + h_s + " " + String(uptime_m) + m_s;
  } else {
    uptime = String(uptime_m) + m_s;
  }

  String s = "<!DOCTYPE html><html><head><meta charset=\"utf-8\">";
  s += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  s += "<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/1.11.2/jquery.min.js\"></script>";
  s += "<link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.5/css/bootstrap.min.css\">";
  s += "<script src=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.5/js/bootstrap.min.js\"></script>";
  s += "</head><body>";
  s += "<div class=\"container\"><div class=\"row\"><div class=\"col-md-12 text-center\">";
  s += "<h1 class=\"page-header\">Switch Modernization Kit 01</h1>";
  s += "<h2>Config</h2>";
  s += "<lu style=\"list-style-type: none\">";
  s += "<li>location1 ID: " + String(location) + "</li>";
  s += "<li>location2 ID: " + String(location_sub) + "</li>";
  s += "<li>device1:      " + String(device1) + "</li>";
  s += "<li>device2:      " + String(device2) + "</li></lu>";
  s += "<h2>Network info</h2>";
  s += "<lu style=\"list-style-type: none\">";
  s += "<li>IP address: " + WiFi.localIP().toString() + "</li>";
  s += "<li>MDNS:       " + String(domain) + ".local</li>";
  s += "<li>SSID:       " + String(ssid) + "</li></ul>";
  s += "<h2>Status</h2>";
  s += "<lu style=\"list-style-type: none\">";
  s += "<li>SW1 state: " + String(now_switch1) + "</li>";
  s += "<li>SW2 state: " + String(now_switch2) + "</li>";
  s += "<li>Door state:" + String(door_parcent) + "</li>";
  s += "<li>Uptime:    " + uptime + "</li></ul>";
  s += "</div></div></div>";
  s += "<footer style=\"position:fixed;bottom:0;width:100%;text-align: center\">version:" + version_n + "</footer>";
  s += "</body></html>";
  server.send(200, "text/html", s);
}

/////////////////////////////////////////
//             Ticker

void ticker_flag_up() {
  ticker_flag = true;
}

void ticker_flag_down() {
  ticker_flag = false;
}

/////////////////////////////////////////
//             Setup

void setup() {
  // Initialize Serial
  Serial.begin(115200);
  delay(50);
  Serial.print("\n\n\n");
  Serial.println("Serial connect.          [ O K ]");

  // Initialize PIN
  pinMode(RSW1_PIN, INPUT_PULLUP);
  pinMode(RSW2_PIN, INPUT_PULLUP);
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(LED1_PIN, OUTPUT);
  analogWriteRange(255);
  Serial.println("PIN mode initialize.     [ O K ]");

  // Initialize DHT
  dht1.begin();
  dht2.begin();
  Serial.println("AM2320 start.            [ O K ]");

  // Initialize ticker
  ticker.attach(60, ticker_flag_up);
  Serial.println("Ticker start.            [ O K ]");

  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  wifi_connect_wait();

  // Initialize MDNS
  if (mdns.begin(domain, WiFi.localIP())) {
    Serial.printf("  MDNS: %s.local\n", domain);
  }
  Serial.println("                         [ O K ]");

  // Setup WebServer Handlers
  server.on("/", handle_root);
  server.on("/api/v1/switch/1", handle_switch1);
  server.on("/api/v1/switch/2", handle_switch2);
  server.on("/information", handle_information);
  server.onNotFound(handle_404);

  // Initialize Server
  server.begin();
  Serial.println("Server start.            [ O K ]");

  // Send boot log
  boot_log();
  Serial.println("Boot log send.           [ O K ]");

  // Set switch state
  set_switch_state();
  Serial.printf("SW state receive.(%d,%d)   [ O K ]\n",now_switch1, now_switch2);
  (now_switch2) ? led(0) : led(225);  // LED initialize
  
  Serial.print("=========== v" + version_n + " ===========\n");
  Serial.print("======== Setup complete! ========\n\n\n");
}

/////////////////////////////////////////
//              Loop

void loop() {
  // Always
  server.handleClient();
  door_scan();
  button_scan();

  // Evrey minute
  if (ticker_flag) {
    am2320();
    illuminance();
    ticker_flag_down();
  }

}

/////////////////////////////////////////
//             Sensor

void illuminance() {
  uint adc_value;
  adc_value = system_adc_read();
  Serial.print("illuminance: "); Serial.println(adc_value);

  // Influxdb
  String payload = "illuminance,location=" + String(location) + " value=" + adc_value + "\n";
  influx_post(payload, true);
}

void am2320() {
  float t1, h1, t2, h2;
  int i = 1;
  do {
    // Retry wait 
    if (i > 1) {Serial.printf("Retry %dtimes.\n", i); delay(500);}

    // Read am2320
    t1 = dht1.readTemperature();
    h1 = dht1.readHumidity();
    t2 = dht2.readTemperature();
    h2 = dht2.readHumidity();

    Serial.printf("Main: ");Serial.print(t1);Serial.print(" ");Serial.println(h1);
    Serial.printf("Sub : ");Serial.print(t2);Serial.print(" ");Serial.println(h2);

    // Give up
    if (i > 9) {
      Serial.printf("[ERROR] Read AM2320 failure.\n\n");
      return;
    }
    i++;
          
  } while (isnan(t1) || isnan(t2) || isnan(h1) || isnan(h2));

  // Influxdb Data
  String payload = "";
  payload += "temperature,location=" + String(location) + " value=" + String(t1) + "\n";
  payload += "humidity,location=" + String(location) + " value=" + String(h1) + "\n";
  payload += "temperature,location=" + String(location_sub) + " value=" + String(t2) + "\n";
  payload += "humidity,location=" + String(location_sub) + " value=" + String(h2) + "\n";
  influx_post(payload, true);
}

/////////////////////////////////////////
//             IO scan

void door_scan() {
  int now_percent = -1;
  int past_percent = door_parcent;
  bool door_close = !digitalRead(RSW1_PIN);
  bool door_open = !digitalRead(RSW2_PIN);

  if (door_open && door_close) {
    Serial.println("[ERROR] Door sensor is malfunction!");
    // Prevent chattering
    delay(1000);
    return;
  } else if (!door_open && !door_close) {
    now_percent = 50;
  } else if (door_open) {
    now_percent = 100;
  } else if (door_close) {
    now_percent  = 0;
  }

  if (past_percent != now_percent) {
    Serial.printf("door open: %d%%\n", now_percent);

    // Influxdb
    String payload = "ocaction,location=" + String(location) + " value=" + String(now_percent) + "\n";
    influx_post(payload, true);

    // Prevent chattering
    delay(1000);
  }
  door_parcent = now_percent;
}

bool key_scan(bool past) {
  bool now = !digitalRead(RSW1_PIN);
  if (past != now) {
    if (now) {
      Serial.println("LOCK");
    }
    if (!now) {
      Serial.println("UNLOCK");
    }

    // Influxdb
    String payload = "keyaction,location=" + String(location) + " value=" + String(now) + "\n";
    influx_post(payload, true);
  }
  past = now;

  return past;
}

void button_scan() {

  if (!digitalRead(BTN1_PIN)) {
    Serial.println("Button1 pushed!");

    // Reverse switch state
    if (now_switch1) {
      wall_switch(1, false);
    } else {
      wall_switch(1, true);
    }
  }
  if (!digitalRead(BTN2_PIN)) {
    Serial.println("Button2 pushed!");

    // Reverse switch state
    if (now_switch2) {
      wall_switch(2, false);
    } else {
      wall_switch(2, true);
    }
  }
}

/////////////////////////////////////////
//             LED

void led(int target_brightness) {

  // Not need change
  if (now_brightness == target_brightness) {
    return;
  }

  int fadeAmount = 5;

  // Select increment or decrement
  if (now_brightness > target_brightness) {
    fadeAmount = -fadeAmount ;
  }

  // PWM loop
  while (1) {
    analogWrite(LED1_PIN, now_brightness);

    // Increment or decrement
    now_brightness = now_brightness + fadeAmount;

    // Done
    if (now_brightness == target_brightness) {
      break;
    }
    delay(10);
  }

  // Update now brightness
  now_brightness = target_brightness;
}

void led_on() {
  led(255);
  Serial.println("LED on");
}

void led_off() {
  led(0);
  Serial.println("LED off");
}

/////////////////////////////////////////
//           Wall Switch

void wall_switch(int switch_n, bool switch_a) {
  // LED
  if (switch_n == 2) {
    (switch_a) ? led_off() : led_on();
  }

  // Wall switch action
  (switch_a) ? servo(switch_n, true) : servo(switch_n, false);
  
  // Change switch state
  (switch_n == 1 ) ? now_switch1 = !now_switch1 : now_switch2 = !now_switch2;
  
  // Craete string
  String device = (switch_n == 1) ? device1 : device2;
  String action = (switch_a) ? "on" : "off";
  Serial.println(device + " switch " + action);

  // Influxdb
  String payload = "swaction,location=" + String(location) + ",device=" + device + " value=\"" + action + "\"\n";
  influx_post(payload, true);
}

/////////////////////////////////////////
//             Servo

void servo_test() {
  servo(1, true);
  delay(1000);
  servo(1, false);

  servo(2, true);
  delay(1000);
  servo(2, false);
}

void servo(int servo_n, bool servo_a) {
  // Attach servo
  (servo_n == 1) ? servo1.attach(SRV1_PIN) : servo2.attach(SRV2_PIN);

  // Select increment or decrement
  int now_angle = DEFO;
  int fede_amount = 1;
  fede_amount = (servo_a) ? fede_amount : -fede_amount;

  // Select target angle
  int target_angle = (servo_a) ? ON : OFF;

  // Move servo
  while (1) {
    (servo_n == 1) ? servo1.write(now_angle) : servo2.write(now_angle);
    delay(30);

    // Increment or decrement
    now_angle = now_angle + fede_amount;

    // Done
    if (target_angle == now_angle) {
      break;
    }
  }
  delay(250);

  // Default angle
  target_angle = DEFO;
  while (1) {
    (servo_n == 1) ? servo1.write(now_angle) : servo2.write(now_angle);
    delay(30);

    // Increment or decrement
    now_angle = now_angle - fede_amount;

    // Done
    if (target_angle == now_angle) {
      break;
    }
  }

  delay(250);

  // Detach
  (servo_n == 1) ? servo1.detach() : servo2.detach();
}

