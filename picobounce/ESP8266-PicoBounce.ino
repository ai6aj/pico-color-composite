#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

ESP8266WebServer server(80);

#define LED_PIN LED_BUILTIN

#define PICO_RUN_PIN D1
#define PICO_BOOT_SEL_PIN D2

const char* ssid = "your-ssid"; //Enter Wi-Fi SSID
const char* password =  "your-passwd"; //Enter Wi-Fi Password

long last_led_on_time;

void setup() {
  pinMode(PICO_RUN_PIN, INPUT);  
  pinMode(PICO_BOOT_SEL_PIN, INPUT);
  Serial.begin(115200); //Begin Serial at 115200 Baud
  wifi_station_set_hostname("PICOBOUNCE");
  WiFi.hostname("PICOBOUNCE");
  WiFi.begin(ssid, password);  //Connect to the WiFi network

  pinMode(LED_PIN,OUTPUT);
  bool led_value = true;
  
  while (WiFi.status() != WL_CONNECTED) {  //Wait for connection
      led_value = !led_value;
      digitalWrite(LED_PIN,led_value);
      delay(250);
      Serial.println("Waiting to connect...");
  }

  
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());  //Print the local IP
  
  server.on("/", handle_index); //Handle Index page
  server.on("/reboot", handle_reboot); //Handle Index page
  server.on("/bootsel", handle_bootsel); //Handle Index page
  
  server.begin(); //Start the server
  Serial.println("Server listening");
  last_led_on_time = millis();
}

void loop() {
  long led_blink_delta = millis()-last_led_on_time;
  if (led_blink_delta > 5000) {
    last_led_on_time = millis();
    digitalWrite(LED_PIN,0);
  } else if (led_blink_delta > 10) {
    digitalWrite(LED_PIN,1);
  }
  
  server.handleClient(); //Handling of incoming client requests
}

void handle_index() {
  //Print Hello at opening homepage
  server.send(200, "text/plain", "PicoBounce");
}


void handle_reboot() {
  //Print Hello at opening homepage
  pinMode(PICO_RUN_PIN, OUTPUT);
  pinMode(PICO_BOOT_SEL_PIN, INPUT);
  digitalWrite(PICO_RUN_PIN, 0);
  delay(100);
  pinMode(PICO_RUN_PIN, INPUT);  
  pinMode(PICO_BOOT_SEL_PIN, INPUT);
  server.send(200, "text/plain", "Reboot complete.");
}


void handle_bootsel() {
  pinMode(PICO_RUN_PIN, OUTPUT);
  pinMode(PICO_BOOT_SEL_PIN, OUTPUT);
 
  digitalWrite(PICO_RUN_PIN, 0);
  digitalWrite(PICO_BOOT_SEL_PIN, 0);
  delay(100);
  pinMode(PICO_RUN_PIN, INPUT);  
  delay(100);
  pinMode(PICO_BOOT_SEL_PIN, INPUT);  
  server.send(200, "text/plain", "BOOTSEL reboot complete.");
}
