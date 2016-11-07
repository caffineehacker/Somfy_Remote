#include <Bridge.h>
#include <Console.h>
#include <FileIO.h>
#include <HttpClient.h>
#include <Mailbox.h>
#include <Process.h>
#include <YunClient.h>
#include <YunServer.h>

/*   This sketch allows you to emulate a Somfy RTS or Simu HZ remote.
   If you want to learn more about the Somfy RTS protocol, check out https://pushstack.wordpress.com/somfy-rts-protocol/
   
   The rolling code will be stored in EEPROM, so that you can power the Arduino off.
   
   Easiest way to make it work for you:
    - Choose a remote number
    - Choose a starting point for the rolling code. Any unsigned int works, 1 is a good start
    - Upload the sketch
    - Long-press the program button of YOUR ACTUAL REMOTE until your blind goes up and down slightly
    - send 'p' to the serial terminal
  To make a group command, just repeat the last two steps with another blind (one by one)
  
  Then:
    - m, u or h will make it to go up
    - s make it stop
    - b, or d will make it to go down
    - you can also send a HEX number directly for any weird command you (0x9 for the sun and wind detector for instance)
*/

#include <EEPROM.h>
#define PORT_TX 5 //5 of PORTD = DigitalPin 5

#define SYMBOL 640
#define HAUT 0x2
#define STOP 0x1
#define BAS 0x4
#define PROG 0x8

#define NEW_ROLLING_CODE 0x101

YunServer server;

typedef struct Remote {
  unsigned long remoteNumber;
  unsigned int rollingCode;
} Remote;

Remote currentRemote;

int currentRemoteNumber = 0;

byte frame[7];
byte checksum;

void BuildFrame(byte *frame, byte button);
void SendCommand(byte *frame, byte sync);
void InitializeRemote();
void LoadRemote();
void SaveRemote();


void setup() {
  Serial.begin(115200);
  pinMode(PORT_TX, OUTPUT);
  digitalWrite(PORT_TX, LOW);
  
  // Initialize the bridge
  Serial.println("Initializing Bridge");
  // Pin 13 typically has an LED attached
  pinMode(13, OUTPUT);
  digitalWrite(13, LOW);
  Bridge.begin();
  digitalWrite(13, HIGH);
  Serial.println("Bridge Initialized");
  
  server.listenOnLocalhost();
  server.begin();
  
  randomSeed(analogRead(0));
}

void loop() {
  YunClient client = server.accept();
  
  if (client) {
    String command = client.readStringUntil('/');
    ExecuteCommand(command[0]);
    client.stop();
  }
  
  if (Serial.available() > 0) {
    char serie = (char)Serial.read();
    ExecuteCommand(serie);
  }
  
  delay(50);
}

void ExecuteCommand(char command)
{
  Serial.println("");
    if(command == 'm'||command == 'u'||command == 'h') {
      Serial.println("Monte"); // Somfy is a French company, after all.
      BuildFrame(frame, HAUT);
    }
    else if(command == 's') {
      Serial.println("Stop");
      BuildFrame(frame, STOP);
    }
    else if(command == 'b'||command == 'd') {
      Serial.println("Descend");
      BuildFrame(frame, BAS);
    }
    else if(command == 'p') {
      Serial.println("Prog");
      BuildFrame(frame, PROG);
    }
    else if(command >= '0' && command <= '9') {
      currentRemoteNumber = command - '0';
      
      EEPROM.get(currentRemoteNumber * sizeof(Remote), currentRemote);
      if (currentRemote.rollingCode < NEW_ROLLING_CODE) {
        InitializeRemote();
      }
      
      Serial.println("Changed to remote #:");
      Serial.println(currentRemoteNumber);
    }
    else {
      Serial.println("Custom code");
      BuildFrame(frame, command);
    }

    Serial.println("");
    SendCommand(frame, 2);
    for(int i = 0; i<2; i++) {
      SendCommand(frame, 7);
    }
}


void BuildFrame(byte *frame, byte button) {
  unsigned int code = currentRemote.rollingCode;
  unsigned long remote = currentRemote.remoteNumber;
  frame[0] = 0xA7; // Encryption key. Doesn't matter much
  frame[1] = button << 4;  // Which button did  you press? The 4 LSB will be the checksum
  frame[2] = code >> 8;    // Rolling code (big endian)
  frame[3] = code;         // Rolling code
  frame[4] = remote >> 16; // Remote address
  frame[5] = remote >>  8; // Remote address
  frame[6] = remote;       // Remote address

  Serial.print("Frame         : ");
  for(byte i = 0; i < 7; i++) {
    if(frame[i] >> 4 == 0) { //  Displays leading zero in case the most significant
      Serial.print("0");     // nibble is a 0.
    }
    Serial.print(frame[i],HEX); Serial.print(" ");
  }
  
// Checksum calculation: a XOR of all the nibbles
  checksum = 0;
  for(byte i = 0; i < 7; i++) {
    checksum = checksum ^ frame[i] ^ (frame[i] >> 4);
  }
  checksum &= 0b1111; // We keep the last 4 bits only


//Checksum integration
  frame[1] |= checksum; //  If a XOR of all the nibbles is equal to 0, the blinds will
                        // consider the checksum ok.

  Serial.println(""); Serial.print("With checksum : ");
  for(byte i = 0; i < 7; i++) {
    if(frame[i] >> 4 == 0) {
      Serial.print("0");
    }
    Serial.print(frame[i],HEX); Serial.print(" ");
  }

  
// Obfuscation: a XOR of all the bytes
  for(byte i = 1; i < 7; i++) {
    frame[i] ^= frame[i-1];
  }

  Serial.println(""); Serial.print("Obfuscated    : ");
  for(byte i = 0; i < 7; i++) {
    if(frame[i] >> 4 == 0) {
      Serial.print("0");
    }
    Serial.print(frame[i],HEX); Serial.print(" ");
  }
  Serial.println("");
  Serial.print("Rolling Code  : "); Serial.println(code);
  currentRemote.rollingCode++;
  SaveRemote();
}

void SendCommand(byte *frame, byte sync) {
  if(sync == 2) { // Only with the first frame.
  //Wake-up pulse & Silence
    digitalWrite(PORT_TX, HIGH);
    delayMicroseconds(9415);
    digitalWrite(PORT_TX, LOW);
    delayMicroseconds(89565);
  }

// Hardware sync: two sync for the first frame, seven for the following ones.
  for (int i = 0; i < sync; i++) {
    digitalWrite(PORT_TX, HIGH);
    delayMicroseconds(4*SYMBOL);
    digitalWrite(PORT_TX, LOW);
    delayMicroseconds(4*SYMBOL);
  }

// Software sync
  digitalWrite(PORT_TX, HIGH);
  delayMicroseconds(4550);
  digitalWrite(PORT_TX, LOW);
  delayMicroseconds(SYMBOL);
  
  
//Data: bits are sent one by one, starting with the MSB.
  for(byte i = 0; i < 56; i++) {
    if(((frame[i/8] >> (7 - (i%8))) & 1) == 1) {
      digitalWrite(PORT_TX, LOW);
      delayMicroseconds(SYMBOL);
      digitalWrite(PORT_TX, HIGH);
      delayMicroseconds(SYMBOL);
    }
    else {
      digitalWrite(PORT_TX, HIGH);
      delayMicroseconds(SYMBOL);
      digitalWrite(PORT_TX, LOW);
      delayMicroseconds(SYMBOL);
    }
  }
  
  digitalWrite(PORT_TX, LOW);
  delayMicroseconds(30415); // Inter-frame silence
}

void InitializeRemote()
{
  currentRemote.rollingCode = NEW_ROLLING_CODE;
  currentRemote.remoteNumber = random(0xFFFFFF);
  SaveRemote();
}

void LoadRemote()
{
  EEPROM.put(currentRemoteNumber * sizeof(Remote), currentRemote);
}

void SaveRemote()
{
  EEPROM.get(currentRemoteNumber * sizeof(Remote), currentRemote);
}
