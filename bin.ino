#include <WiFiEsp.h>
#include <WiFiEspClient.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <HX711.h>
#include <SoftwareSerial.h>

#define HALLPIN 2
#define ECHOPIN 8
#define TRIGPIN 9
#define BUZZERPIN 10
#define AP_SSID "UFC"
#define AP_PASSWORD ""
#define SERVER "mqtt.thingspeak.com"
#define PORT 1883
#define BIN_ID "507f191e810c19729de860ea"
#define TOPIC "channels/565960/publish/LIFQ82NBPEUZIZCC" //bin_id
#define UPDATE_INTERVAL 60000

typedef struct package {
  float humidity;
  float temperature;
  float fill;
  float volume;
  float weight;
} Package;

typedef struct wasteBin {
  float topWidth;
  float topLength;
  float bottomWidth;
  float bottomLength;
  float height;
} WasteBin;

WasteBin bin = {38, 30, 30, 20, 64.4};

Package data;
SoftwareSerial esp8266Serial(3, 4);
DHT dht(7, DHT22);
WiFiEspClient espClient;
PubSubClient client(espClient);
HX711 scale(5, 6);
int status = WL_IDLE_STATUS;
boolean isLidOpened = false;
boolean wasLidOpened = false;
unsigned long timeStart;
unsigned long timeEnd;
unsigned long timeOpened;
unsigned long previousMillis = 60000;

void setup(){
  Serial.begin(9600);
  pinMode(ECHOPIN, INPUT);
  pinMode(TRIGPIN, OUTPUT);
  pinMode(BUZZERPIN, OUTPUT);
  dht.begin();
  scale.set_scale(44500);
  scale.set_offset(-210500);
  initWiFi();
  client.setServer(SERVER, PORT);
  attachInterrupt(0, hallChange, CHANGE);
}

void loop(){
  unsigned long currentMillis = millis();
  if (wasLidOpened){
    previousMillis = millis();
    commitData(true, false);
    wasLidOpened = false;
  } 
  if ((unsigned long)(currentMillis - previousMillis) >= UPDATE_INTERVAL){
    previousMillis = millis();
    if (isLidOpened){
      commitData(false, true);
    } else {
      commitData(false, false);
    }
  }
}

void initWiFi(){
  esp8266Serial.begin(9600);
  WiFi.init(&esp8266Serial);
  if (WiFi.status() == WL_NO_SHIELD){
    Serial.println("[WiFiEsp] WiFi shield not present");
    while (true);
  }

  Serial.println("[WiFiEsp] Connecting to AP ...");
  while (status != WL_CONNECTED) {
    Serial.print("[WiFiEsp] Attempting to connect to WPA SSID: ");
    Serial.println(AP_SSID);
    status = WiFi.begin(AP_SSID, AP_PASSWORD);
    delay(500);
  }
  Serial.println("[WiFiEsp] Connected to AP");
}

void reconnectWiFi(){
  while (status != WL_CONNECTED){
    Serial.print("[WiFiEsp] Attempting to reconnect to WPA SSID: ");
    Serial.println(AP_SSID);
    status = WiFi.begin(AP_SSID, AP_PASSWORD);
    delay(500);
  }
  Serial.println("[WiFiEsp] Reconnected to AP");
}

void reconnectClient(){
  while (!client.connected()){
    Serial.print("[PubSubClient] Connecting to ");
    Serial.println(SERVER);
    if (client.connect(BIN_ID)){ //(clientId, username, password)
      Serial.println( "[PubSubClient] Connected" );
    } else {
      Serial.print("[PubSubClient] Failed to connect, retrying in 5 seconds");
      delay(5000);
    }
  }
}

void commitData(boolean isLidEvent, boolean isLidOpen){
  //DHT
  if (isLidEvent) {
    delay(1500);
    data.humidity = dht.readHumidity();
    data.temperature = dht.readTemperature();
  } else {
    data.humidity = dht.readHumidity();
    data.temperature = dht.readTemperature();
  }
  if (isnan(data.humidity) || isnan(data.temperature)) {
    Serial.println("[DHT] Failed to read from sensor!");
  }
  //HC-SR04
  digitalWrite(TRIGPIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIGPIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGPIN, LOW);
  long duration = pulseIn(ECHOPIN, HIGH);
  float distance;
  if (!isnan(data.humidity) && (!isnan(data.temperature))){
    float speedOfSound = (331.296 + (0.606 * data.temperature) * 1 + (data.humidity * 9.064 * pow(10, -6) * pow(10, (0.032 * (data.temperature - (0.004 * (data.temperature * data.temperature)))))));
    distance = duration / (20000 / speedOfSound);
  } else {
    distance = duration * 0.034 / 2;
  }
  float partialHeight = bin.height - distance;
  if (partialHeight < 0) {
    partialHeight = 0;
  }
  data.fill = (partialHeight * 100)/bin.height;
  if (partialHeight == 0) {
    data.volume = 0;
  } else {
    float partialWidth = ((partialHeight * bin.topWidth) + (bin.height - partialHeight) * bin.bottomWidth)/bin.height;
    float partialLength = ((partialHeight * bin.topLength) + (bin.height - partialHeight) * bin.bottomLength)/bin.height;
    float topArea = partialWidth * partialLength;
    float bottomArea = bin.bottomWidth * bin.bottomLength;
    //data.volume = (bin.height/3) * (bottomArea + topArea + sqrt(bottomArea * topArea));
    float volumeCm3 = partialHeight * (topArea + bottomArea)/2;
    data.volume = volumeCm3/1000000; //convert to m3
  }
  //HX711
  data.weight = scale.get_units(), 10;
  if (isnan(data.humidity) && isnan(data.temperature)){
    isLidOpened = true;
    Serial.println("Detected lid opened");
  } else {
    isLidOpened = false;
    Serial.println("Detected lid closed");
  }
  if (isLidEvent && !isnan(data.humidity) && !isnan(data.temperature)) {
    tone(BUZZERPIN, 2500, 200);
    delay(200);
    noTone(BUZZERPIN);
  }
  String payload = "field1="; 
  if (isnan(data.humidity)){
    payload += "n";
  } else {
    payload += data.humidity;
  }
  payload += "&field2="; 
  if (isnan(data.temperature)){
    payload += "n";
  } else {
    payload += data.temperature;
  }
  payload += "&field3=";
  if (isLidOpened) {
    payload += "n";
  } else {
    payload += data.fill; 
  }
  payload += "&field4="; 
  if (isLidOpened) {
    payload += "n";
  } else {
    payload += data.volume;
  }
  payload += "&field5="; 
  payload += data.weight;
  if (isLidEvent){
    payload += "&field6="; 
    payload += "1";
    payload += "&field7="; 
    payload += timeOpened / 1000;
  }
  if (isLidOpen){
    payload += "&field8="; 
    payload += "1";
  }
  char data[payload.length()+1];
  Serial.print("Sendind data ");
  Serial.println(payload);
  payload.toCharArray(data, payload.length()+1);
  sendData(data);
}

void sendData(char* data){
  status = WiFi.status();
  if (status != WL_CONNECTED){
    reconnectWiFi();
  }
  if (!client.connected()){
    reconnectClient();
  }
  if (client.publish(TOPIC, data)){
    Serial.println("[PubSubClient] Payload sent sucessfully");
  } else {
    Serial.println("[PubSubClient] Error while trying to send payload");
  }
}

void hallChange(){
  if (!isLidOpened) {
    timeStart = timeEnd;
    isLidOpened = true;
    wasLidOpened = false;
    Serial.println("[System] Lid opened");
  } else {
    timeEnd = millis();
    isLidOpened = false;
    wasLidOpened = true;
    timeOpened = timeEnd - timeStart;
    Serial.print("[System] Lid closed, was opened for ");
    Serial.print(timeOpened);
    Serial.println("ms");
  }
}
