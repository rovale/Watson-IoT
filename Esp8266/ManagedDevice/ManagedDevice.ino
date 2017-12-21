#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Secrets.h>

char mqttServer[] = ORG ".messaging.internetofthings.ibmcloud.com";
char authMethod[] = "use-token-auth";
char token[] = TOKEN;
char clientId[] = "d:" ORG ":" DEVICE_TYPE ":" DEVICE_ID;

const char publishTopic[] = "iot-2/evt/status/fmt/json";
const char responseTopic[] = "iotdm-1/response";
const char manageTopic[] = "iotdevice-1/mgmt/manage";
const char updateTopic[] = "iotdm-1/device/update";
const char rebootTopic[] = "iotdm-1/mgmt/initiate/device/reboot";

WiFiClient wiFiClient;
PubSubClient client(wiFiClient);

unsigned long lastReconnectAttemptAt = 0;
unsigned long lastPublishMessageAt = 0;
int publishInterval = 60000;

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
    JsonObject &root = jsonBuffer.parseObject((char *)payload);
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
    for (JsonArray::iterator it = fields.begin(); it != fields.end(); ++it)
    {
        JsonObject &field = *it;
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

void initManagedDevice()
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
    Serial.println("publishing device metadata:");
    Serial.println(buff);
    client.publish(manageTopic, buff);
}

boolean connectToMqqtBroker()
{
  Serial.print("Connecting to MQTT broker...");
  if (client.connect(clientId, authMethod, token))
  {
    Serial.println("connected");
    initManagedDevice();
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
  connectToNetwork();
  client.setServer(mqttServer, 1883);
  client.setCallback(onReceive);
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
    client.loop();

    if (currentMillis - lastPublishMessageAt >= publishInterval)
    {
      lastPublishMessageAt = currentMillis;

      //int rssi = WiFi.RSSI();
      //publish(rssiTopic, String(rssi));

      String payload = "{\"d\":{\"uptime\":";
      payload += currentMillis / 1000;
      payload += "}}";
  
      Serial.print("Sending payload: ");
      Serial.println(payload);
  
      client.publish(publishTopic, (char *)payload.c_str());
    }
  }
}
