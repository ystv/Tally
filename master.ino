#define BUFSIZ 255

#include <EEPROM.h>
#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>

#include <SkaarhojPgmspace.h>

#include <ATEMbase.h>
#include <ATEMext.h>

int dio[] = {A0, A1, A2, A3, 7, A5, A4, 6};
int tState[8];
int mapping[64];
int state = 0;

bool qReset = false;

// DE - YTSV - 00
byte mac[] = { 0xDE, 0x59, 0x53, 0x54, 0x56, 0x00 };
EthernetServer server(80);  // create a server at port 80
ATEMext AtemSwitcher;

bool dhcp = true;
IPAddress ip;
IPAddress atemip;

void setup() {
	Serial.begin(9600);
	
	for (int i = 0; i < 8; i++) {
		pinMode(dio[i], OUTPUT);
	}
	
	ip[0] = EEPROM.read(0);
	ip[1] = EEPROM.read(1);
	ip[2] = EEPROM.read(2);
	ip[3] = EEPROM.read(3);
	
	dhcp = EEPROM.read(4) != 0;
	
	atemip[0] = EEPROM.read(5+0);
	atemip[1] = EEPROM.read(5+1);
	atemip[2] = EEPROM.read(5+2);
	atemip[3] = EEPROM.read(5+3);

	for (int i = 0; i < 64; i++) {
		mapping[i] = EEPROM.read(9+i);
	}
	
	Serial.println("Initializing network");
	if (dhcp) {
		if (Ethernet.begin(mac) == 0) {
			Serial.println("ERROR - DHCP failed!");
			return;
		}
		ip = Ethernet.localIP();
	} else {
		Ethernet.begin(mac, ip);
	}
	server.begin();
	
	AtemSwitcher.begin(atemip, 56417);
	AtemSwitcher.serialOutput(0x80);
	AtemSwitcher.connect();
	
	Serial.println("Initializing SD card...");
	if (!SD.begin(4)) {
		Serial.println("ERROR - SD card initialization failed!");
		return;
	}
	Serial.println("SUCCESS - SD card initialized.");
	
	if (!SD.exists("index.htm")) {
		Serial.println("ERROR - Can't find index.htm file!");
		return;
	}
	Serial.println("SUCCESS - Found index.htm file.");
}

void loop() {
	AtemSwitcher.runLoop();
	webServer();
	updateXBEE();
	
	if (qReset) {
		reset();
	}
}

bool getProgramTally(int source) {
	return (AtemSwitcher.getTallyByIndexTallyFlags(source - 1) & 1) > 0 ? true : false;
}

void updateXBEE() {
	int addr = (state % 64);
	state++;
	updateTally(addr, getProgramTally(mapping[addr]));

	for (int i = 0; i < 8; i++) {
		if (getProgramTally(i + 1) != tState[i]) {
			tState[i] = getProgramTally(i + 1);
			for (int j = 0; j < 64; j++) {
				if (mapping[j] == i + 1) {
					updateTally(j, tState[i]);
				}
			}
		}
	}
}

void updateTally(int addr, int state) {
	digitalWrite(dio[0], state);
	digitalWrite(dio[7], HIGH);
	for (int i = 0; i < 6; i++) {
		digitalWrite(dio[i + 1], addr & (00000001 << i));
	}
	delay(30);
	digitalWrite(dio[7], LOW);
	delay(30);
}

void reset() {
	asm volatile ("  jmp 0"); 
}

void setValues(String post) {
	int a = post.indexOf("&");
	String var_ = post.substring(0, a);
	
	int b = var_.indexOf("=");
	String val_ = var_.substring(b + 1);
	var_ = var_.substring(0, b);
	
	setValue(var_, val_);
	
	if (a != -1) {
		setValues(post.substring(a + 1));
	}
}

IPAddress stringToIp(String ip) {
	IPAddress ip_;
	for (int i = 0; i < 3; i++) {
		int a = ip.indexOf(".");
		byte b = ip.substring(0, a).toInt();
		ip_[i] = b;
		ip = ip.substring(a + 1);
	}
	ip_[3] = ip.toInt();
	return ip_;
}

void setValue(String var, String val) {
	if (var == "dhcp") {
		EEPROM.write(4, val.toInt());
		dhcp = val.toInt() != 0;
	} else if (var == "ip") {
		ip = stringToIp(val);
		
		EEPROM.write(0, ip[0]);
		EEPROM.write(1, ip[1]);
		EEPROM.write(2, ip[2]);
		EEPROM.write(3, ip[3]);
	} else if (var == "atemip") {
		atemip = stringToIp(val);
		
		EEPROM.write(5+0, atemip[0]);
		EEPROM.write(5+1, atemip[1]);
		EEPROM.write(5+2, atemip[2]);
		EEPROM.write(5+3, atemip[3]);
		
		qReset = true;
	} else if (var.substring(0, 3) == "map") {
		int addr = var.substring(3).toInt();
		EEPROM.write(9+addr, val.toInt());
		mapping[addr] = val.toInt();
		updateTally(addr, getProgramTally(val.toInt()));
	}
}

void doTemplate(char line[], EthernetClient client) {
	String line_ = String(line);
	int a = line_.indexOf("{{");
	while (a >= 0) {
		int b = line_.indexOf("}}");
		String temp = line_.substring(a + 2, b);
		String args = "";
		
		int c = temp.indexOf(":");
		if (c >= 0) {
			args = temp.substring(c + 1);
			temp = temp.substring(0, c);
		}
		
		String result = "";
		if (temp == "state") {
			result = getProgramTally(args.toInt()) ? "ON" : "OFF";
		} else if (temp == "name") {
			result = AtemSwitcher.getInputShortName(args.toInt());
		} else if (temp == "map") {
			result = String(mapping[args.toInt()]);
		} else if (temp == "ip") {
			String dot = ".";
			result = ip[0] + dot + ip[1] + dot + ip[2] + dot + ip[3];
		} else if (temp == "file") {
			char args_[BUFSIZ];
			args.toCharArray(args_, BUFSIZ);
			sendFile(args_, client);
		} else if (temp == "mac") {
			result = "DE:59:53:54:56:00";
		} else if (temp == "atemip") {
			String dot = ".";
			result = atemip[0] + dot + atemip[1] + dot + atemip[2] + dot + atemip[3];
		} else if (temp == "dhcp") {
			result = (0 != args.toInt()) == dhcp ? "checked" : "";
		}
		line_ = (line_.substring(0, a) + result + line_.substring(b + 2));
		line_.toCharArray(line, BUFSIZ);
		a = line_.indexOf("{{");
	}
}

void sendFile(char* file, EthernetClient client) {
	File webFile;
	if (webFile = SD.open(file)) {
		int index = 0;
		char clientline[BUFSIZ];
		while(webFile.available()) {
			char c = webFile.read();
			
			if (c == '\r') {
				client.write(c);
				c = webFile.read();
			}
			
			if (c != '\n') {
				clientline[index++] = c;
				if (index < BUFSIZ)
				continue;
			}
			clientline[index] = 0;
			
			doTemplate(clientline, client);
			client.write(clientline); // send web page to client
			client.write(c);
			
			index = 0;
			clientline[0] = 0;
		}
		webFile.close();
	}
}

void webServer() {
	char clientline[BUFSIZ];
	int index = 0;
	String file = "404.htm";
	int type = 0;
	int contentLen = 0;
	
	EthernetClient client = server.available();  // try to get client
	if (client) {  // got client?
		while (client.connected()) {
			if (client.available()) {   // client data available to read
				char c = client.read();
				
				if (c == '\r') {
					c = client.read();
				}
				
				if (c != '\n') {
					clientline[index++] = c;
					
					if (index >= BUFSIZ)
						index = BUFSIZ -1;
					
					continue;
				}
				
				clientline[index] = 0;
				
				if (String(clientline).indexOf("GET / ") != -1) {
					file = "index.htm";
				} else if (String(clientline).indexOf("GET /") != -1) {
					file = String(clientline);
					file = file.substring(5, file.lastIndexOf(" HTTP"));
				} else if (String(clientline).indexOf("POST") != -1) {
					file = "index.htm";
					type = 2;
				} else if (String(clientline).indexOf("Content-Length: ") != -1) {
					contentLen = String(clientline).substring(16).toInt();
				}
				
				if (index == 0) {
					if (type == 2) {
						char postvars[contentLen];
						index = 0;
						while (index < contentLen) {
							c = client.read();
							postvars[index++] = c;
						}
						postvars[index] = 0;
						setValues(String(postvars));
						type = 0;
					}
					
					char file_[BUFSIZ];
					file.toCharArray(file_, BUFSIZ);
					
					if (!SD.exists(file_)) {
						file = "404.htm";
					}
					// send a standard http response header
					client.println("HTTP/1.1 200 OK");
					if (file.indexOf(".css") != -1) {
						client.println("Cache-Control: max-age=3600");
						client.println("Content-Type: text/css");
					} else if (file.indexOf(".png") != -1) {
						client.println("Cache-Control: max-age=3600");
						client.println("Content-Type: image/png");
						type = 1;
					} else {
						client.println("Content-Type: text/html");
					}
					client.println("Connection: close");
					client.println();
					
					if (type == 0) {
						sendFile(file_, client);
					} else {
						File webFile;
						if (webFile = SD.open(file_)) {
							while(webFile.available()) {
								client.write(webFile.read());
							}
							webFile.close();
						}
					}
					break;
				}
				index = 0;
				clientline[0] = 0;
			}
		}
		delay(1);
		client.stop(); // close the connection
	}
}