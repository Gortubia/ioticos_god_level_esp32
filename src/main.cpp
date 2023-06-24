//VERY IMPORTANT
//to see colors in terminals add this line at the end of platformio.ini
//monitor_flags = --raw
#include <Arduino.h>
#include "Colors.h"
#include "IoTicosSplitter.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
//Incluir las bibliotecas max6675
#include "max6675.h"

String dId = "01-01-02";
String webhook_pass = "IopsIVKSvZ"; 
String webhook_endpoint = "http://192.168.1.24:3001/api/getdevicecredentials";
const char *mqtt_server = "192.168.1.24";

//PINS
#define led CONFIG_BLINK_GPIO
#define ifserial true

//Selecciona el pin al que se conecta el sensor de temperatura
int MISO_th = 19;//Miso 19
int CS_th = 5; //CSO 5
int SCLK_th = 18;//CLK18

//Comunicar que vamos a utilizar la interfaz max6675
MAX6675 thermocouple(SCLK_th, CS_th, MISO_th);

//WiFi
const char *wifi_ssid = "Casa_23";
const char *wifi_password = "puelche2021";


//Functions definitions
bool get_mqtt_credentials();
void check_mqtt_connection();
bool reconnect();
void process_sensors();
void process_actuators();
void send_data_to_broker();
void callback(char *topic, byte *payload, unsigned int length);
void process_incoming_msg(String topic, String incoming);
void print_stats();
void clear();

//Global Vars
WiFiClient espclient;
PubSubClient client(espclient);
IoTicosSplitter splitter;
long lastReconnectAttemp = 0;
long varsLastSend[20];
String last_received_msg = "";
String last_received_topic = "";
int prev_temp = 0;
int prev_hum = 0;

DynamicJsonDocument mqtt_data_doc(2048);

void setup()
{

  if(ifserial){
  Serial.begin(921600);
  clear();
  }
  pinMode(led, OUTPUT);
  if(ifserial){
  Serial.print(underlinePurple + "\n\n\nWiFi Connection in Progress" + fontReset + Purple);
  }
  WiFi.begin(wifi_ssid, wifi_password);

  int counter = 0;

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    if(ifserial){
    Serial.print(".");
    }
    counter++;

    if (counter > 10)
    {
      if(ifserial){
      Serial.print("  ⤵" + fontReset);
      Serial.print(Red + "\n\n         Ups WiFi Connection Failed :( ");
      Serial.println(" -> Restarting..." + fontReset);
      }
      delay(2000);
      ESP.restart();
    }
  }

  if(ifserial){
  Serial.print("  ⤵" + fontReset);

  //Printing local ip
  Serial.println(boldGreen + "\n\n         WiFi Connection -> SUCCESS :)" + fontReset);
  Serial.print("\n         Local IP -> ");
  Serial.print(boldBlue);
  Serial.print(WiFi.localIP());
  Serial.println(fontReset);
  }

  client.setCallback(callback);
  //on start LED
  for (size_t i = 0; i < 4; i++)
  {
    /* led ON/OFF start esp32 */
     digitalWrite(led, HIGH);
     delay(200);
     digitalWrite(led, LOW);
     delay(200);
  }
  


}

void loop()
{
  check_mqtt_connection();

  
}



//USER FUNTIONS ⤵
void process_sensors()
{
  //Leer la temperatura
  float temperatureC = thermocouple.readCelsius(); 
  mqtt_data_doc["variables"][0]["last"]["value"] = temperatureC;

  //save temp?
  int dif = (int)temperatureC - prev_temp;

  if (dif < 0)
  {
    dif *= -1;
  }

  if (dif >= 2)
  {
    mqtt_data_doc["variables"][0]["last"]["save"] = 1;
     prev_temp = (int)temperatureC;
  }
  else
  {
    mqtt_data_doc["variables"][0]["last"]["save"] = 1;
  }



  //get humidity simulation
  int hum = random(1, 50);
  mqtt_data_doc["variables"][1]["last"]["value"] = hum;

  //save hum?
  dif = hum - prev_hum;
  if (dif < 0)
  {
    dif *= -1;
  }

  if (dif >= 20)
  {
    mqtt_data_doc["variables"][1]["last"]["save"] = 1;
  }
  else
  {
    mqtt_data_doc["variables"][1]["last"]["save"] = 0;
  }

  prev_hum = hum;

  //get led status
  mqtt_data_doc["variables"][4]["last"]["value"] = (HIGH == digitalRead(led));
}

void process_actuators()
{
  
  //Serial.println(Red + "process_actuators:");
  //Serial.println(Red + "variable 0--> ");
  String str_dat1 = mqtt_data_doc["variables"][1]["last"]["value"];
  //Serial.println(str_dat0);

  if (str_dat1 == "true")
  {
    digitalWrite(led, HIGH);
    mqtt_data_doc["variables"][1]["last"]["value"] = "";
    varsLastSend[4] = 0;
    if(ifserial){
    Serial.print(Green + "\n\n   value true ");
    }
    digitalWrite(led, HIGH);
  }
  else if (str_dat1 == "false")
  {
    digitalWrite(led, LOW);
    mqtt_data_doc["variables"][1]["last"]["value"] = "";
    varsLastSend[4] = 0;
    if(ifserial){
    Serial.print(Red + "\n\n   value false ");
    }
    digitalWrite(led, LOW);
  }



}




//TEMPLATE ⤵
void process_incoming_msg(String topic, String incoming){

  last_received_topic = topic;
  last_received_msg = incoming;

  String variable = splitter.split(topic, '/', 2);

  for (int i = 0; i < mqtt_data_doc["variables"].size(); i++ ){

    if (mqtt_data_doc["variables"][i]["variable"] == variable){
      
      DynamicJsonDocument doc(256);
      deserializeJson(doc, incoming);
      mqtt_data_doc["variables"][i]["last"] = doc;

      long counter = mqtt_data_doc["variables"][i]["counter"];
      counter++;
      mqtt_data_doc["variables"][i]["counter"] = counter;

    }

  }

  process_actuators();

}

void callback(char *topic, byte *payload, unsigned int length)
{

  String incoming = "";

  for (int i = 0; i < length; i++)
  {
    incoming += (char)payload[i];
  }

  incoming.trim();

  process_incoming_msg(String(topic), incoming);

}

void send_data_to_broker()
{

  long now = millis();

  for (int i = 0; i < mqtt_data_doc["variables"].size(); i++)
  {

    if (mqtt_data_doc["variables"][i]["variableType"] == "output")
    {
      continue;
    }

    int freq = mqtt_data_doc["variables"][i]["variableSendFreq"];

    if (now - varsLastSend[i] > freq * 1000)
    {
      varsLastSend[i] = millis();

      String str_root_topic = mqtt_data_doc["topic"];
      String str_variable = mqtt_data_doc["variables"][i]["variable"];
      String topic = str_root_topic + str_variable + "/sdata";

      String toSend = "";

      serializeJson(mqtt_data_doc["variables"][i]["last"], toSend);

      client.publish(topic.c_str(), toSend.c_str());


      //STATS
      long counter = mqtt_data_doc["variables"][i]["counter"];
      counter++;
      mqtt_data_doc["variables"][i]["counter"] = counter;

    }
  }
}

bool reconnect()
{

  if (!get_mqtt_credentials())
  {
    if(ifserial){
    Serial.println(boldRed + "\n\n      Error getting mqtt credentials :( \n\n RESTARTING IN 10 SECONDS");
    Serial.println(fontReset);
    }
    delay(10000);
    ESP.restart();
    return false;
  }

  //Setting up Mqtt Server
  client.setServer(mqtt_server, 1883);
  if(ifserial){
  Serial.print(underlinePurple + "\n\n\nTrying MQTT Connection" + fontReset + Purple + " :");
  }
  String str_client_id = "device_" + dId + "_" + random(1, 9999);
  const char *username = mqtt_data_doc["username"];
  const char *password = mqtt_data_doc["password"];
  String str_topic = mqtt_data_doc["topic"];

  if (client.connect(str_client_id.c_str(), username, password))
  { 
    if(ifserial){
    Serial.print(boldGreen + "\n\n         Mqtt Client Connected :) " + fontReset);
    }
    delay(2000);
    
    client.subscribe((str_topic + "+/actdata").c_str());
    return true;
  }
  else
  {
    if(ifserial){
    Serial.print(boldRed + "\n\n         Mqtt Client Connection Failed :( " + fontReset);
    }
    return false;
  }
  
}

void check_mqtt_connection()
{

  if (WiFi.status() != WL_CONNECTED)
  {
    if(ifserial){
    Serial.print(Red + "\n\n         Ups WiFi Connection Failed :( ");
    Serial.println(" -> Restarting..." + fontReset);
    }
    delay(15000);
    ESP.restart();
  }

  if (!client.connected())
  {

    long now = millis();

    if (now - lastReconnectAttemp > 5000)
    {
      lastReconnectAttemp = millis();
      if (reconnect())
      {
        lastReconnectAttemp = 0;
      }
    }
  }
  else
  {
    client.loop();
    process_sensors();
    send_data_to_broker();

    print_stats();
  }
}

bool get_mqtt_credentials()
{

  if(ifserial){
  Serial.print(underlinePurple + "\n\n\nGetting MQTT Credentials from WebHook" + fontReset + Purple + " : ");
  }
  delay(1000);

  String toSend = "dId=" + dId + "&password=" + webhook_pass;

  HTTPClient http;
  http.begin(webhook_endpoint);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int response_code = http.POST(toSend);

  if (response_code < 0)
  { 
    if(ifserial){
    Serial.print(boldRed + "\n\n         Error Sending Post Request :( " + fontReset);
    }
    http.end();
    return false;
  }

  if (response_code != 200)
  {
    if(ifserial){
    Serial.print(boldRed + "\n\n         Error in response :(   e-> " + fontReset + " " + response_code);
    }
    http.end();
    return false;
  }

  if (response_code == 200)
  {
    String responseBody = http.getString();
    if(ifserial){
    Serial.print(boldGreen + "\n\n         Mqtt Credentials Obtained Successfully :) " + fontReset);
    }
    deserializeJson(mqtt_data_doc, responseBody);
    http.end();
    delay(1000);
  }

  return true;
}

void clear()
{
  Serial.write(27);    // ESC command
  Serial.print("[2J"); // clear screen command
  Serial.write(27);
  Serial.print("[H"); // cursor to home command
}

long lastStats = 0;

void print_stats()
{
  long now = millis();

  if (now - lastStats > 2000)
  {
    lastStats = millis();
    if(ifserial){
    clear();

    Serial.println(" ");
    Serial.println(Purple + "╔══════════════════════════╗" + fontReset);
    Serial.println(Purple + "║       SYSTEM STATS       ║" + fontReset);
    Serial.println(Purple + "╚══════════════════════════╝" + fontReset);
    Serial.print("\n\n");
    Serial.print("\n\n");

    Serial.println(boldCyan + "#" + " \t Name" + " \t\t Var" + " \t\t Type" + " \t\t Count" + " \t\t Last V" + fontReset);

    for (int i = 0; i < mqtt_data_doc["variables"].size(); i++)
    {

      String variableFullName = mqtt_data_doc["variables"][i]["variableFullName"];
      String variable = mqtt_data_doc["variables"][i]["variable"];
      String variableType = mqtt_data_doc["variables"][i]["variableType"];
      String lastMsg = mqtt_data_doc["variables"][i]["last"];
      long counter = mqtt_data_doc["variables"][i]["counter"];

      Serial.println(String(i) + " \t " + variableFullName.substring(0,5) + " \t\t " + variable.substring(0,10) + " \t " + variableType.substring(0,5) + " \t\t " + String(counter).substring(0,10) + " \t\t " + lastMsg);
    }

    Serial.println(boldGreen + "Free RAM -> " + fontReset + ESP.getFreeHeap() + " Bytes");

    Serial.println(boldGreen + "Last Incomming Msg -> " + fontReset + last_received_msg);
    }
  }
}


