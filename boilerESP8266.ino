#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <PolledTimeout.h>
#include <DHT.h>
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

// vars for metrics
int wifi_disconnects=0;
int http_requests=0;
int http_posts=0;
int http_gets=0;
int http_404s=0;

//temp
DHT dht(DHT11_PIN, DHTTYPE);

WiFiClient espClient;
ESP8266WebServer server(80);

float temp=0.0;
float humidity=0.0;

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
            Serial.println();
            Serial.println("WiFi connected");
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());
            break;
        case WIFI_EVENT_STAMODE_DISCONNECTED:
            Serial.println();
            Serial.println("WiFi lost connection");
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
    Serial.println();
    Serial.print("Connecting to wifi");
    WiFi.onEvent(WiFiEvent);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
      digitalWrite(LED_ESP, HIGH);
      delay(250);
      Serial.print(".");
      digitalWrite(LED_ESP, LOW);
      delay(250);
    }

    //mDNS setup
    if (MDNS.begin("boiler")) {     // Start the mDNS responder for boiler.local
      Serial.println("mDNS responder started");
    } else {
      Serial.println("Error setting up mDNS responder!");
    }
    
    // Initialize web server
    server.on("/", httpRoot);
    server.on("/health", httpHealth);
    server.on("/metrics", httpMetrics);
    server.on("/temp", httpTemp);
    server.onNotFound(httpNotFound);
    
    server.begin();
    Serial.println("HTTP server started");

    //Advertise http
    MDNS.addService("http", "tcp", 80);

    //start temp
    dht.begin();

    //Turn off setup LEDs
    digitalWrite(LED_MCU, HIGH);
    digitalWrite(LED_ESP, HIGH);
}

/*  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    digitalWrite(BOILER_PIN, HIGH);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is acive low on the ESP-01)
  } else {
    digitalWrite(BOILER_PIN, LOW);  // Turn the LED off by making the voltage HIGH
  }
*/

void loop() {
  server.handleClient();
  MDNS.update();
  
  static esp8266::polledTimeout::periodicMs timeout(UPDATE_CYCLE);
  if (timeout.expired()) {
    digitalWrite(LED_MCU, LOW);
    MDNS.announce();
    temp=0.0;
    humidity=0.0;
    temp = dht.readTemperature();
    humidity = dht.readHumidity();
    Serial.print(F("Temperature: "));
    Serial.print(temp);
    Serial.print(F("°C  Humidity: "));
    Serial.print(humidity);
    Serial.println(F("%"));
    digitalWrite(LED_MCU, HIGH);
  }
}

void printHttpClientRequest() {
  Serial.print("Received ");
  switch (server.method()) {
    case HTTP_GET:
      Serial.print("GET");
      break;
    case HTTP_POST:
      Serial.print("POST");
      break;
  }
  Serial.print(" request from ");
  Serial.println(server.client().remoteIP().toString());
  
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
