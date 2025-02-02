#include "EEPROM.h"
#include <SPI.h>
#include "WiFi.h"
#include "Wire.h"
#include "SSD1306.h"
#include "kissHost.hpp"
#include "ax25.hpp"

#define offsetEEPROM 0x0    //offset config
#define Modem_RX 22
#define Modem_TX 23
#define OLED_SCL	15			// GPIO15
#define OLED_SDA	4			// GPIO4
#define OLED_RST	16
#define TX_LED 27
#define TX_LED_DELAY 500 // milliseconds
#define OLED_ADR	0x3C		// Default 0x3C for 0.9", for 1.3" it is 0x78
#define wdtTimeout 30   //time in seconds to trigger the watchdog
#define VERSION "Arduino_RAZ_IGATE_TCP"
#define hasLCD
#define SQ_MIN 0
#define SQ_MAX 8

char receivedString[28];

uint32_t oledSleepTime = 0;
uint32_t txLedDelayTime = 0;
uint32_t lastClientUpdate = 0;

WiFiClient client;
HardwareSerial Modem(1);
KissHost kissHost(1);
AX25 ax25(1);

hw_timer_t *timer = NULL;

void IRAM_ATTR resetModule() {
	ets_printf("WDT Reboot\n");
	esp_restart();
}

struct StoreStruct {
  byte chkDigit;
  char wifiSSID[25];
  char pass[64];
  char callSign[10];
  int modemChannel;
  int squelch;
  int oledTimeout;
  int updateInterval;
  char passCode[6];
  char symbol[3];
  char latitude[9];
  char longitude[10];
  char PHG[8];
  char beaconText[37];
  char AprsIsAddress[25];
  int APRSPort;
};

StoreStruct storage = {
  '$',
  "YourSSID",
  "WiFiPassword",
  "PI4RAZ-11",
  64, //12.5 KHz steps, 64 = 144.800
  1,
  20,
  600,
  "99999",
  "/I",
  "5204.44N",
  "00430.24E",
  "PHG3210",
  "Arduino RAZ IGATE /A=000012",
  "rotate.aprs.net",
  14580
};

#ifdef hasLCD
SSD1306  lcd(OLED_ADR, OLED_SDA, OLED_SCL);// i2c ADDR & SDA, SCL on wemos
#endif

void setup() {
	pinMode(OLED_RST,OUTPUT);
	digitalWrite(OLED_RST, LOW); // low to reset OLED
	pinMode(TX_LED,OUTPUT);
	digitalWrite(TX_LED, LOW); // low to reset OLED
	delay(50);
	digitalWrite(OLED_RST, HIGH); // must be high to turn on OLED
	delay(50);
	Wire.begin(4, 15);

	#ifdef hasLCD
	lcd.init();
	lcd.flipScreenVertically();
	lcd.setFont(ArialMT_Plain_10);
	lcd.setTextAlignment(TEXT_ALIGN_LEFT);
	lcd.drawString(0, 0, "APRS IGate");
	lcd.drawString(0, 8, "STARTING");
	lcd.display();
	lcd.setFont(ArialMT_Plain_10);
	#endif
	delay(1000);

	Serial.begin(9600);
	Serial.println("AIRGATE - PA2RDK");
	Modem.begin(9600, SERIAL_8N1, Modem_RX, Modem_TX);
	Modem.setTimeout(2);

	if (!EEPROM.begin(sizeof(storage) /*EEPROM_SIZE*/))
	{
		Serial.println("failed to initialise EEPROM"); while(1);
	}
  if (EEPROM.read(offsetEEPROM) != storage.chkDigit){
		Serial.println(F("Writing defaults"));
		saveConfig();
  }
	loadConfig();
	printConfig();

	#ifdef hasLCD
	lcd.drawString(0, 16, "Wait for setup");
	lcd.display();
	#endif

	Serial.println(F("Type GS to enter setup:"));
  uint32_t setupWaitTime=millis();
  char gs1, gs2;
  while (millis()-setupWaitTime < 10000) {
    if (Serial.available()) {
      gs1 = gs2;
      gs2 = Serial.read();
      if (((gs1 == 'G') || (gs1 == 'g')) &&
          ((gs2 == 'S') || (gs2 == 's'))) {
        Serial.println(F("Setup entered..."));
        #ifdef hasLCD
        lcd.clear();
        lcd.drawString(0, 0,"Setup entered");
        lcd.display();
        #endif
        setSettings(true);
        delay(2000);
      }
    }
  }

	delay(1000);
	setDra(storage.modemChannel, storage.squelch);
	Modem.println(F("AT+DMOSETVOLUME=8"));
	Modem.println(F("AT+DMOSETMIC=8,0"));
	Modem.println(F("AT+SETFILTER=1,1,1"));
	Serial.print(F("DRA Initialized on freq:"));
	Serial.println(float(144000+(storage.modemChannel*12.5))/1000);

  #ifdef hasLCD
  lcd.drawString(0, 24, "Modem set at "+String(float(144000+(storage.modemChannel*12.5))/1000)+ "MHz" );
  lcd.display();
  oledSleepTime=millis();
  #endif

	timer = timerBegin(0, 80, true);                  //timer 0, div 80
	timerAttachInterrupt(timer, &resetModule, true);  //attach callback
	timerAlarmWrite(timer, wdtTimeout * 1000 * 1000, false); //set time in us
	timerAlarmEnable(timer);                          //enable interrupt

	delay(3000);
}

void loop() {
  timerWrite(timer, 0);

  #ifdef hasLCD
  if (millis()-oledSleepTime>storage.oledTimeout*1000) {
    lcd.clear();
    lcd.display();
    oledSleepTime=millis();
  }
  #endif

  if (millis()-txLedDelayTime>TX_LED_DELAY) {
    digitalWrite(TX_LED, LOW);
  }
  
  int buflen = 0;
  if (Modem.available() > 0) {
    buflen = kissHost.processKissInByte(Modem.read());
  }
  
  if (check_connection()){
    if (buflen>0) {
      bool drop;
      if (ax25.parseForIS(kissHost.packet, buflen, &drop)) {
        if (drop) {
          drop_packet();
        }
        else {
          digitalWrite(TX_LED, HIGH);
          txLedDelayTime=millis();
          send_packet();
        }
      }
    }

    if (millis()-lastClientUpdate>storage.updateInterval*1000) {
      updateGatewayonAPRS();
    }
    
    //  If connected to APRS-IS, read any response from APRS-IS and display it.
    //  Buffer 80 characters at a time in case printing a character at a time is slow.
    receive_data();
    int b = 0;
    while (Serial.available() > 0) {
      b = Serial.read();
      Modem.write(b);
    }
  }
}

// See http://www.aprs-is.net/Connecting.aspx
boolean check_connection() {
	if (WiFi.status() !=WL_CONNECTED || !client.connected()) {
		InitConnection();
	}
	return client.connected();
}

void receive_data() {
	if (client.available()) {
		char rbuf[81];
		int i = 0;
		while (i < 80 && client.available()) {
			char c = client.read();
			rbuf[i++] = c;
			if (c == '\n') break;
		}
    rbuf[i] = 0;
 // Serial.println(rbuf);
  }
}

void drop_packet() {
  Serial.print("d ");
  Serial.println(ax25.isPacket);
  #ifdef hasLCD
  lcd.clear();
  lcd.drawString(0, 0, "Drop packet");
  lcd.drawStringMaxWidth(0,16, 200, ax25.isPacket);
  lcd.display();
  oledSleepTime=millis();
  #endif
}

void send_packet() {
  Serial.print("R ");
  Serial.println(ax25.isPacket);
  #ifdef hasLCD
  lcd.clear();
  lcd.drawString(0, 0, "Send packet");
  lcd.drawStringMaxWidth(0,16, 200, ax25.isPacket);
  lcd.display();
  oledSleepTime=millis();
  #endif
  client.println(ax25.isPacket);
  client.println();
}

void InitConnection() {
	Serial.println("++++++++++++++");
	Serial.println("Initialize connection");

  #ifdef hasLCD
  lcd.clear();
  lcd.drawString(0, 0, "Connecting to WiFi" );
  lcd.display();
  oledSleepTime=millis();
  #endif

	if (WiFi.status() != WL_CONNECTED){
		Serial.println("Connecting to WiFi");
		WlanReset();
		WiFi.begin(storage.wifiSSID, storage.pass);
		int agains=1;
		while ((WiFi.status() != WL_CONNECTED) && (agains < 20)){
			Serial.print(".");
			delay(1000);
			agains++;
		}
		//WlanStatus();
	}

	if (WlanStatus()==WL_CONNECTED){
    #ifdef hasLCD
    lcd.drawString(0, 8, "Connected to:");
    lcd.drawString(70,8, storage.wifiSSID);
    lcd.display();
    oledSleepTime=millis();
    #endif
    Serial.println("++++++++++++++");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");

		if (!client.connected()) {
			Serial.println("Connecting...");
			if (client.connect(storage.AprsIsAddress, storage.APRSPort)) {
				// Log in

				client.print("user ");
				client.print(storage.callSign);
				client.print(" pass ");
				client.print(storage.passCode);
				client.println(" vers " VERSION);

				updateGatewayonAPRS();

				Serial.println("Connected");

        #ifdef hasLCD
        lcd.drawString(0, 16, "Con. APRS-IS:");
        lcd.drawString(70,16, storage.callSign);
        lcd.display();
        oledSleepTime=millis();
        #endif
      }
      else {
        Serial.println("Failed");

        #ifdef hasLCD
        lcd.drawString(0, 16, "Conn. to APRS-IS Failed" );
        lcd.display();
        oledSleepTime=millis();
        #endif
        // if still not connected, delay to prevent constant attempts.
        delay(1000);
      }
		}
	}
  oledSleepTime=millis();
}

void updateGatewayonAPRS(){
  #ifdef hasLCD
  lcd.clear();
  lcd.drawString(0, 0, "Update IGate info");
  lcd.display();
  oledSleepTime=millis();
  #endif
  if (client.connected()){
    Serial.println("Update IGate info on APRS");
    client.print(storage.callSign);
    client.print(">APRAZ1,TCPIP*:!");
    client.print(storage.latitude);
    client.print(storage.symbol[0]);
    client.print(storage.longitude);
    client.print(storage.symbol[1]);
    client.print(storage.PHG);
    client.println(storage.beaconText);
    lastClientUpdate = millis();
  };
}

void WlanReset() {
	WiFi.persistent(false);
	WiFi.disconnect();
	WiFi.mode(WIFI_OFF);
	WiFi.mode(WIFI_STA);
	delay(1000);
}

int WlanStatus() {
	switch (WiFi.status()) {
	case WL_CONNECTED:
		Serial.print(F("WlanCStatus:: CONNECTED to:"));				// 3
		Serial.println(WiFi.SSID());
		WiFi.setAutoReconnect(true);				// Reconenct to this AP if DISCONNECTED
		return(3);
		break;

		// In case we get disconnected from the AP we loose the IP address.
		// The ESP is configured to reconnect to the last router in memory.
	case WL_DISCONNECTED:
		Serial.print(F("WlanStatus:: DISCONNECTED, IP="));			// 6
		Serial.println(WiFi.localIP());
		return(6);
		break;

		// When still pocessing
	case WL_IDLE_STATUS:
		Serial.println(F("WlanStatus:: IDLE"));					// 0
		return(0);
		break;

		// This code is generated as soonas the AP is out of range
		// Whene detected, the program will search for a better AP in range
	case WL_NO_SSID_AVAIL:
		Serial.println(F("WlanStatus:: NO SSID"));					// 1
		return(1);
		break;

	case WL_CONNECT_FAILED:
		Serial.println(F("WlanStatus:: FAILED"));					// 4
		return(4);
		break;

		// Never seen this code
	case WL_SCAN_COMPLETED:
		Serial.println(F("WlanStatus:: SCAN COMPLETE"));			// 2
		return(2);
		break;

		// Never seen this code
	case WL_CONNECTION_LOST:
		Serial.println(F("WlanStatus:: LOST"));					// 5
		return(5);
		break;

		// This code is generated for example when WiFi.begin() has not been called
		// before accessing WiFi functions
	case WL_NO_SHIELD:
		Serial.println(F("WlanStatus:: WL_NO_SHIELD"));				//255
		return(255);
		break;

	default:
		break;
	}
	return(-1);
}

void setDra(byte freq, byte squelch) {
  char buff[50];

  int frMhz = int(freq/80)+4;
  int frkHzPart;
  if (freq>79) {
    frkHzPart = freq-80;
  }
  else {
    frkHzPart=freq;
  }
  frkHzPart *=125; 

  sprintf(
    buff, "AT+DMOSETGROUP=0,14%01d.%04d,14%01d.%04d,0000,%1d,0000", 
    frMhz, frkHzPart, frMhz, frkHzPart, squelch
  );
  Serial.println();
  Serial.println(buff);
  Modem.println(buff);
}

void setSettings(bool doSet) {
	int i = 0;
	receivedString[0] = 'X';

	Serial.print(F("Wifi SSID ("));
	Serial.print(storage.wifiSSID);
	Serial.print(F("):"));
	if (doSet == 1) {
		getStringValue(24);
		if (receivedString[0] != 0) {
			storage.wifiSSID[0] = 0;
			strcat(storage.wifiSSID, receivedString);
		}
	}
	Serial.println();

	Serial.print(F("WiFi password ("));
	Serial.print(storage.pass);
	Serial.print(F("):"));
	if (doSet == 1) {
		getStringValue(63);
		if (receivedString[0] != 0) {
			storage.pass[0] = 0;
			strcat(storage.pass, receivedString);
		}
	}
	Serial.println();

	Serial.print(F("Callsign (+SSID) ("));
	Serial.print(storage.callSign);
	Serial.print(F("):"));
	if (doSet == 1) {
		getStringValue(9);
		if (receivedString[0] != 0) {
			storage.callSign[0] = 0;
			strcat(storage.callSign, receivedString);
		}
	}
	Serial.println();

	Serial.print(F("Modem channel (12.5 KHz steps, 64 = 144.800)("));
	Serial.print(storage.modemChannel);
	Serial.print(F("): "));
	if (doSet == 1) {
		i = get32NumericValue();
		if (receivedString[0] != 0) storage.modemChannel = i;
	}
	Serial.println();

  Serial.print(F("squelch (0..8 0=open)("));
  Serial.print(storage.squelch);
  Serial.print(F("): "));
  if (doSet == 1) {
    i = get32NumericValue();
    if (receivedString[0] != 0) {
      if ((i >= SQ_MIN) && (i <= SQ_MAX)) {
        storage.squelch = i;
      }
    }
  }
  Serial.println();

	Serial.print(F("Oled timeout (seconds)("));
	Serial.print(storage.oledTimeout);
	Serial.print(F("): "));
	if (doSet == 1) {
		i = get32NumericValue();
		if (receivedString[0] != 0) storage.oledTimeout = i;
	}
	Serial.println();

	Serial.print(F("Update interval (minutes)("));
	Serial.print(storage.updateInterval);
	Serial.print(F("): "));
	if (doSet == 1) {
		i = get32NumericValue();
		if (receivedString[0] != 0) storage.updateInterval = i;
	}
	Serial.println();

	Serial.print(F("Passcode ("));
	Serial.print(storage.passCode);
	Serial.print(F("):"));
	if (doSet == 1) {
		getStringValue(5);
		if (receivedString[0] != 0) {
			storage.passCode[0] = 0;
			strcat(storage.passCode, receivedString);
		}
	}
	Serial.println();

  Serial.print(F("Symbol ("));
  Serial.print(storage.symbol);
  Serial.print(F("):"));
  if (doSet == 1) {
    getStringValue(2);
    if (receivedString[0] != 0) {
      storage.symbol[0] = 0;
      strcat(storage.symbol, receivedString);
    }
  }
  Serial.println();

	Serial.print(F("Latitude ("));
	Serial.print(storage.latitude);
	Serial.print(F("):"));
	if (doSet == 1) {
		getStringValue(8);
		if (receivedString[0] != 0) {
			storage.latitude[0] = 0;
			strcat(storage.latitude, receivedString);
		}
	}
	Serial.println();

	Serial.print(F("Longitude ("));
	Serial.print(storage.longitude);
	Serial.print(F("):"));
	if (doSet == 1) {
		getStringValue(9);
		if (receivedString[0] != 0) {
			storage.longitude[0] = 0;
			strcat(storage.longitude, receivedString);
		}
	}
	Serial.println();

	Serial.print(F("PHG ("));
	Serial.print(storage.PHG);
	Serial.print(F("):"));
	if (doSet == 1) {
		getStringValue(7);
		if (receivedString[0] != 0) {
			storage.PHG[0] = 0;
			strcat(storage.PHG, receivedString);
		}
	}
	Serial.println();

  Serial.print(F("beacon text ("));
  Serial.print(storage.beaconText);
  Serial.print(F("):"));
  if (doSet == 1) {
    getStringValue(36);
    if (receivedString[0] != 0) {
      storage.beaconText[0] = 0;
      strcat(storage.beaconText, receivedString);
    }
  }
  Serial.println();

	Serial.print(F("APRS-IS address ("));
	Serial.print(storage.AprsIsAddress);
	Serial.print(F("):"));
	if (doSet == 1) {
		getStringValue(24);
		if (receivedString[0] != 0) {
			storage.AprsIsAddress[0] = 0;
			strcat(storage.AprsIsAddress, receivedString);
		}
	}
	Serial.println();

	Serial.print(F("APRS port("));
	Serial.print(storage.APRSPort);
	Serial.print(F("): "));
	if (doSet == 1) {
		i = get32NumericValue();
		if (receivedString[0] != 0) storage.APRSPort = i;
	}
	Serial.println();

	if (doSet == 1) {
		saveConfig();
		loadConfig();
	}
}

void getStringValue(int length) {
	SerialFlush();
	receivedString[0] = 0;
	int i = 0;
	while (receivedString[i] != 13 && i < length) {
		if (Serial.available() > 0) {
			receivedString[i] = Serial.read();
			if (receivedString[i] == 13 || receivedString[i] == 10) {
				i--;
			}
			else {
				Serial.write(receivedString[i]);
			}
			i++;
		}
	}
	receivedString[i] = 0;
	SerialFlush();
}

byte getCharValue() {
	SerialFlush();
	receivedString[0] = 0;
	int i = 0;
	while (receivedString[i] != 13 && i < 2) {
		if (Serial.available() > 0) {
			receivedString[i] = Serial.read();
			if (receivedString[i] == 13 || receivedString[i] == 10) {
				i--;
			}
			else {
				Serial.write(receivedString[i]);
			}
			i++;
		}
	}
	receivedString[i] = 0;
	SerialFlush();
	return receivedString[i - 1];
}

uint32_t get32NumericValue() {
	SerialFlush();
	uint32_t myByte = 0;
	byte inChar = 0;
	bool isNegative = false;
	receivedString[0] = 0;

	int i = 0;
	while (inChar != 13) {
		if (Serial.available() > 0) {
			inChar = Serial.read();
			if (inChar > 47 && inChar < 58) {
				receivedString[i] = inChar;
				i++;
				Serial.write(inChar);
				myByte = (myByte * 10) + (inChar - 48);
			}
			if (inChar == 45) {
				Serial.write(inChar);
				isNegative = true;
			}
		}
	}
	receivedString[i] = 0;
	if (isNegative == true) myByte = myByte * -1;
	SerialFlush();
	return myByte;
}

uint16_t get16NumericValue() {
	SerialFlush();
	uint16_t myByte = 0;
	byte inChar = 0;
	bool isNegative = false;
	receivedString[0] = 0;

	int i = 0;
	while (inChar != 13) {
		if (Serial.available() > 0) {
			inChar = Serial.read();
			if (inChar > 47 && inChar < 58) {
				receivedString[i] = inChar;
				i++;
				Serial.write(inChar);
				myByte = (myByte * 10) + (inChar - 48);
			}
			if (inChar == 45) {
				Serial.write(inChar);
				isNegative = true;
			}
		}
	}
	receivedString[i] = 0;
	if (isNegative == true) myByte = myByte * -1;
	SerialFlush();
	return myByte;
}

byte getNumericValue() {
	SerialFlush();
	byte myByte = 0;
	byte inChar = 0;
	bool isNegative = false;
	receivedString[0] = 0;

	int i = 0;
	while (inChar != 13) {
		if (Serial.available() > 0) {
			inChar = Serial.read();
			if (inChar > 47 && inChar < 58) {
				receivedString[i] = inChar;
				i++;
				Serial.write(inChar);
				myByte = (myByte * 10) + (inChar - 48);
			}
			if (inChar == 45) {
				Serial.write(inChar);
				isNegative = true;
			}
		}
	}
	receivedString[i] = 0;
	if (isNegative == true) myByte = myByte * -1;
	SerialFlush();
	return myByte;
}

void saveConfig() {
  for (unsigned int t = 0; t < sizeof(storage); t++)
    EEPROM.write(offsetEEPROM + t, *((char*)&storage + t));
  EEPROM.commit();
}

void loadConfig() {
  if (EEPROM.read(offsetEEPROM + 0) == storage.chkDigit)
    for (unsigned int t = 0; t < sizeof(storage); t++)
      *((char*)&storage + t) = EEPROM.read(offsetEEPROM + t);
}

void printConfig() {
  if (EEPROM.read(offsetEEPROM + 0) == storage.chkDigit)
    for (unsigned int t = 0; t < sizeof(storage); t++)
      Serial.write(EEPROM.read(offsetEEPROM + t));

  Serial.println();
  setSettings(0);
}

void SerialFlush() {
  for (int i = 0; i < 10; i++)
  {
    while (Serial.available() > 0) {
      Serial.read();
    }
  }
}
