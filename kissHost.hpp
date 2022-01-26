#ifndef KISS_HOST_H
#define KISS_HOST_H

#define FEND  0xC0
#define FESC  0xDB
#define TFEND 0xDC
#define TFESC 0xDD

#define KS_UNDEF    0
#define KS_GET_CMD  1
#define KS_GET_DATA 2
#define KS_ESCAPE   3

#define BUFFERSIZE 260

class KissHost {
  public:
    KissHost(int i);
    int processKissInByte(char newByte);
    char packet[BUFFERSIZE+1];
    int  packetSize = 0;
  private:
    int  _kissInState = KS_UNDEF;
    bool _kissOutFrameEnd = true;
    bool _readAddress = true;
    int  _packetByteIdx = 0;
    void storeDataByte(char newByte);
    char byteBitShift(char val, int bitCount);
    void clearPacket();
    void setPacketReady();
};
	
#endif
