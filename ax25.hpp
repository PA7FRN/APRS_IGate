#ifndef AX25_H
#define AX25_H

#define UI_FRAME_CONTROL_FIELD  0x03
#define UI_FRAME_PID            0xF0
#define SSID_MASK               0x0F
#define ASCII_BASE              0x30
#define FLAG_MASK               0x40

#define BUFFERSIZE 400

class AX25 {
  public:
    AX25(int i);
    bool parseForIS(char* uiFrame, int size, bool* drop);
    char isPacket[BUFFERSIZE];
  private:
    int tranlateAddressToIS(char* uiFrame, int uiStartPos, int isPos, bool markflag);
};
	
#endif
