#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>

#include <ArduinoOTA.h>

#include <PubSubClient.h>

#include <ArduinoJson.h>

#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <DHT.h>

#include <Secrets.h>

char mqttServer[] = ORG ".messaging.internetofthings.ibmcloud.com";
char authMethod[] = "use-token-auth";
char token[] = TOKEN;
char clientId[] = "d:" ORG ":" DEVICE_TYPE ":" DEVICE_ID;

const char responseTopic[] = "iotdm-1/response";
const char updateTopic[] =   "iotdm-1/device/update";
const char rebootTopic[] =   "iotdm-1/mgmt/initiate/device/reboot";

const char manageTopic[] =   "iotdevice-1/mgmt/manage";
const char publishTopic[] =  "iot-2/evt/status/fmt/json";

void onReceive(char* topic, byte* payload, unsigned int payloadLength);

WiFiClientSecure wifiClient;
PubSubClient client(mqttServer, 8883, onReceive, wifiClient);

Adafruit_BMP085 bmp;
DHT dht(D3, DHT11);

unsigned long lastReconnectAttemptAt = 0;

int publishInterval = 300000;
unsigned long lastPublishMessageAt = 0;

void initializeOTA()
{
    // Port defaults to 8266
    // ArduinoOTA.setPort(8266);

    // Hostname defaults to esp8266-[ChipID]
    // ArduinoOTA.setHostname("myesp8266");

    // No authentication by default
    // ArduinoOTA.setPassword((const char *)"123");

    ArduinoOTA.onStart([]() {
        Serial.println("Start");
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

    ArduinoOTA.begin();
}

void connectToNetwork()
{
    delay(10);
    Serial.println();
    Serial.print("Connecting to ");
    Serial.print(WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.print("connected at address ");
    Serial.println(WiFi.localIP());
}

void onReceive(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");

    for (int i = 0; i < length; i++)
    {
        Serial.print((char)payload[i]);
    }
    Serial.println();

    if (strcmp(responseTopic, topic) == 0)
    {
        return;
    }

    if (strcmp(rebootTopic, topic) == 0)
    {
        Serial.println("Rebooting...");
        ESP.restart();
    }

    if (strcmp(updateTopic, topic) == 0)
    {
        handleUpdate(payload);
    }
}

void handleUpdate(byte *payload)
{
    StaticJsonBuffer<300> jsonBuffer;
    JsonObject &root = jsonBuffer.parseObject(payload);
    if (!root.success())
    {
        Serial.println("handleUpdate: payload parse FAILED");
        return;
    }

    Serial.println("handleUpdate payload:");
    root.prettyPrintTo(Serial);
    Serial.println();

    JsonObject &d = root["d"];
    JsonArray &fields = d["fields"];
    for (auto &field : fields)
    {
        const char *fieldName = field["field"];
        if (strcmp(fieldName, "metadata") == 0)
        {
            JsonObject &fieldValue = field["value"];
            if (fieldValue.containsKey("publishInterval"))
            {
                publishInterval = fieldValue["publishInterval"];
                Serial.print("publishInterval:");
                Serial.println(publishInterval);
            }
        }
    }
}

void initializeManagedDevice()
{
    client.subscribe(rebootTopic);
    client.subscribe(responseTopic);
    client.subscribe(updateTopic);

    StaticJsonBuffer<300> jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();
    JsonObject &d = root.createNestedObject("d");
    JsonObject &metadata = d.createNestedObject("metadata");
    metadata["publishInterval"] = publishInterval;
    JsonObject &supports = d.createNestedObject("supports");
    supports["deviceActions"] = true;

    char buff[300];
    root.printTo(buff, sizeof(buff));
    Serial.println(buff);
    Serial.println(client.publish(manageTopic, buff));
}

void publish()
{
  int rssi = WiFi.RSSI();
  float temperature  = bmp.readTemperature();
  int pressure = bmp.readPressure();
  float humidity = dht.readHumidity();
  float temperature2 = dht.readTemperature();
  int air = analogRead(A0);
  
  StaticJsonBuffer<300> jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();
  JsonObject &d = root.createNestedObject("d");
  d["rssi"] = rssi;
  d["uptime"] = millis() / 1000;
  d["temperature"] = temperature;
  d["temperature2"] = temperature2;
  d["pressure"] = pressure;
  d["humidity"] = humidity;
  d["air"] = air;
  
  char payload[300];
  root.printTo(payload, sizeof(payload));  
  Serial.println(payload);
  Serial.println(client.publish(publishTopic, payload, 300));
}

boolean connectToMqqtBroker()
{
    Serial.print("Connecting to MQTT broker...");
    Serial.print(clientId);
    if (client.connect(clientId, authMethod, token))
    {
        Serial.println("connected");
        initializeManagedDevice();
        publish();
    }
    else
    {
        Serial.print("failed, rc=");
        Serial.println(client.state());
    }

    return client.connected();
}

void setup()
{
    Serial.begin(115200);

    bmp.begin();
    dht.begin();

    initializeOTA();
    connectToNetwork();
}

void loop()
{
    unsigned long currentMillis = millis();

    if (!client.connected())
    {
        if (currentMillis - lastReconnectAttemptAt >= 5000)
        {
            lastReconnectAttemptAt = currentMillis;

            if (connectToMqqtBroker())
            {
                lastReconnectAttemptAt = 0;
            }
        }
    }
    else
    {
        ArduinoOTA.handle();
        
        client.loop();

        if (currentMillis - lastPublishMessageAt >= publishInterval)
        {
            lastPublishMessageAt = currentMillis;
            publish();
        }
    }
}
