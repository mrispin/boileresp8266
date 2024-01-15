#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <PolledTimeout.h>
#include <DHT.h>
// NTP Requirements start
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
// NTP Requirements end
#include "secrets.h"

#define BAUDRATE 9600

#define UPDATE_CYCLE        (10 * 1000)      // every 10 seconds

#define BOILER_PIN 5 //D1 on NodeMCU board

#define DHTTYPE DHT11
#define DHT11_PIN 4

#define LED_ESP 2
#define LED_MCU 16

const char* ssid     = SECRET_SSID;
const char* password = SECRET_WIFIPASS;
const char* host_name = SECRET_HOSTNAME;

// vars for metrics
int wifi_disconnects=0;
int http_requests=0;
int http_posts=0;
int http_gets=0;
int http_404s=0;

//temp
DHT dht(DHT11_PIN, DHTTYPE);

// network services
WiFiClient espClient;
ESP8266WebServer server(80);

//ntp
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// temp sensor
float temp=0.0;
float humidity=0.0;

// boiler control
int boilerOnSeconds=0;


//logging
void log(const String message, const String level = "INFO");
void printTimestamp();

// defs for http uri handlers
void httpRoot();
void httpHealth();
void httpMetrics();
void httpNotFound();
void httpTemp();

void WiFiEvent(WiFiEvent_t event) {
    //Serial.printf("[WiFi-event] event: %d\n", event);

    switch(event) {
        case WIFI_EVENT_STAMODE_GOT_IP:
            log("Wifi connected.");
            log(WiFi.localIP().toString());
            break;
        case WIFI_EVENT_STAMODE_DISCONNECTED:
            log("WiFi lost connection", "WARN");
            wifi_disconnects++;
            break;
    }
}

void setup() {
    //LEDS on for setup
    pinMode(LED_MCU, OUTPUT);
    pinMode(LED_ESP, OUTPUT);
    digitalWrite(LED_ESP, LOW);
    digitalWrite(LED_MCU, LOW);

    //Boiler control relay
    pinMode(BOILER_PIN, OUTPUT);
    digitalWrite(BOILER_PIN,LOW);

    //Start serial output
    Serial.begin(BAUDRATE);

    //Wifi setup
    WiFi.mode(WIFI_STA);
    log("Connecting to wifi");
    WiFi.onEvent(WiFiEvent);
    WiFi.hostname(host_name);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
      digitalWrite(LED_ESP, HIGH);
      delay(250);
      digitalWrite(LED_ESP, LOW);
      delay(250);
    }

    //start ntp
    log("Starting NTP Client");
    timeClient.begin();
    while(!timeClient.isTimeSet()) {
      timeClient.update();
      digitalWrite(LED_ESP, HIGH);
      delay(500);
      digitalWrite(LED_ESP, LOW);
      delay(500);      
    }

    //mDNS setup
    log("Starting mDNS server");
    MDNS.begin("boiler");  // Start the mDNS responder for boiler.local

    
    // Initialize web server
    log("Starting web server");
    server.on("/", httpRoot);
    server.on("/health", httpHealth);
    server.on("/metrics", httpMetrics);
    server.on("/temp", httpTemp);
    server.onNotFound(httpNotFound);
    
    server.begin();

    //Advertise http
    MDNS.addService("http", "tcp", 80);

    //start temp
    log("Starting temperature monitor");
    dht.begin();

    
    //Turn off setup LEDs
    digitalWrite(LED_MCU, HIGH);
    digitalWrite(LED_ESP, HIGH);
}


void loop() {
  server.handleClient();
  MDNS.update();
  timeClient.update();
  
  static esp8266::polledTimeout::periodicMs timeout(UPDATE_CYCLE);
  if (timeout.expired()) {
    digitalWrite(LED_MCU, LOW);
    MDNS.announce();
    temp=0.0;
    humidity=0.0;
    temp = dht.readTemperature();
    humidity = dht.readHumidity();
    char msg[100];
    snprintf(msg,100,"Temperature: %.1f°C  Humidity: %.1f%%",
      temp,
      humidity);
    log(msg);
    digitalWrite(LED_MCU, HIGH);
  }
}

void printTimestamp() {
  setTime(static_cast<time_t>(timeClient.getEpochTime()));
  char timestamp[20];
  snprintf( timestamp, 20, "%04d-%02d-%02d %02d:%02d:%02d",
    year(),
    month(),
    day(),
    hour(),
    minute(),
    second()
  );
  Serial.print(timestamp);
}
void log(const String message, const String level) {
  printTimestamp();
  Serial.print(F(" - "));
  Serial.print(level);
  Serial.print(F(" - "));
  Serial.println(message);
  
}

void printHttpClientRequest() {
  String request_type;
  switch (server.method()) {
    case HTTP_GET:
      request_type = "GET";
      break;
    case HTTP_POST:
      request_type = "POST";
      break;
  }
  char clientIP[16];
  server.client().remoteIP().toString().toCharArray(clientIP,16);
  char msg[100];
  snprintf(msg, 100, "Received %s request from %s", request_type, clientIP);
  log(msg);
}

void httpRoot() {
  digitalWrite(LED_ESP, LOW);
  http_requests++;
  printHttpClientRequest();

  /*  
   *  If the request is a POST request:
   *  If the post content is "ON":
   *  turn on the relay
   *  else if the content is "OFF":
   *  turn off the relay
   *  else return an error  
   */
  if (server.method() == HTTP_POST) {
    http_posts++;
    if (server.arg("plain") == "ON") {
      digitalWrite(BOILER_PIN, 1);
      server.send(200, "text/plain", "ON\n");
    } else if (server.arg("plain") == "OFF") {
      digitalWrite(BOILER_PIN, 0);
      server.send(200, "text/plain", "OFF\n");
    } else {
      server.send(400, "text/plain", "Only accepts ON or OFF\n");
    }

  /*
   *  Otherwise it's a GET request:
   *  return the relay status
   */
  } else {
    http_gets++;
    if (digitalRead(BOILER_PIN)==1){
      server.send(200, "text/plain", "ON\n");
    } else {
      server.send(200, "text/plain", "OFF\n");
    }
  }
  digitalWrite(LED_ESP, HIGH);
}

void httpHealth() {
  digitalWrite(LED_ESP, LOW);
  http_requests++;
  http_gets++;
  printHttpClientRequest();
  
  char health[100];

  snprintf( health, 100, "\
SSID=%s\n\
RSSI=%d dBm\n\
"
    ,
    WiFi.SSID().c_str(),
    WiFi.RSSI()
  );
  server.send(200, "text/plain", health);
  digitalWrite(LED_ESP, HIGH);
}

void httpMetrics() {
  digitalWrite(LED_ESP, LOW);
  http_requests++;
  http_gets++;
  printHttpClientRequest();
  
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;
  
  char metrics[200];
  
  snprintf( metrics, 200, "\
uptime=%02d:%02d:%02d\n\
wifi_disconnects=%d\n\
http_requests=%d\n\
http_gets=%d\n\
http_posts=%d\n\
http_404s=%d\n\
"
    ,
    hr, min % 60, sec % 60,
    wifi_disconnects,
    http_requests,
    http_gets,
    http_posts,
    http_404s
  );
  
  server.send(200, "text/plain", metrics);
  digitalWrite(LED_ESP, HIGH);
}

void httpNotFound() {
  digitalWrite(LED_ESP, LOW);
  http_requests++;
  http_404s++;
  printHttpClientRequest();
  
  server.send(404, "text/plain", "NotFound");
  digitalWrite(LED_ESP, HIGH);
}

void httpTemp() {
  digitalWrite(LED_ESP, LOW);
  http_requests++;
  http_gets++;
  printHttpClientRequest();
  
  char tempchars[50];
  
  snprintf( tempchars, 50, "\
temperature=%d\n\
humidity=%d\n\
"
    ,
    int(temp),
    int(humidity)
  );

  server.send(200, "text/plain", tempchars);
  
  digitalWrite(LED_ESP, HIGH);
}
