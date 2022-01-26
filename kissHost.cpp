#include "kissHost.hpp"

KissHost::KissHost(int i) {
}

int KissHost::processKissInByte(char newByte) {
  switch (_kissInState) {
    case KS_UNDEF:
      if (newByte == FEND) {
        _kissInState = KS_GET_CMD;
      }
      break;
    case KS_GET_CMD:
      packetSize = 0;
      if (newByte != FEND) {
        if (newByte == 0) {
          _kissInState = KS_GET_DATA;
        }
        else {
          _kissInState = KS_UNDEF;
        }
      }
      break;
    case KS_GET_DATA:
      if (newByte == FESC) {
        _kissInState = KS_ESCAPE;
      }
      else if (newByte == FEND) {
        _readAddress = true;
        setPacketReady();
        _kissInState = KS_GET_CMD;
      }
      else {
        storeDataByte(newByte);
      }
      break;
    case KS_ESCAPE:
      if (newByte == TFEND) {
        storeDataByte(FEND);
        _kissInState = KS_GET_DATA;
      }
      else if (newByte == TFESC) {
        storeDataByte(FESC);
        _kissInState = KS_GET_DATA;
      }
      else {
		    clearPacket();
        _kissInState = KS_UNDEF;
      }
      break;
  }
  return packetSize;
}

void KissHost::storeDataByte(char newByte) {
  if (_packetByteIdx < BUFFERSIZE) {
    if (_readAddress) {
      _readAddress = (newByte & 0x01) != 0x01;
      newByte = byteBitShift(newByte, 1);
    }
    packet[_packetByteIdx] = newByte;
    _packetByteIdx++;
  }
}

char KissHost::byteBitShift(char val, int bitCount) {
  int intByte = val;
  intByte &= 0x00FF;
  intByte >>= bitCount; 
  return char(intByte);
}

void KissHost::clearPacket() {
  _packetByteIdx = 0;
}

void KissHost::setPacketReady() {
  packet[_packetByteIdx] = 0;
  packetSize = _packetByteIdx+1;
  _packetByteIdx = 0;
}
