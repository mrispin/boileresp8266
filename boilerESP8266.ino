#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#define BOILER_PIN 5 //D1 on NodeMCU board

const char* ssid     = "";
const char* password = "";
const char* mqtt_server = "";

// vars for metrics
int mqtt_msgs_received=0;
int mqtt_reconnects=0;
int wifi_reconnects=0;
int http_requests=0;

WiFiClient espClient;
PubSubClient client(espClient);
ESP8266WebServer server(80);

// defs for http uri handlers
void httpRoot();
void httpHealth();
void httpMetrics();
void httpNotFound();

void WiFiEvent(WiFiEvent_t event) {
    //Serial.printf("[WiFi-event] event: %d\n", event);

    switch(event) {
        case WIFI_EVENT_STAMODE_GOT_IP:
            Serial.println("WiFi connected");
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());
            break;
        case WIFI_EVENT_STAMODE_DISCONNECTED:
            Serial.println("WiFi lost connection");
            wifi_reconnects++;
            break;
    }
}

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    pinMode(BOILER_PIN, OUTPUT);

    // delete old config
//why?    WiFi.disconnect(true);

//    delay(1000);

    Serial.print("Connecting to wifi");
    WiFi.onEvent(WiFiEvent);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      Serial.print(".");
    }
    
    //Hook up mqtt connection
    Serial.println("Connecting to MQTT server.");
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);

    if (MDNS.begin("boiler")) {     // Start the mDNS responder for boiler.local
      Serial.println("mDNS responder started");
    } else {
      Serial.println("Error setting up mDNS responder!");
    }
    
    // Initialize web server
    server.on("/", httpRoot);
    server.on("/health", httpHealth);
    server.on("/metrics", httpMetrics);
    server.onNotFound(httpNotFound);
    
    server.begin();
    Serial.println("HTTP server started");
    
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  mqtt_msgs_received++;

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    digitalWrite(BOILER_PIN, HIGH);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is acive low on the ESP-01)
  } else {
    digitalWrite(BOILER_PIN, LOW);  // Turn the LED off by making the voltage HIGH
  }
}


void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    mqtt_reconnects++;
    if (client.connect("boilerESP8266", "chpi", "")) {
      Serial.println("connected");
      // ... and resubscribe
      client.subscribe("control/boiler");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  server.handleClient();
}

void httpRoot() {
  http_requests++;
  if (digitalRead(BOILER_PIN)==1){
    server.send(200, "text/plain", "Boiler is ON");
  } else {
    server.send(200, "text/plain", "Boiler is OFF");
  }
}

void httpHealth() {
  http_requests++;
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
}

void httpMetrics() {
  http_requests++;
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;
  
  char metrics[200];
  
  snprintf( metrics, 200, "\
uptime=%02d:%02d:%02d\n\
mqtt_messages_received=%d\n\
mqtt_reconnects=%d\n\
wifi_reconnects=%d\n\
http_requests=%d\n\
"
    ,
    hr, min % 60, sec % 60,
    mqtt_msgs_received,
    mqtt_reconnects,
    wifi_reconnects,
    http_requests
  );
  
  server.send(200, "text/plain", metrics);
}
void httpNotFound() {
  http_requests++;
  server.send(404, "text/plain", "NotFound");
}

