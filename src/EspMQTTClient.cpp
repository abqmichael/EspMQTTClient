#include "EspMQTTClient.h"


// =============== Constructor / destructor ===================

// MQTT only (no wifi connection attempt)
EspMQTTClient::EspMQTTClient(
  const char* mqttServerIp,
  const short mqttServerPort,
  const char* mqttClientName) :
  EspMQTTClient(NULL, NULL, mqttServerIp, NULL, NULL, mqttClientName, mqttServerPort)
{
}
EspMQTTClient::EspMQTTClient(
  const char* mqttServerIp,
  const short mqttServerPort,
  const char* mqttUsername,
  const char* mqttPassword,
  const char* mqttClientName) :
  EspMQTTClient(NULL, NULL, mqttServerIp, mqttUsername, mqttPassword, mqttClientName, mqttServerPort)
{
}

// Wifi and MQTT handling
EspMQTTClient::EspMQTTClient(
  const char* wifiSsid,
  const char* wifiPassword,
  const char* mqttServerIp,
  const char* mqttClientName,
  const short mqttServerPort) :
  EspMQTTClient(wifiSsid, wifiPassword, mqttServerIp, NULL, NULL, mqttClientName, mqttServerPort)
{
}

// Warning : for old constructor support, this will be deleted soon or later
EspMQTTClient::EspMQTTClient(
  const char* wifiSsid, 
  const char* wifiPassword, 
  const char* mqttServerIp,
  const short mqttServerPort, 
  const char* mqttUsername, 
  const char* mqttPassword,
  const char* mqttClientName, 
  ConnectionEstablishedCallback connectionEstablishedCallback,
  const bool enableWebUpdater, 
  const bool enableSerialLogs) :
  EspMQTTClient(wifiSsid, wifiPassword, mqttServerIp, mqttUsername, mqttPassword, mqttClientName, mqttServerPort)
{
  if (enableWebUpdater)
    enableHTTPWebUpdater();

  if (enableSerialLogs)
    enableDebuggingMessages();

  setOnConnectionEstablishedCallback(connectionEstablishedCallback);

  mShowLegacyConstructorWarning = true;
}

// Warning : for old constructor support, this will be deleted soon or later
EspMQTTClient::EspMQTTClient(
  const char* wifiSsid, 
  const char* wifiPassword,
  ConnectionEstablishedCallback connectionEstablishedCallback, 
  const char* mqttServerIp, 
  const short mqttServerPort,
  const char* mqttUsername,
  const char* mqttPassword, 
  const char* mqttClientName,
  const bool enableWebUpdater,
  const bool enableSerialLogs) :
  EspMQTTClient(wifiSsid, wifiPassword, mqttServerIp, mqttUsername, mqttPassword, mqttClientName, mqttServerPort)
{
  if (enableWebUpdater)
    enableHTTPWebUpdater();

  if (enableSerialLogs)
    enableDebuggingMessages();

  setOnConnectionEstablishedCallback(connectionEstablishedCallback);

  mShowLegacyConstructorWarning = true;
}

EspMQTTClient::EspMQTTClient(
  const char* wifiSsid,
  const char* wifiPassword,
  const char* mqttServerIp,
  const char* mqttUsername,
  const char* mqttPassword,
  const char* mqttClientName,
  const short mqttServerPort) :
  mWifiSsid(wifiSsid),
  mWifiPassword(wifiPassword),
  mMqttServerIp(mqttServerIp),
  mMqttUsername(mqttUsername),
  mMqttPassword(mqttPassword),
  mMqttClientName(mqttClientName),
  mMqttServerPort(mqttServerPort),
  mMqttClient(mqttServerIp, mqttServerPort, mWifiClient)
{
#ifdef WIFI_FIX
   mConnState = 0; // connection state (1-5)
   mWaitCount = 0;
   mBrokerConnectPauseMills = 60*1000;
#else
  // WiFi connection
  mWifiConnected = false;
  mLastWifiConnectionAttemptMillis = 0;
  mLastWifiConnectionSuccessMillis = 0;
#endif

  // MQTT client
  mTopicSubscriptionListSize = 0;
  mMqttConnected = false;
  mLastMqttConnectionMillis = 0;
  mMqttLastWillTopic = 0;
  mMqttLastWillMessage = 0;
  mMqttLastWillRetain = false;
  mMqttCleanSession = true;
  mMqttClient.setCallback([this](char* topic, byte* payload, unsigned int length) {this->mqttMessageReceivedCallback(topic, payload, length);});

  // Web updater
  mUpdateServerAddress = NULL;
  mHttpServer = NULL;
  mHttpUpdater = NULL;

  // other
  mEnableSerialLogs = false;
  mConnectionEstablishedCallback = onConnectionEstablished;
  mShowLegacyConstructorWarning = false;
  mDelayedExecutionListSize = 0;
  mConnectionEstablishedCount = 0;
}

EspMQTTClient::~EspMQTTClient()
{
  if (mHttpServer != NULL)
    delete mHttpServer;
  if (mHttpUpdater != NULL)
    delete mHttpUpdater;
}


// =============== Configuration functions, most of them must be called before the first loop() call ==============

void EspMQTTClient::enableDebuggingMessages(const bool enabled)
{
  mEnableSerialLogs = enabled;
}

#ifdef WIFI_FIX
void EspMQTTClient::configureHTTPWebUpdater()
{
  MDNS.begin(mMqttClientName);
  mHttpUpdater->setup(mHttpServer, mUpdateServerAddress, mUpdateServerUsername, mUpdateServerPassword);
  mHttpServer->begin();
  MDNS.addService("http", "tcp", 80);

  if (mEnableSerialLogs)
    Serial.printf("WEB: Updater ready, open http://%s.local in your browser and login with username '%s' and password '%s'.\n", mMqttClientName, mUpdateServerUsername, mUpdateServerPassword);
}

#endif



void EspMQTTClient::enableHTTPWebUpdater(const char* username, const char* password, const char* address)
{
  if (mHttpServer == NULL)
  {
    mHttpServer = new WebServer(80);
    mHttpUpdater = new ESPHTTPUpdateServer(mEnableSerialLogs);
    mUpdateServerUsername = (char*)username;
    mUpdateServerPassword = (char*)password;
    mUpdateServerAddress = (char*)address;
  }
  else if (mEnableSerialLogs)
    Serial.print("SYS! You can't call enableHTTPWebUpdater() more than once !\n");
}

void EspMQTTClient::enableHTTPWebUpdater(const char* address)
{
  if(mMqttUsername == NULL || mMqttPassword == NULL)
    enableHTTPWebUpdater("", "", address);
  else
    enableHTTPWebUpdater(mMqttUsername, mMqttPassword, address);
}

void EspMQTTClient::enableMQTTPersistence()
{
  mMqttCleanSession = false;
}

void EspMQTTClient::enableLastWillMessage(const char* topic, const char* message, const bool retain)
{
  mMqttLastWillTopic = (char*)topic;
  mMqttLastWillMessage = (char*)message;
  mMqttLastWillRetain = retain;
}


// =============== Public functions =================
#ifdef WIFI_FIX
void EspMQTTClient::loop()
{
  unsigned long currentMillis = millis();
  // start of non-blocking connection setup section
  
  // mConnState values
  // status |   WiFi   |    MQTT
  // -------+----------+------------
  //      0 |   down   |    down
  //      1 | starting |    down
  //      2 |    up    |    down
  //      3 |    up    |  starting
  //      4 |    up    | finalising
  //      5 |    up    |     up

  if ((WiFi.status() != WL_CONNECTED) && (mConnState != 1)) {
    mConnState = 0;
  }
  if ((WiFi.status() == WL_CONNECTED) && !mMqttClient.connected() && (mConnState != 3))  {
    mConnState = 2;
  }
  if ((WiFi.status() == WL_CONNECTED) && mMqttClient.connected() && (mConnState != 5)) {
    mConnState = 4;
  }
  switch (mConnState) {
    case 0:                                                       // MQTT and WiFi down: start WiFi
      if (mEnableSerialLogs)
	Serial.println("WiFi and MQTT down: starting WiFi");
      connectToWifi();
      mConnState = 1;
      break;
    case 1:                                                       // WiFi starting, do nothing here
      if (mEnableSerialLogs) {
	if (mWaitCount == 0) 
	{
	  Serial.printf("WiFi starting, MQTT down, waiting %d ", mWaitCount);
	}
	else if (mWaitCount % 1000 == 0) 
	{
	  Serial.printf("\nWiFi starting, MQTT down, waiting %d ", mWaitCount);
	}
	else if (mWaitCount < 11) 
	{
	  Serial.printf("%d, ", mWaitCount);
	}
	else if (mWaitCount % 50 == 0) 
	{
	  Serial.printf(".", mWaitCount);
	}
      }
      delay(10);
      mWaitCount++;
      break;
    case 2:                                                       // WiFi up, MQTT down: start MQTT
      if (mEnableSerialLogs) {
	Serial.print("\nWiFi up, MQTT down. IP = ");
	Serial.println(WiFi.localIP());
      }
      mWaitCount = 0;
      // Configure web updater
      if (mHttpServer != NULL)
      {
	configureHTTPWebUpdater();
      }
      mTopicSubscriptionListSize = 0;
      mConnState = 3;
      break;
    case 3:                                                       // WiFi up, MQTT starting, do nothing here
      mWaitCount++;
      if (mWaitCount < 3) 
      {
	Serial.println("WiFi up, MQTT starting");
      } 
      if (mWaitCount == 2 || ((mWaitCount % 1000000) == 0)) 
      {
	Serial.print("\nWiFi up, MQTT starting ");
      } 
      else if ((mWaitCount % 20000) == 0)
      {
	Serial.print(".");
      }
      connectToMqttBroker();
      break;
    case 4:                                                       // WiFi up, MQTT up: finish MQTT configuration
      if (mEnableSerialLogs)
      {
	if (mWaitCount < 2) {
	  Serial.println("WiFi up, MQTT up on first try.");
	} 
	else 
	{
	  Serial.println("WiFi up, MQTT up after multiple tries.");
	}
      }
      //mMqttClient.subscribe(output_topic);
      mWaitCount = 0;
      mConnState = 5;
      break;
    case 5:                                                       // Running MQTT
      mMqttClient.loop();
      // Web updater handling
      if (mHttpServer != NULL)
      {
	mHttpServer->handleClient();
	#ifdef ESP8266
	  MDNS.update(); // We need to do this only for ESP8266
	#endif
      }
  }
  // Delayed execution handling
  if (mDelayedExecutionListSize > 0)
  {
    unsigned long currentMillis = millis();

    for(byte i = 0 ; i < mDelayedExecutionListSize ; i++)
    {
      if (mDelayedExecutionList[i].targetMillis <= currentMillis)
      {
	//Serial.printf("Calling # %d, current ms %d, target %d\n", i, mDelayedExecutionList[i].targetMillis, currentMillis);
        (*mDelayedExecutionList[i].callback)();
        for(byte j = i ; j < mDelayedExecutionListSize-1 ; j++)
          mDelayedExecutionList[j] = mDelayedExecutionList[j + 1];
        mDelayedExecutionListSize--;
        i--;
      }
    }
  }

  // Old constructor support warning
  if (mEnableSerialLogs && mShowLegacyConstructorWarning)
  {
    mShowLegacyConstructorWarning = false;
    Serial.print("SYS! You are using a constructor that will be deleted soon, please update your code with the new construction format.\n");
  }
}
#else
void EspMQTTClient::loop()
{
  unsigned long currentMillis = millis();

  if (WiFi.status() == WL_CONNECTED)
  {
    // If we just being connected to wifi
    if (!mWifiConnected)
    {
      if (mEnableSerialLogs)
        Serial.printf("WiFi: Connected, ip : %s\n", WiFi.localIP().toString().c_str());

      mLastWifiConnectionSuccessMillis = millis();
      
      // Config of web updater
      if (mHttpServer != NULL)
      {
	MDNS.begin(mMqttClientName);
        mHttpUpdater->setup(mHttpServer, mUpdateServerAddress, mUpdateServerUsername, mUpdateServerPassword);
        mHttpServer->begin();
        MDNS.addService("http", "tcp", 80);
      
        if (mEnableSerialLogs)
          Serial.printf("WEB: Updater ready, open http://%s.local in your browser and login with username '%s' and password '%s'.\n", mMqttClientName, mUpdateServerUsername, mUpdateServerPassword);
      }
      mWifiConnected = true;
    }
    
    // MQTT handling
    if (mMqttClient.connected())
      mMqttClient.loop();
    else
    {
      if (mMqttConnected)
      {
        if (mEnableSerialLogs)
          Serial.println("MQTT! Lost connection.");
        
        mTopicSubscriptionListSize = 0;
        mMqttConnected = false;
      }
      
      if (currentMillis - mLastMqttConnectionMillis > CONNECTION_RETRY_DELAY || mLastMqttConnectionMillis == 0)
        connectToMqttBroker();
    }
      
    // Web updater handling
    if (mHttpServer != NULL)
    {
      mHttpServer->handleClient();
      #ifdef ESP8266
        MDNS.update(); // We need to do this only for ESP8266
      #endif
    }
      
  }
  else // If we are not connected to wifi
  {
    if (mWifiConnected) 
    {
      if (mEnableSerialLogs)
        Serial.println("WiFi! Lost connection.");
      
      mWifiConnected = false;

      // If we handle wifi, we force disconnection to clear the last connection
      if (mWifiSsid != NULL)
        WiFi.disconnect();
    }
    
    // We retry to connect to the wifi if we handle it and there was no attempt since the last connection lost
    if (mWifiSsid != NULL && (mLastWifiConnectionAttemptMillis == 0 || mLastWifiConnectionSuccessMillis > mLastWifiConnectionAttemptMillis))
      connectToWifi();
  }
  
  // Delayed execution handling
  if (mDelayedExecutionListSize > 0)
  {
    unsigned long currentMillis = millis();

    for(byte i = 0 ; i < mDelayedExecutionListSize ; i++)
    {
      if (mDelayedExecutionList[i].targetMillis <= currentMillis)
      {
        (*mDelayedExecutionList[i].callback)();
        for(byte j = i ; j < mDelayedExecutionListSize-1 ; j++)
          mDelayedExecutionList[j] = mDelayedExecutionList[j + 1];
        mDelayedExecutionListSize--;
        i--;
      }
    }
  }

  // Old constructor support warning
  if (mEnableSerialLogs && mShowLegacyConstructorWarning)
  {
    mShowLegacyConstructorWarning = false;
    Serial.print("SYS! You are using a constructor that will be deleted soon, please update your code with the new construction format.\n");
  }

}
#endif

bool EspMQTTClient::publish(const String &topic, const String &payload, bool retain)
{
  bool success = mMqttClient.publish(topic.c_str(), payload.c_str(), retain);

  if (mEnableSerialLogs) 
  {
    if(success)
      Serial.printf("MQTT << [%s] %s\n", topic.c_str(), payload.c_str());
    else
      Serial.println("MQTT! publish failed, is the message too long ?"); // This can occurs if the message is too long according to the maximum defined in PubsubClient.h
  }

  return success;
}

bool EspMQTTClient::subscribe(const String &topic, MessageReceivedCallback messageReceivedCallback)
{
  // Check the possibility to add a new topic
  if (mTopicSubscriptionListSize >= MAX_TOPIC_SUBSCRIPTION_LIST_SIZE) 
  {
    if (mEnableSerialLogs)
      Serial.println("MQTT! Subscription list is full, ignored.");
    return false;
  }

  // Check the duplicate of the subscription to the topic
  bool found = false;
  for (byte i = 0; i < mTopicSubscriptionListSize && !found; i++)
    found = mTopicSubscriptionList[i].topic.equals(topic);

  if (found) 
  {
    if (mEnableSerialLogs)
      Serial.printf("MQTT! Subscribed to [%s] already, ignored.\n", topic.c_str());
    return false;
  }

  // All checks are passed - do the job
  bool success = mMqttClient.subscribe(topic.c_str());

  if(success)
    mTopicSubscriptionList[mTopicSubscriptionListSize++] = { topic, messageReceivedCallback, NULL };
  
  if (mEnableSerialLogs)
  {
    if(success)
      Serial.printf("MQTT: Subscribed to [%s]\n", topic.c_str());
    else
      Serial.println("MQTT! subscribe failed");
  }

  return success;
}

bool EspMQTTClient::subscribe(const String &topic, MessageReceivedCallbackWithTopic messageReceivedCallback)
{
  if(subscribe(topic, (MessageReceivedCallback)NULL))
  {
    mTopicSubscriptionList[mTopicSubscriptionListSize-1].callbackWithTopic = messageReceivedCallback;
    return true;
  }
  return false;
}

bool EspMQTTClient::unsubscribe(const String &topic)
{
  bool found = false;
  bool success = false;

  for (byte i = 0; i < mTopicSubscriptionListSize; i++)
  {
    if (!found)
    {
      if (mTopicSubscriptionList[i].topic.equals(topic))
      {
        found = true;
        success = mMqttClient.unsubscribe(topic.c_str());

        if (mEnableSerialLogs)
        {
          if(success)
            Serial.printf("MQTT: Unsubscribed from %s\n", topic.c_str());
          else
            Serial.println("MQTT! unsubscribe failed");
        }
      }
    }

    if (found)
    {
      if ((i + 1) < MAX_TOPIC_SUBSCRIPTION_LIST_SIZE)
        mTopicSubscriptionList[i] = mTopicSubscriptionList[i + 1];
    }
  }

  if (found)
    mTopicSubscriptionListSize--;
  else if (mEnableSerialLogs)
    Serial.println("MQTT! Topic cannot be found to unsubscribe, ignored.");

  return success;
}

void EspMQTTClient::executeDelayed(const unsigned long delay, DelayedExecutionCallback callback)
{
  if (mDelayedExecutionListSize < MAX_DELAYED_EXECUTION_LIST_SIZE)
  {
    DelayedExecutionRecord delayedExecutionRecord;
    delayedExecutionRecord.targetMillis = millis() + delay;
    delayedExecutionRecord.callback = callback;
    
    mDelayedExecutionList[mDelayedExecutionListSize] = delayedExecutionRecord;
    mDelayedExecutionListSize++;
    //Serial.printf("Added delayed execution, list size = %d\n", mDelayedExecutionListSize);
  }
  else if (mEnableSerialLogs)
    Serial.printf("SYS! The list of delayed functions is full.\n");
}

#ifdef WIFI_FIX
bool EspMQTTClient::isConnected() // identical to isMqttConnected
{
  //Serial.printf("Connection State = %d\n", mConnState);
  if (mConnState = 5) 
    return true;
  return false;
}

bool EspMQTTClient::isWifiConnected()
{ 
  if (mConnState >= 3) 
    return true;
  return false;
}

bool EspMQTTClient::isMqttConnected()
{
  if (mConnState = 5) 
    return true;
  return false;
}

void EspMQTTClient::brokerConnectPause(unsigned int mills)
{
  mBrokerConnectPauseMills = mills;
}
#endif
// ================== Private functions ====================-

void EspMQTTClient::connectToWifi()
{
  WiFi.mode(WIFI_STA);
  #ifdef ESP32
    WiFi.setHostname(mMqttClientName);
  #else
    WiFi.hostname(mMqttClientName);
  #endif
  WiFi.begin(mWifiSsid, mWifiPassword);

  if (mEnableSerialLogs)
    Serial.printf("\nWiFi: Connecting to %s ... \n", mWifiSsid);
#ifndef WIFI_FIX
  mLastWifiConnectionAttemptMillis = millis();
#endif
}

void EspMQTTClient::connectToMqttBroker()
{
#ifdef WIFI_FIX
  if ((mLastMqttConnectionMillis == 0) || // e.g., first connection
      (mLastMqttConnectionMillis + mBrokerConnectPauseMills < millis())) {
#endif
    if (mEnableSerialLogs) {
      //Serial.printf("Time = %d, Last connect + pause = %d\n", millis(), mLastMqttConnectionMillis + mBrokerConnectPauseMills);
      Serial.printf("MQTT: Connecting to broker @%s ... ", mMqttServerIp);
    }
    if (mMqttClient.connect(mMqttClientName, mMqttUsername, mMqttPassword, mMqttLastWillTopic, 0, mMqttLastWillRetain, mMqttLastWillMessage, mMqttCleanSession))
    {
      mMqttConnected = true;
      
      if (mEnableSerialLogs) 
	Serial.println("ok.");
  
      mConnectionEstablishedCount++;
      (*mConnectionEstablishedCallback)();
    }
    else if (mEnableSerialLogs)
    {
      Serial.print("unable to connect, ");
  
      switch (mMqttClient.state())
      {
	case -4:
	  Serial.println("MQTT_CONNECTION_TIMEOUT");
	  break;
	case -3:
	  Serial.println("MQTT_CONNECTION_LOST");
	  break;
	case -2:
	  Serial.println("MQTT_CONNECT_FAILED");
	  break;
	case -1:
	  Serial.println("MQTT_DISCONNECTED");
	  break;
	case 1:
	  Serial.println("MQTT_CONNECT_BAD_PROTOCOL");
	  break;
	case 2:
	  Serial.println("MQTT_CONNECT_BAD_CLIENT_ID");
	  break;
	case 3:
	  Serial.println("MQTT_CONNECT_UNAVAILABLE");
	  break;
	case 4:
	  Serial.println("MQTT_CONNECT_BAD_CREDENTIALS");
	  break;
	case 5:
	  Serial.println("MQTT_CONNECT_UNAUTHORIZED");
	  break;
      }
    }
    mLastMqttConnectionMillis = millis();
#ifdef WIFI_FIX
  }
#endif
}

/**
 * Matching MQTT topics, handling the eventual presence of a single wildcard character
 *
 * @param topic1 is the topic may contain a wildcard
 * @param topic2 must not contain wildcards
 * @return true on MQTT topic match, false otherwise
 */
bool EspMQTTClient::mqttTopicMatch(const String &topic1, const String &topic2) {
	//Serial.println(String("Comparing: ") + topic1 + " and " + topic2);
	int i = 0;
	if((i = topic1.indexOf('#')) >= 0) {
		//Serial.print("# detected at position "); Serial.println(i);
		String t1a = topic1.substring(0, i);
		String t1b = topic1.substring(i+1);
		//Serial.println(String("t1a: ") + t1a);
		//Serial.println(String("t1b: ") + t1b);
		if((t1a.length() == 0 || topic2.startsWith(t1a))&&
		   (t1b.length() == 0 || topic2.endsWith(t1b)))
		   return true;
	} else if((i= topic1.indexOf('+')) >= 0) {
		//Serial.print("+ detected at position "); Serial.println(i);
		String t1a = topic1.substring(0, i);
		String t1b = topic1.substring(i+1);
		//Serial.println(String("t1a: ") + t1a);
		//Serial.println(String("t1b: ") + t1b);
		if((t1a.length() == 0 || topic2.startsWith(t1a))&&
		   (t1b.length() == 0 || topic2.endsWith(t1b))) {
			if(topic2.substring(t1a.length(), topic2.length()-t1b.length()).indexOf('/') == -1)
				return true;
		}

	} else {
		return topic1.equals(topic2);
	}
	return false;
}

void EspMQTTClient::mqttMessageReceivedCallback(char* topic, byte* payload, unsigned int length)
{
  // Convert the payload into a String
  // First, We ensure that we dont bypass the maximum size of the PubSubClient library buffer that originated the payload
  // This buffer has a maximum length of MQTT_MAX_PACKET_SIZE and the payload begin at "headerSize + topicLength + 1"
  unsigned int strTerminationPos;
  if (strlen(topic) + length + 9 >= MQTT_MAX_PACKET_SIZE)
  {
    strTerminationPos = length - 1;

    if (mEnableSerialLogs)
      Serial.print("MQTT! Your message may be truncated, please change MQTT_MAX_PACKET_SIZE of PubSubClient.h to a higher value.\n");
  }
  else
    strTerminationPos = length;

  // Second, we add the string termination code at the end of the payload and we convert it to a String object
  payload[strTerminationPos] = '\0';
  String payloadStr((char*)payload);
  String topicStr(topic);

  // Logging
  if (mEnableSerialLogs)
    Serial.printf("MQTT >> [%s] %s\n", topic, payloadStr.c_str());

  // Send the message to subscribers
  for (byte i = 0 ; i < mTopicSubscriptionListSize ; i++)
  {
    if (mqttTopicMatch(mTopicSubscriptionList[i].topic, String(topic)))
    {
      if(mTopicSubscriptionList[i].callback != NULL)
        (*mTopicSubscriptionList[i].callback)(payloadStr); // Call the callback
      if(mTopicSubscriptionList[i].callbackWithTopic != NULL)
        (*mTopicSubscriptionList[i].callbackWithTopic)(topicStr, payloadStr); // Call the callback
    }
  }
}
