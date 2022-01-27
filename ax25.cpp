#include "ax25.hpp"

AX25::AX25(int i) {
}

bool AX25::parseForIS(char* uiFrame, int size) {
    int controlFieldPos = 0;
    int pIdx=0;
    while ((pIdx<size-1) && (controlFieldPos==0)) {
      if (uiFrame[pIdx] == UI_FRAME_CONTROL_FIELD) {
        if (uiFrame[pIdx+1] == UI_FRAME_PID) {
          controlFieldPos = pIdx;
        }
      }
      pIdx++;
    }
    
    int checkPos = 13;
    
    bool controlFielOk = (controlFieldPos > checkPos) && 
                         (controlFieldPos < (size - 2));
    if (!controlFielOk) {
      return false;
    }

    if (uiFrame[controlFieldPos + 2] == '}') {
      return false;
    }
   
    int isPos = 0;
    
    isPos = tranlateAddressToIS(uiFrame, 7, isPos, false);
    isPacket[isPos] = '>';
    isPos++;
    isPos = tranlateAddressToIS(uiFrame, 0, isPos, false);

    checkPos += 7;
    int uiStartPos = 14;
    int i = 0;
    while ((controlFieldPos > checkPos) && i < 8) {
      isPacket[isPos] = ',';
      isPos++;
      isPos = tranlateAddressToIS(uiFrame, uiStartPos, isPos, true);
      checkPos   += 7;
      uiStartPos += 7;
	  i++;
    }
    
    isPacket[isPos] = ':';
    isPos++;
    
    uiStartPos += 2;
    while (uiStartPos < size) {
      isPacket[isPos] = (char)uiFrame[uiStartPos];
      isPos++;
      uiStartPos++;
    }

    while (isPos < BUFFERSIZE) {
      isPacket[isPos] = '\0';
      isPos++;
    }

    return true;
}

int AX25::tranlateAddressToIS(char* uiFrame, int uiStartPos, int isPos, bool markflag) {
    int pIdx=0;
    int endPos = uiStartPos+6;
    for (pIdx=uiStartPos; pIdx<endPos; pIdx++){
      if (uiFrame[pIdx]!=' ') {
        isPacket[isPos]=(char)uiFrame[pIdx];
        isPos++;
      }
    }

    char btSsid = uiFrame[endPos] & SSID_MASK; 
    if (btSsid > 0) {
      isPacket[isPos]='-';
      isPos++;
      if (btSsid > 9) {
        isPacket[isPos]='1';
        isPos++;
        btSsid -= 10;
      }
      isPacket[isPos] = btSsid + ASCII_BASE;
      isPos++;
    }

    if (markflag) {
	    if ((uiFrame[endPos] & FLAG_MASK) > 0) {
        isPacket[isPos] = '*';
        isPos++;
	    }
    }

    return isPos;
}
