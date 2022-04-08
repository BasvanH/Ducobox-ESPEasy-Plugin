/*
 * Author: Arne Mauer
 * Parts of this code is based on the -library made by 'supersjimmie', 'Thinkpad', 'Klusjesman' and 'jodur'.
 */

#include "DucoCC1101.h"
#include <string.h>
#include <Arduino.h>
#include <SPI.h>




// TODO: 
// - Hoe reageert een bedieningsschakelaar wanneer er een networkcall is voor zijn network address?
// bijvoorbeeld gw address = 5
// Received message: SRC:1; DEST:0; ORG.SRC:1; ORG.DEST:0; Ntwrk:0001f947;Type:0; Bytes:10; Counter:12; RSSI:-68 (0x0D);
// DATA: FF,05,  <<<< 





// default constructor
DucoCC1101::DucoCC1101(uint8_t counter, uint8_t sendTries) : CC1101()
{
	this->outDucoPacket.counter = counter;
	this->sendTries = sendTries;
	this->deviceAddress = 0;
	this->radioPower = 0xC1; // default radio power 0xC1 = 10,3dBm @ 868mhz
	this->waitingForAck = 0; 
	this->ackTimer = 0;
	this->ackRetry = 0;

	this->testCounter = 0; // temp 
	
	this->messageCounter = 1; // for messages out NEVER ZERO!
	//this->lastRssiByte = 0;

	this->numberOfLogmessages = 0;

	this->installerModeActive = false;
	this->temperature = 0;
} //DucoCC1101

// default destructor
DucoCC1101::~DucoCC1101()
{
} //~DucoCC1101

void DucoCC1101::setLogMessage(const __FlashStringHelper* flashString)
{
    String s(flashString);
    setLogMessage(s.c_str());
}

void DucoCC1101::setLogMessage(const char *newLogMessage){
	snprintf(logMessages[this->numberOfLogmessages], sizeof(logMessages[this->numberOfLogmessages]), "%lu - %s", millis(), newLogMessage);

	if(this->numberOfLogmessages == NUMBER_OF_LOG_STRING){
		this->numberOfLogmessages = 0;
	}else{
		this->numberOfLogmessages++;
	}
}

uint8_t DucoCC1101::getNumberOfLogMessages(){
	uint8_t tempNumber = this->numberOfLogmessages;
	this->numberOfLogmessages = 0;
	return tempNumber;
}


void DucoCC1101::reset(){
	writeCommand(CC1101_SRES);
	this->ducoDeviceState = ducoDeviceState_notInitialised;

	// TODO: reset/delete all variables
	return;
}

void DucoCC1101::initReceive()
{
	/*
	Configuration reverse engineered from RFT print.
	
	Base frequency		868.294312  		(868.294312 FREQ2-0 0x21 0x65 0x5C) / (868.292358 FREQ2-0 0x21 0x65 0x57)
	Channel				0
	Channel spacing		199.951172kHz
	Carrier frequency	868.294312MHz  		(868.294312 FREQ2-0 0x21 0x65 0x5C) / (868.292358 FREQ2-0 0x21 0x65 0x57)
	Xtal frequency		26.000000MHz
	Data rate			38.3835kBaud  
	RX filter BW		101.562500kHz 
	Manchester			disabled
	Modulation			GFSK
	Deviation			20.629883 kHz	
	TX power			0xC5,0x00,0x00,0x00,0x00,0x00,0x00,0x00
	PA ramping			disabled
	Whitening			disabled
	*/	
	this->ducoDeviceState = ducoDeviceState_notInitialised;
	writeCommand(CC1101_SRES);

	writeRegister(CC1101_IOCFG0 ,0x2E);	//!!! 	//High impedance (3-state)
	writeRegister(CC1101_IOCFG1 ,0x2E);	//!!    //High impedance (3-state)
	//writeRegister(CC1101_IOCFG2 ,0x07);	//!!! duco 0x07= Asserts when a packet has been received with CRC OK. De-asserts when the first byte is read from the RX FIFO.
	
	writeRegister(CC1101_IOCFG2 ,0x06);	
	/* 6 (0x06) = Asserts when sync word has been sent / received, and de-asserts at the end of the packet. 
	=> In RX, the pin will also deassert when a packet is discarded due to address or maximum length filtering 
      or when the radio enters RXFIFO_OVERFLOW state. 
			=>> on rising -> DO NOTHING
			=>> on falling -> check 

   => In TX the pin will de-assert if the TX FIFO underflows.
	      =>> TODO: disable interrupt
	*/

	writeRegister(CC1101_FIFOTHR, 0x07); // default value. FIFO treshold TX=33;RX=32
	writeRegister(CC1101_SYNC1 ,0xD3);	//duco 0xD3 /   		//message2 byte6
	writeRegister(CC1101_SYNC0 ,0x91);	//duco 0x91	/  	//message2 byte7

	writeRegister(CC1101_PKTLEN ,0x20); // duco 0x20 / maximum packet size is 32 bytes, The packet length is defined as the 
										// payload data, excluding the length byte and the optional CRC.

	/* In variable packet length mode, PKTCTRL0.LENGTH_CONFIG=1, the packet length is configured by the first byte after the
sync word. The packet length is defined as the payload data, excluding the length byte and the optional CRC. The PKTLEN register is
used to set the maximum packet length allowed in RX. Any packet received with a length byte with a value greater than PKTLEN will be discarded.*/


	writeRegister(CC1101_PKTCTRL1 ,0x0C); //duco 0x0C=00001100 /  	//ADR_CHK[1:0]=No address check, APPEND_STATUS= two status bytes will be appended to the payload of the packet. The status bytes contain RSSI and LQI values, as well as CRC OK.; CRC_AUTOFLUSH = Enable automatic flush of RX FIFO when CRC is not OK.
	writeRegister(CC1101_PKTCTRL0 ,0x05); //duco 0x05=00000101 /  		//Variable packet length mode. Packet length configured by the first byte after sync word, CRC calculation in TX and CRC check in RX enabled, No data whitening, Normal mode, use FIFOs for RX and TX
	
	writeRegister(CC1101_ADDR ,0x00); // duco/ NOT USED: Address used for packet filtration. 
	writeRegister(CC1101_CHANNR ,0x00); // duco/

	writeRegister(CC1101_FSCTRL1 ,0x06); // duco/
	writeRegister(CC1101_FSCTRL0 ,0x00); // duco/

	writeRegister(CC1101_FREQ2 ,0x21); // duco/
	writeRegister(CC1101_FREQ1 ,0x65); // duco/
	writeRegister(CC1101_FREQ0 ,0x5C); //  0x6A / duco: 0x5C

	writeRegister(CC1101_MDMCFG4 ,0xCA); // duco 0xCA   / 0xE8 
	writeRegister(CC1101_MDMCFG3 ,0x83); // duco 0x83   / 0x43 
	writeRegister(CC1101_MDMCFG2 ,0x13); // duco 0x13   / 0x00 		//Enable digital DC blocking filter before demodulator, GFSK, Disable Manchester encoding/decoding, No preamble/sync 
	writeRegister(CC1101_MDMCFG1 ,0x22); // duco/		//Disable FEC
	writeRegister(CC1101_MDMCFG0 ,0xF8); // duco/ channel spacing = 199.951 kHz
	
	writeRegister(CC1101_DEVIATN ,0x35); // duco 0x35   / 0x40 

	writeRegister(CC1101_MCSM2 ,0x07);  
	writeRegister(CC1101_MCSM1 ,0x2F); //Next state after finishing packet reception/transmission => RX
	writeRegister(CC1101_MCSM0 ,0x08);  //no auto calibrate // 0x18 / duco 0x08

	writeRegister(CC1101_FOCCFG ,0x16); // duco/
	writeRegister(CC1101_BSCFG ,0x6C); // duco/
	writeRegister(CC1101_AGCCTRL2 ,0x43);  // duco/
	writeRegister(CC1101_AGCCTRL1 ,0x40); // duco/
	writeRegister(CC1101_AGCCTRL0 ,0x91); // duco/

	writeRegister(CC1101_FREND1 ,0x56); // duco/ 0x56
	writeRegister(CC1101_FREND0 ,0x10); // duco 0x10   / 0x17


	writeRegister(CC1101_FSCAL3 ,0xE9); // duco 0xE9 /  0xA9
	writeRegister(CC1101_FSCAL2 ,0x2A); // duco/
	writeRegister(CC1101_FSCAL1 ,0x00); // duco/
	writeRegister(CC1101_FSCAL0 ,0x1F); // duco/

	writeRegister(CC1101_FSTEST ,0x59); // duco/
	writeRegister(CC1101_TEST2 ,0x81); // duco/
	writeRegister(CC1101_TEST1 ,0x35); // duco/
	writeRegister(CC1101_TEST0 ,0x09); // duco 0x09 / 0x0B

	writeRegister(CC1101_IOCFG0 ,0x07);	//!!! 	//High impedance (3-state)


	uint8_t ducoPaTableReceive[8] = {this->radioPower, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	writeBurstRegister(CC1101_PATABLE | CC1101_WRITE_BURST, (uint8_t*)ducoPaTableReceive, 8);
	
	writeCommand(CC1101_SIDLE);
	writeCommand(CC1101_SIDLE);

	writeCommand(CC1101_SCAL);

	//wait for calibration to finish
	//while ((readRegisterWithSyncProblem(CC1101_MARCSTATE, CC1101_STATUS_REGISTER)) != CC1101_MARCSTATE_IDLE) yield();
	// ADDED: Timeout added because device will loop when there is no response from cc1101. 
	unsigned long startedWaiting = millis();
	while((readRegisterWithSyncProblem(CC1101_MARCSTATE, CC1101_STATUS_REGISTER)) != CC1101_MARCSTATE_IDLE && millis() - startedWaiting <= 1000)
	{
		//esp_yield();
		delay(0); // delay will call esp_yield()
	}

	if((readRegisterWithSyncProblem(CC1101_MARCSTATE, CC1101_STATUS_REGISTER)) != CC1101_MARCSTATE_IDLE){
		this->ducoDeviceState = ducoDeviceState_initialisationCalibrationFailed;
		return;
	}


	writeCommand(CC1101_SRX);
	// wait till in rx mode (0x1F)
	//while ((readRegisterWithSyncProblem(CC1101_MARCSTATE, CC1101_STATUS_REGISTER)) != CC1101_MARCSTATE_RX) yield();
	// ADDED: Timeout added because device will loop when there is no response from cc1101. 
	startedWaiting = millis();
	while((readRegisterWithSyncProblem(CC1101_MARCSTATE, CC1101_STATUS_REGISTER)) != CC1101_MARCSTATE_RX && millis() - startedWaiting <= 1000)
	{
		delay(0); // delay will call esp_yield()
	}

	if((readRegisterWithSyncProblem(CC1101_MARCSTATE, CC1101_STATUS_REGISTER)) != CC1101_MARCSTATE_RX){
		this->ducoDeviceState = ducoDeviceState_initialisationRxmodeFailed;
		return;
	}	



//	if(this->deviceAddress != 0x00){
	this->ducoDeviceState = ducoDeviceState_initialised;
//	}


}


void DucoCC1101::sendDataToDuco(CC1101Packet *packet, uint8_t outboxQMessageNumber){
	// DEBUG
	if(this->logRFMessages){
		char bigLogBuf[110]; // 32 bytes * 3 characters (hex) = max 96 characters needed as buffer
		
		snprintf(bigLogBuf, sizeof(bigLogBuf), "SENT message: SRC:%u; DEST:%u; ORG.SRC:%u; ORG.DEST:%u; Ntwrk:%02x%02x%02x%02x;Type:%u; Bytes:%u; Counter:%u; ",
				  outboxQ[outboxQMessageNumber].packet.sourceAddress,outboxQ[outboxQMessageNumber].packet.destinationAddress, outboxQ[outboxQMessageNumber].packet.originalSourceAddress, outboxQ[outboxQMessageNumber].packet.originalDestinationAddress,outboxQ[outboxQMessageNumber].packet.networkId[0], outboxQ[outboxQMessageNumber].packet.networkId[1], outboxQ[outboxQMessageNumber].packet.networkId[2], outboxQ[outboxQMessageNumber].packet.networkId[3],outboxQ[outboxQMessageNumber].packet.messageType, outboxQ[outboxQMessageNumber].packet.dataLength + 10, outboxQ[outboxQMessageNumber].packet.counter);
		setLogMessage(bigLogBuf);
		memset(bigLogBuf, 0, sizeof(bigLogBuf)); // reset char bigLogBuf

		arrayToString(packet->data, min(static_cast<uint>(packet->length),sizeof(bigLogBuf)) , bigLogBuf);
		setLogMessage(bigLogBuf);
	}
	// DEBUG
	sendData(packet);
	outboxQ[outboxQMessageNumber].hasSent = true;
	
}



bool DucoCC1101::pollNewDeviceAddress(){
	if(this->ducoDeviceState == ducoDeviceState_joinSuccessful) {
		this->ducoDeviceState = ducoDeviceState_initialised;
		return 1;
	}else if(this->ducoDeviceState == ducoDeviceState_disjointed){
		this->ducoDeviceState = ducoDeviceState_notInitialised;
		return 1;
	}
	return 0;
}


  char DucoCC1101::valToHex(uint8_t val) {
    if ((val & 0x0f) < 10)
      return ('0' + val);
    else
      return ('a' + (val - 10));
  }

  String DucoCC1101::byteToHexString(uint8_t b) {
    String buffer = "";
    buffer += valToHex(b & 0x0f);
    b >>= 4;
    buffer = valToHex(b & 0x0f) + buffer;
    return buffer;
  }


void DucoCC1101::arrayToString(byte array[], unsigned int len, char buffer[])
{
    for (unsigned int i = 0; i < len; i++)
    {
        byte nib1 = (array[i] >> 4) & 0x0F;
        byte nib2 = (array[i] >> 0) & 0x0F;
        buffer[i*3+0] = nib1  < 0xA ? '0' + nib1  : 'A' + nib1  - 0xA;
        buffer[i*3+1] = nib2  < 0xA ? '0' + nib2  : 'A' + nib2  - 0xA;
		buffer[i*3+2] = 0x2C; // comma
    }
    buffer[len*3] = '\0';
}



// get nr in InboxQ where we can store a new received message.
// always start from 0 because messages in InboxQ are stored in order of arrival
uint8_t DucoCC1101::getInboxQFreeSpot(){

	for(uint8_t i=0; i< INBOXQ_MESSAGES;i++){
		if(inboxQ[i].messageProcessed == true){
			return i;
		}
	}
	return 255; // No free space in InboxQ
}

// get nr in OutboxQ where we can store a new sent message.
uint8_t DucoCC1101::getOutboxQFreeSpot(){
	for(uint8_t i=0; i< OUTBOXQ_MESSAGES;i++){
		if( outboxQ[i].hasSent == true){
			// if waitForAck = false OR if waitForAck = true AND AckReceived = true
			if( (outboxQ[i].waitForAck == false) || (outboxQ[i].waitForAck == true && outboxQ[i].ackReceived == true) ){
				return i;
			}
		}
	}
	return 255; // No free space in OutboxQ
}


bool DucoCC1101::checkForNewPacket()
{
	if (receiveData(&inMessage)){
		if(inMessage.length < 8 ){
			//discard package? length is smaller dan minimal packet length.... 
			// resulting in negative uint8_t inDucoPacket.dataLength :S
			setLogMessage(F("Invalid packet length (<8)."));
			return false;
		}

		// get next free spot in de inboxQ
		uint8_t messageNumber = getInboxQFreeSpot();

		//char bigLogBuf[20];
		
			//snprintf(bigLogBuf, sizeof(bigLogBuf), "free space:%u; ",messageNumber);
			//setLogMessage(bigLogBuf);

		if(messageNumber == 255){
			setLogMessage(F("No free space in InboxQ, dropping message;"));
			return false;
		}

		inboxQ[messageNumber].time_received_message = millis();
		inboxQ[messageNumber].messageProcessed = false;

		// reset dataLength
		// InboxQ[messageNumber].packet.dataLength = 0; is dit nodig?
		inboxQ[messageNumber].packet.messageType = inMessage.data[0];

		// Fill Duco packet with data
		memcpy(inboxQ[messageNumber].packet.networkId, &inMessage.data[1], sizeof inboxQ[messageNumber].packet.networkId);
		inboxQ[messageNumber].packet.sourceAddress = (inMessage.data[5] >> 3); // first 5 bits 
		inboxQ[messageNumber].packet.destinationAddress = ( (inMessage.data[5] & 0b00000111) << 2) | (inMessage.data[6] >> 6); // get last 3 bits and shift left.
		inboxQ[messageNumber].packet.originalSourceAddress = ( (inMessage.data[6] & 0b00111110) >> 1); // first 5 bits 
		inboxQ[messageNumber].packet.originalDestinationAddress = ( (inMessage.data[6] & 0b00000001) << 4) | (inMessage.data[7] >> 4); // get last 3 bits and shift left.
		inboxQ[messageNumber].packet.counter = (inMessage.data[7] & 0b00001111);

		inboxQ[messageNumber].packet.crc_ok = inMessage.crc_ok;
		inboxQ[messageNumber].packet.rssi = inMessage.rssi;
		inboxQ[messageNumber].packet.lqi = inMessage.lqi;
		inboxQ[messageNumber].packet.length = inMessage.length;
		inboxQ[messageNumber].packet.rssiSender = inMessage.data[8];

		memcpy(&inboxQ[messageNumber].packet.data, &inMessage.data[9],(inMessage.length-9));
		inboxQ[messageNumber].packet.dataLength = (inMessage.length-9);
		return true;
	}	// end if(receiveData(&inMessage)){
	return false;
}


void DucoCC1101::processNewMessages(){
for(uint8_t i=0; i< INBOXQ_MESSAGES;i++){
		if(inboxQ[i].messageProcessed == false){
			processMessage(i);
		}
	}
}
 

void DucoCC1101::processMessage(uint8_t inboxQMessageNumber)
{
		// CREATE LOG ENTRY OF RECEIVED PACKET
		if(this->logRFMessages){
  			char bigLogBuf[250];
		
			snprintf(bigLogBuf, sizeof(bigLogBuf), "RECEIVED message: SRC:%u; DEST:%u; ORG.SRC:%u; ORG.DEST:%u; Ntwrk:%02x%02x%02x%02x;Type:%u; Bytes:%u; Counter:%u; RSSI:%d (0x%02X); LQI: 0x%02X",
				  inboxQ[inboxQMessageNumber].packet.sourceAddress,inboxQ[inboxQMessageNumber].packet.destinationAddress, inboxQ[inboxQMessageNumber].packet.originalSourceAddress, inboxQ[inboxQMessageNumber].packet.originalDestinationAddress,inboxQ[inboxQMessageNumber].packet.networkId[0], inboxQ[inboxQMessageNumber].packet.networkId[1], inboxQ[inboxQMessageNumber].packet.networkId[2], inboxQ[inboxQMessageNumber].packet.networkId[3],inboxQ[inboxQMessageNumber].packet.messageType, inboxQ[inboxQMessageNumber].packet.length, inboxQ[inboxQMessageNumber].packet.counter, convertRssiHexToDBm(inboxQ[inboxQMessageNumber].packet.rssi), inboxQ[inboxQMessageNumber].packet.rssi, inboxQ[inboxQMessageNumber].packet.lqi);
			setLogMessage(bigLogBuf);
			memset(bigLogBuf, 0, sizeof(bigLogBuf)); // reset char bigLogBuf

			//log received bytes 
			arrayToString(inboxQ[inboxQMessageNumber].packet.data, min(static_cast<uint>(inboxQ[inboxQMessageNumber].packet.dataLength),sizeof(bigLogBuf)) , bigLogBuf);
			setLogMessage(bigLogBuf);
		}

		// check if network ID is our network ID
		/*YES: 
		- ducomsg_network
		- ducomsg_ack
		- ducomsg_message

		 IF NOT:
		- ducomsg_join2
		- ducomsg_join4
		*/ 
		if(matchingNetworkId(inboxQ[inboxQMessageNumber].packet.networkId)){ // check for network id
				
			// if destinationAddress is broadcastaddress = 0, then repeat the message
			if(inboxQ[inboxQMessageNumber].packet.destinationAddress == 0x00 && inboxQ[inboxQMessageNumber].packet.sourceAddress == 0x01 && inboxQ[inboxQMessageNumber].packet.messageType == ducomsg_network){
				setLogMessage(F("Received messagetype: network0"));
				processNetworkPacket(inboxQMessageNumber);

			// check if the message is send to this device
			}else if(inboxQ[inboxQMessageNumber].packet.destinationAddress == this->deviceAddress){
				//  if originalDestinationAddress is our address, we need to process the packet.
				//  otherwise repeat the message
				if(inboxQ[inboxQMessageNumber].packet.originalDestinationAddress == this->deviceAddress){
		
					switch(inboxQ[inboxQMessageNumber].packet.messageType){
						case ducomsg_network:{
							// if we receive a network message send to a specific node we most send an Ack!
							setLogMessage(F("Received messagetype: network0 (addressed to this node!)"));
							sendAck(inboxQMessageNumber); 
							processNetworkPacket(inboxQMessageNumber); // also resend network package if it is addressed to this node?
							break;
						}
						case ducomsg_ack:{
							setLogMessage(F("Received messagetype: ACK"));
							
							processReceivedAck(inboxQMessageNumber);				
							break;
						}

						case ducomsg_message:{
							setLogMessage(F("Received messagetype: Normal message"));
							sendAck(inboxQMessageNumber); 
							parseMessageCommand(inboxQMessageNumber);	
							break;
						}

						case ducomsg_join4:{
							setLogMessage(F("Received messagetype: JOIN4"));
							// if we are waiting for disjoin confirmation, finish disjoin
							if(ducoDeviceState == ducoDeviceState_disjoinWaitingForConfirmation){
								finishDisjoin(inboxQMessageNumber);

							// if we are waiting for join confirmation, finish joining
							}else if(ducoDeviceState == ducoDeviceState_join3){
								processJoin4Packet(inboxQMessageNumber);
							}else{
								// if ducobox didnt receive the first ack (from sendJoinFinish()), check if address in packet is the same and send ack again!
								setLogMessage(F("Received join4 message but join already finished, check address and resend ACK."));
								if(inboxQ[inboxQMessageNumber].packet.data[5] == this->deviceAddress){
									sendAck(inboxQMessageNumber);
									setLogMessage(F("sendJoin4FinishPacket: another ACK sent!"));
								}else{
									setLogMessage(F("No match between join4 address and our deviceid. Ignoring message."));
								}
							}
							break;
						}

						default:{
							setLogMessage(F("Received messagetype: unknown"));
							break;
						}
					} // end switch

				}else{
				// destinationAddress is our address but originalDestinationAddress is an other device
				// as repeater we need to repeat the message (send to inDucoPacket.originalDestinationAddress)
					repeatMessage(inboxQMessageNumber);
				}
						
			}
		
		
		}else // network ID doesnt match with our network ID => are we joining a network?
		{
			switch(inboxQ[inboxQMessageNumber].packet.messageType){
				case ducomsg_join2:{
					setLogMessage(F("Received messagetype: JOIN2"));
					if(ducoDeviceState == ducoDeviceState_join1){
						processJoin2Packet(inboxQMessageNumber);
					}else{
						setLogMessage(F("Ignoring join 2 message because gateway ducoDeviceState isn't JOIN1."));
					}
					break;
				}
				default:{
					setLogMessage(F("Ignore message, not our networkID."));
					break;
				}
			}//switch

		}
		
	// update inboxQMessage 
	inboxQ[inboxQMessageNumber].messageProcessed = true;


}
	




void DucoCC1101::processReceivedAck(uint8_t inboxQMessageNumber){

	for(uint8_t i=0; i< OUTBOXQ_MESSAGES;i++){
			char bigLogBuf[60];
		
			snprintf(bigLogBuf, sizeof(bigLogBuf), "hassent:%u; waitforack:%u;ackreceived:%u; counter: %u",outboxQ[i].hasSent, outboxQ[i].waitForAck, outboxQ[i].ackReceived, outboxQ[i].packet.counter);
			setLogMessage(bigLogBuf);


		if(outboxQ[i].hasSent == true){
			setLogMessage(F("HAS SENT TRUE"));

			if(outboxQ[i].waitForAck == true && outboxQ[i].ackReceived == false){
			setLogMessage(F("waitfor ACK = TRUE en ackreceived= FALSE"));

				if(outboxQ[i].packet.counter == inboxQ[inboxQMessageNumber].packet.counter){
					outboxQ[i].ackReceived = true;
					setLogMessage(F("Received ACK  for a message we've sent."));
					
					// if we've send a disjoin message and receive an ACK, set ducoDeviceState to the next status
					if(ducoDeviceState == ducoDeviceState_disjoinWaitingForAck){
						ducoDeviceState = ducoDeviceState_disjoinWaitingForConfirmation;
					}

					return;
				}
			}	
		}	
	} 
	setLogMessage(F("Received ACK but cant match it with the messagecounter of a sent message."));
}

uint8_t DucoCC1101::updateMessageCounter(){
	if(this->messageCounter == 15){
		this->messageCounter = 1;
	}else{
		this->messageCounter++;
	}
	return this->messageCounter;
}

/* convert RSSI value from duco packet to normale CC1101 RSSI value */
uint8_t DucoCC1101::getRssi(uint8_t rssi){
	if (rssi >= 128){
        //rssi_dec = ( rssi_byte - 256) / 2) - 74;
		return (rssi - 128);
	}else{
        //rssi_dec = (rssi_byte / 2) - 74;
		return (rssi + 128);
	}
}

int DucoCC1101::convertRssiHexToDBm(uint8_t rssi){
	int rssi_dec = 0;
	if (rssi >= 128){
      rssi_dec = (( rssi - 256) / 2) - 74;
	}else{
      rssi_dec = (rssi / 2) - 74;
	}
	return rssi_dec;

}

void DucoCC1101::processNetworkPacket(uint8_t inboxQMessageNumber){
	/* first data byte of a network package:
		- 0x00 = installermode off
		- 0x01 = installermode on
		- 0x06 = ?????
	*/
	switch(inboxQ[inboxQMessageNumber].packet.data[1]){
		case 0x00: 
			installerModeActive = false; // deactivate installerMode
			// no log message here because we often receive a networkpacket
			break;
		case 0x01:
			installerModeActive = true; // activate installerMode
			setLogMessage(F("Installermode activated!"));
		break;
		default:
			/* 
			If databyte is higer than 1, it is a broadcast for a specific the node number!
			Duco packettype = LINK (coreloglevel debug)
			*/
			setLogMessage(F("Network message -> call to specific node"));
		break;
	}

	// get a free spot in OutboxQ
	uint8_t outboxQMessageNumber = getOutboxQFreeSpot();
	if(outboxQMessageNumber == 255){
		setLogMessage(F("No free space in outboxQ, dropping message;"));
		return;
	}
	resetOutDucoPacket(outboxQMessageNumber);
	
	// copy data from incoming network package 
	outboxQ[outboxQMessageNumber].packet.data[0] = inboxQ[inboxQMessageNumber].packet.data[0]; 
	outboxQ[outboxQMessageNumber].packet.dataLength++;
 	outboxQ[outboxQMessageNumber].packet.messageType = ducomsg_network;

 	prefillDucoPacket(&outboxQ[outboxQMessageNumber].packet, 0x00, inDucoPacket.originalSourceAddress, inDucoPacket.originalDestinationAddress); 

	outboxQ[outboxQMessageNumber].packet.counter = inDucoPacket.counter;
	ducoToCC1101Packet(&outboxQ[outboxQMessageNumber].packet, &outMessage);

	// packet 9 = footer, needs to be rssi value. 	// after receiving a networkpacket, each node repeats the network packet with RSSI value
	outboxQ[outboxQMessageNumber].packet.rssi = getRssi(inboxQ[inboxQMessageNumber].packet.rssi);

	sendDataToDuco(&outMessage, outboxQMessageNumber);

	outboxQ[outboxQMessageNumber].hasSent 		= true;
	outboxQ[outboxQMessageNumber].waitForAck 	= false;
	outboxQ[outboxQMessageNumber].ackReceived 	= false;
	outboxQ[outboxQMessageNumber].ackTimer 		= 0;
	outboxQ[outboxQMessageNumber].sendRetries 	= 0;

	setLogMessage(F("Send processNetworkPacket done!"));
}


// reset outducoPacket to default values
void DucoCC1101::resetOutDucoPacket(uint8_t outboxQMessageNumber){
	
	outboxQ[outboxQMessageNumber].packet.messageType = 0x00; // 0=network message

	outboxQ[outboxQMessageNumber].packet.data[0] = 0x00; // reset commandLength bytes
	outboxQ[outboxQMessageNumber].packet.data[1] = 0x00; // reset commandLength bytes

	outboxQ[outboxQMessageNumber].packet.dataLength = 2;

	outboxQ[outboxQMessageNumber].packet.sourceAddress =  0x00;
	outboxQ[outboxQMessageNumber].packet.destinationAddress = 0x00;
	outboxQ[outboxQMessageNumber].packet.originalSourceAddress =  0x00;
	outboxQ[outboxQMessageNumber].packet.originalDestinationAddress = 0x00;
	outboxQ[outboxQMessageNumber].packet.counter = 0x00;
	outboxQ[outboxQMessageNumber].packet.rssi = 0x00;

	for(int i=0; i<4;i++) // maybe skip this?
	outboxQ[outboxQMessageNumber].packet.networkId[i] = 0x00;

	// reset status flags
	outboxQ[outboxQMessageNumber].hasSent 		= false;
	outboxQ[outboxQMessageNumber].waitForAck 	= false;
	outboxQ[outboxQMessageNumber].ackReceived 	= false;
	outboxQ[outboxQMessageNumber].ackTimer 		= 0;
	outboxQ[outboxQMessageNumber].sendRetries 	= 0;

}



void DucoCC1101::sendDisjoinPacket(){
	setLogMessage(F("sendDisjoinPacket()"));

	ducoDeviceState = ducoDeviceState_disjoinRequest;

	// get a free spot in OutboxQ
	uint8_t outboxQMessageNumber = getOutboxQFreeSpot();
	if(outboxQMessageNumber == 255){
		setLogMessage(F("No free space in outboxQ, dropping message;"));
		return;
	}

	resetOutDucoPacket(outboxQMessageNumber);
	
 	outboxQ[outboxQMessageNumber].packet.messageType = ducomsg_message;

	setCommandLength(&outboxQ[outboxQMessageNumber].packet, 0, 1); // set commandlength

	outboxQ[outboxQMessageNumber].packet.data[2] = 0x3B;
	outboxQ[outboxQMessageNumber].packet.dataLength++;
 
  	prefillDucoPacket(&outboxQ[outboxQMessageNumber].packet, 0x01); // to ducobox
	outboxQ[outboxQMessageNumber].packet.counter = updateMessageCounter();
	ducoToCC1101Packet(&outboxQ[outboxQMessageNumber].packet, &outMessage);

	sendDataToDuco(&outMessage, outboxQMessageNumber);
	setLogMessage(F("SEND disjoin packet done!"));
	ducoDeviceState = ducoDeviceState_disjoinWaitingForAck;

	// update outboxQMessage
	outboxQ[outboxQMessageNumber].hasSent 		= true;
	outboxQ[outboxQMessageNumber].waitForAck 	= false;
	outboxQ[outboxQMessageNumber].ackReceived 	= false;
	outboxQ[outboxQMessageNumber].ackTimer 		= 0;
	outboxQ[outboxQMessageNumber].sendRetries 	= 0;
}

void DucoCC1101::finishDisjoin(uint8_t inboxQMessageNumber){
	setLogMessage(F("FinishDisjoin()"));
	if(matchingNetworkId(inboxQ[inboxQMessageNumber].packet.networkId)){
		if(matchingDeviceAddress(inboxQ[inboxQMessageNumber].packet.destinationAddress)){
			sendAck(inboxQMessageNumber); 						// then send ack
			setLogMessage(F("Device disjoining finished!"));
			// remove networkID and deviceID
			this->networkId[0] = 0x00;
			this->networkId[1] = 0x00;
			this->networkId[2] = 0x00;
			this->networkId[3] = 0x00;

			this->deviceAddress = 0x00;
			ducoDeviceState = ducoDeviceState_disjointed;

		}
	}
	// update inboxQMessage 
	inboxQ[inboxQMessageNumber].messageProcessed = true;
}

void DucoCC1101::sendJoinPacket(){
	setLogMessage(F("SendJoinPacket()"));

	// get a free spot in OutboxQ
	uint8_t outboxQMessageNumber = getOutboxQFreeSpot();
	if(outboxQMessageNumber == 255){
		setLogMessage(F("No free space in outboxQ, dropping message;"));
		return;
	}
	resetOutDucoPacket(outboxQMessageNumber);
	
	// remove networkID and deviceID, to receive the join messages me need to be in network 0000000 and address 0
	this->networkId[0] = 0x00;
	this->networkId[1] = 0x00;
	this->networkId[2] = 0x00;
	this->networkId[3] = 0x00;

	this->deviceAddress = 0x00;

	outboxQ[outboxQMessageNumber].packet.messageType = ducomsg_join1;	
	outboxQ[outboxQMessageNumber].packet.sourceAddress =  0x01;
	outboxQ[outboxQMessageNumber].packet.destinationAddress = 0x00;
	outboxQ[outboxQMessageNumber].packet.originalSourceAddress =  0x01;
	outboxQ[outboxQMessageNumber].packet.originalDestinationAddress = 0x00;
	outboxQ[outboxQMessageNumber].packet.counter = messageCounter;

	// this temporarly networkId is the hardware device ID of the node
	for(int i=0; i<4;i++)
	outboxQ[outboxQMessageNumber].packet.networkId[i] = joinCO2NetworkId[i];
	outboxQ[outboxQMessageNumber].packet.data[2] = 0x0c; // 0x0c = 00001100 (BIN) = 12 (DEC)
	outboxQ[outboxQMessageNumber].packet.dataLength++;

	ducoToCC1101Packet(&outboxQ[outboxQMessageNumber].packet, &outMessage);

	sendDataToDuco(&outMessage, outboxQMessageNumber);
	setLogMessage(F("Joinpacket sent. DucoDeviceState = ducoDeviceState_join1"));
	ducoDeviceState = ducoDeviceState_join1;

	// update inboxQMessage and outboxQMessage
	outboxQ[outboxQMessageNumber].hasSent 		= true;
	outboxQ[outboxQMessageNumber].waitForAck 	= false;
	outboxQ[outboxQMessageNumber].ackReceived 	= false;

}



// TODO: split this function in receiveJoin2Packet and sendJoin3Packet
void DucoCC1101::processJoin2Packet(uint8_t inboxQMessageNumber){
	setLogMessage(F("processJoin2Packet()"));

	// check if joinCO2NetworkId is in data
	if(joinPacketValidNetworkId(inboxQMessageNumber)){
		setLogMessage(F("SendJoin3Packet: valid join2 packet received!"));
		sendJoin3Packet(inboxQMessageNumber);
	}else{
		// cant find joinCO2NetworkId in data
		setLogMessage(F("SendJoin3Packet: INVALID join2 packet received!"));
	}
}


void DucoCC1101::sendJoin3Packet(uint8_t inboxQMessageNumber){
	// get a free spot in OutboxQ
	uint8_t outboxQMessageNumber = getOutboxQFreeSpot();
	if(outboxQMessageNumber == 255){
		setLogMessage(F("No free space in outboxQ, dropping message;"));
		return;
	}
	resetOutDucoPacket(outboxQMessageNumber);

	// TODO -> if we received a message from a repeater than we need to send this join3 packet to the repeater.
	outboxQ[outboxQMessageNumber].packet.sourceAddress =  0x00;  
	outboxQ[outboxQMessageNumber].packet.destinationAddress = 0x01; // <<<<<<<<<<<<<<
	outboxQ[outboxQMessageNumber].packet.originalSourceAddress =  0x00;
	outboxQ[outboxQMessageNumber].packet.originalDestinationAddress = 0x01;

	// update networkid
	memcpy(this->networkId, &inboxQ[inboxQMessageNumber].packet.networkId,4);

	// send response
	outboxQ[outboxQMessageNumber].packet.messageType = ducomsg_join3;
		
	memcpy(outboxQ[outboxQMessageNumber].packet.networkId,networkId,4 );

	//co2 = 00 00 7C 3E -- Batt Remote = 00 00 00 00
	for(int i=0; i<4;i++)
	outboxQ[outboxQMessageNumber].packet.data[i] = joinCO2NetworkId[i];

	outboxQ[outboxQMessageNumber].packet.data[4] = 0x0C;
	outboxQ[outboxQMessageNumber].packet.dataLength = 5;

	outboxQ[outboxQMessageNumber].packet.counter = 0x0D; // counter is always 13 for join3 message! (for battery remote!!!)
	ducoToCC1101Packet(&outboxQ[outboxQMessageNumber].packet, &outMessage);

	sendDataToDuco(&outMessage,outboxQMessageNumber);
	setLogMessage(F("sendJoin3Packet: join3 message sent. DucoDeviceState = ducoDeviceState_join3"));
	ducoDeviceState = ducoDeviceState_join3;

	// update outboxQMessage
	outboxQ[outboxQMessageNumber].hasSent 		= true;
	outboxQ[outboxQMessageNumber].waitForAck 	= false;
	outboxQ[outboxQMessageNumber].ackReceived 	= false;
	outboxQ[outboxQMessageNumber].ackTimer 		= 0;
	outboxQ[outboxQMessageNumber].sendRetries 	= 0;

	
}


bool DucoCC1101::joinPacketValidNetworkId(uint8_t inboxQMessageNumber){
	setLogMessage(F("joinPacketValidNetworkId()"));
		if((inboxQ[inboxQMessageNumber].packet.sourceAddress == 0x01) && (inboxQ[inboxQMessageNumber].packet.destinationAddress == 0x00)){
			// check if network id is in command
			for(int i=0; i<4;i++){
				if(inboxQ[inboxQMessageNumber].packet.data[i] == joinCO2NetworkId[i]){
					if(i==3){ 
						return true;
					}
				}else{
					return false;
				}
			}
		}
	return false;
}



void DucoCC1101::processJoin4Packet(uint8_t inboxQMessageNumber){
	// A join4 message is send to the networkID of the Ducobox (networkID is set by a join2 message). Check for matching network ID
	if(matchingNetworkId(inboxQ[inboxQMessageNumber].packet.networkId)){
		if(joinPacketValidNetworkId(inboxQMessageNumber)){
			sendJoin4FinishPacket(inboxQMessageNumber);
		}else{
			setLogMessage(F("processJoin4Packet: invalid join4 packet received, can't find joinCO2NetworkId in data."));
		}
	}else{
			setLogMessage(F("processJoin4Packet: invalid join4 packet received, not our network ID."));
	}
}


// data of a join4 message: 00 00 7C 3E XX YY
// XX = network address
// YY = node number
void DucoCC1101::sendJoin4FinishPacket(uint8_t inboxQMessageNumber){
	ducoDeviceState = ducoDeviceState_join4;

	setLogMessage(F("sendJoinFinish: valid join4 packet received!"));
	this->deviceAddress = inboxQ[inboxQMessageNumber].packet.data[4]; // = new address	
	//this->nodeNumber = inDucoPacket.data[5]; // = nodenumber -> do we need to save this somewhere?

	char logBuf[50];
	snprintf(logBuf, sizeof(logBuf), "sendJoinFinish: new device address is: %u;",this->deviceAddress);
	setLogMessage(logBuf);

	// send ack! from new deviceaddress to address of sender.
	sendAck(inboxQMessageNumber); //
	setLogMessage(F("sendJoinFinish: ACK sent!"));
	ducoDeviceState = ducoDeviceState_joinSuccessful;
}




void DucoCC1101::waitForAck(uint8_t outboxQMessageNumber){
	setLogMessage(F("Start waiting for ack..."));
	outboxQ[outboxQMessageNumber].ackTimer = millis();
	outboxQ[outboxQMessageNumber].sendRetries = 0;
	outboxQ[outboxQMessageNumber].waitForAck = true;
	outboxQ[outboxQMessageNumber].ackReceived = false;

}

// loop through all outboxq messages to check for ack!
void DucoCC1101::checkForAck(){
	for(uint8_t outboxQMessageNumber=0; outboxQMessageNumber < OUTBOXQ_MESSAGES;outboxQMessageNumber++){


		if(outboxQ[outboxQMessageNumber].hasSent == true){
			//  if waitForAck = true AND AckReceived = false
			if( (outboxQ[outboxQMessageNumber].waitForAck == true && outboxQ[outboxQMessageNumber].ackReceived == false) ){
							char bigLogBuf[100];
		     snprintf(bigLogBuf, sizeof(bigLogBuf), "Q%u hassent:%u; waitforack:%u;ackreceived:%u; timer: %lu; sendTries: %u ",outboxQMessageNumber, outboxQ[outboxQMessageNumber].hasSent, outboxQ[outboxQMessageNumber].waitForAck, outboxQ[outboxQMessageNumber].ackReceived, outboxQ[outboxQMessageNumber].ackTimer, outboxQ[outboxQMessageNumber].sendRetries);
			setLogMessage(bigLogBuf);


				unsigned long mill = millis();
 				if (mill - outboxQ[outboxQMessageNumber].ackTimer >= 300){ // wait for 300 ms (standard duco),
				 	setLogMessage(F("CheckforAck: check if 300ms is verstreken"));

		 			if(outboxQ[outboxQMessageNumber].sendRetries < this->sendTries){
						setLogMessage(F("CheckforAck: still waiting for ACK. Sending message again..."));
						//resend message
						ducoToCC1101Packet(&outboxQ[outboxQMessageNumber].packet, &outMessage);
						sendDataToDuco(&outMessage,outboxQMessageNumber);
						//setLogMessage("CheckforAck: message resent");

						outboxQ[outboxQMessageNumber].ackTimer = millis();
						outboxQ[outboxQMessageNumber].sendRetries++;
			 		}else{
						outboxQ[outboxQMessageNumber].waitForAck = 0;
						setLogMessage(F("CheckforAck: no ack received, cancel retrying."));
					}
		 		}
			}
		}
	}
}

bool DucoCC1101::matchingNetworkId(uint8_t id[4]) 
{
	for (uint8_t i=0; i<=3;i++){
		if (id[i] != this->networkId[i]){
			return false;
		}
	}
	return true;
}


bool DucoCC1101::matchingDeviceAddress(uint8_t compDeviceAddress){
	if(this->deviceAddress == compDeviceAddress){
		return true;
	}else{
		return false;
	}
}

 


bool DucoCC1101::checkForNewPacketInRXFifo(){
	uint8_t rxBytes = readRegisterWithSyncProblem(CC1101_RXBYTES, CC1101_STATUS_REGISTER);
	
	char LogBuf[20];
	snprintf(LogBuf, sizeof(LogBuf), "STATUS: %02X",rxBytes);
	setLogMessage(LogBuf);

	rxBytes = rxBytes & CC1101_BITS_RX_BYTES_IN_FIFO;

	if(rxBytes > 0){
		return true;
	}else{
		return false;
	}
}


uint8_t DucoCC1101::getMarcState(bool noLogMessage){
	uint8_t result = readRegisterWithSyncProblem(CC1101_MARCSTATE, CC1101_STATUS_REGISTER);

if(!noLogMessage){
	char LogBuf[20];
	snprintf(LogBuf, sizeof(LogBuf), "MARCST: %02X",result);
	setLogMessage(LogBuf);
}
return result;
}



uint8_t DucoCC1101::checkForBytesInRXFifo(){
	uint8_t rxBytes = readRegisterWithSyncProblem(CC1101_RXBYTES, CC1101_STATUS_REGISTER);
	rxBytes = rxBytes & CC1101_BITS_RX_BYTES_IN_FIFO;

return rxBytes;

}




void DucoCC1101::parseMessageCommand(uint8_t inboxQMessageNumber)
{
	uint8_t command = 0;
	uint8_t commandLength = 0;
	uint8_t startByteNextCommand = 2; // = de startbyte van het eerstvolgende commando. data[0] en data[1] zijn commandLenght bytes. begint bij inDucoPacket.data[2]  
	bool commandWaitForAck = false;


	// get a free spot in OutboxQ
	uint8_t outboxQMessageNumber = getOutboxQFreeSpot();
	if(outboxQMessageNumber == 255){
		setLogMessage(F("No free space in outboxQ, dropping message;"));
		return;
	}
	resetOutDucoPacket(outboxQMessageNumber);

	for(uint8_t commandNumber=0; commandNumber<4; commandNumber++){
		commandLength = getCommandLength(inboxQ[inboxQMessageNumber].packet.data[0], inboxQ[inboxQMessageNumber].packet.data[1], commandNumber);


		if(commandLength > 0){
			command = inboxQ[inboxQMessageNumber].packet.data[startByteNextCommand]; // get command 

			switch(command){
				case 0x00: //  unknown, command 0x00 followed by node id (e.g. 0x00 0x02)
					setLogMessage(F("???"));

					commandWaitForAck = true; // ??? ACK needed?
				break;

				case 0x01: // 0x01 = nodereset commando 
					setLogMessage(F("Received nodereset command"));

					commandWaitForAck = true; // ??? ACK needed?
				break;

				case 0x02: // 0x02 = node save data command
					setLogMessage(F("Received node save data command"));

					commandWaitForAck = true; // ??? ACK needed?
				break;

				case 0x36: // 0x36 = wachten op aanmelden roosters/kleppen
					setLogMessage(F("Received waitcommand for joining electronic window vents."));
					sendPing(outboxQMessageNumber, commandNumber, startByteNextCommand);
					commandWaitForAck = true;
				break;

				case 0x12: // 0x12 = bevestiging wijziging ventilatiemodus
					setLogMessage(F("Received change ventilation command"));
					if(processNewVentilationMode(inboxQMessageNumber, commandNumber, startByteNextCommand)){ // if ventilationmode is changed, 
						prepareVentilationModeMessage(outboxQMessageNumber, commandNumber, this->permanentVentilationMode, false, this->currentVentilationMode, 0, this->temperature, false, 0);
						commandWaitForAck = true;
						setLogMessage(F("Send sendConfirmationNewVentilationMode"));
					}else{
						setLogMessage(F("Ventilationmode did not changed, no need to send confirmation."));
					}

				break;

				case 0x40: // 0x40 = opvragen parameter(s)
					setLogMessage(F("Received request for parameter value command"));
					sendNodeParameterValue(outboxQMessageNumber, inboxQMessageNumber, commandNumber, startByteNextCommand);
					commandWaitForAck = true;
				break;

				case 0x49: // 0x48 = parameter schrijven
					
					commandWaitForAck = true;
				break;

				case 0x50: // 0x50 = Duidt het geselecteerde component aan tijdens wijzigen van instellingen (blauwe led op bedieningsschakelaar)

					
					commandWaitForAck = true;
				break;

				case 0x51: // 0x51 = keert terug naar "normale stand" (witte leds)
					
					commandWaitForAck = true;
				break;

				case 0x52: // 0x52 = Ducobox in initialisatie stand
					// inboxQ[inboxQMessageNumber].packet.data[startByteNextCommand] 
					// 0x00 = Ducobox normale stand
					// 0x01 = Ducobox in Initialisatie (inregeling van het systeem bezig) (na heropstart systeem)
							   // gele leds op bedieningsschakelaar + intern remove allowed = true (in deze stand) 
							   // TODO: checken of bedieningsschakelaar zelf een timer bijhoud waarbinnen hij verwijderd mag worden na opstarten ducobox!

					// 0x02 = Overgangsfase (a.u.b. wachten)	leds GEEL (of groen???) traag knipperen

					// 0x03 = onbekende status (groen/geel contstant aan)

					
				break;



				default:{
					setLogMessage(F("Unknown command received"));
					break;
				}

			}

		}

		startByteNextCommand = startByteNextCommand + commandLength;
	}

	// bericht versturen i.p.v in de losse functies om zo meerdere commandos in een bericht te verwerken.

	// check if there is data to send
	if(outboxQ[outboxQMessageNumber].packet.dataLength > 2){
		outboxQ[outboxQMessageNumber].packet.messageType = ducomsg_message;

		prefillDucoPacket(&outboxQ[outboxQMessageNumber].packet, inboxQ[inboxQMessageNumber].packet.sourceAddress, inboxQ[inboxQMessageNumber].packet.originalDestinationAddress, inboxQ[inboxQMessageNumber].packet.originalSourceAddress); 
		outboxQ[outboxQMessageNumber].packet.counter = updateMessageCounter();
		ducoToCC1101Packet(&outboxQ[outboxQMessageNumber].packet, &outMessage);

		sendDataToDuco(&outMessage,outboxQMessageNumber);
		setLogMessage(F("send response"));

		// update outboxQMessage
		outboxQ[outboxQMessageNumber].hasSent 		= true;

		if(commandWaitForAck){
			waitForAck(outboxQMessageNumber);
		}




	}else{
		setLogMessage(F("No data to send!"));
	}


}


	// destinationAddress is our address but originalDestinationAddress is an other device
	// as repeater we need to repeat the message (send to inDucoPacket.originalDestinationAddress)
void DucoCC1101::repeatMessage(uint8_t inboxQMessageNumber){

	// get a free spot in OutboxQ
	uint8_t outboxQMessageNumber = getOutboxQFreeSpot();
	if(outboxQMessageNumber == 255){
		setLogMessage(F("No free space in outboxQ, dropping message;"));
		return;
	}
	resetOutDucoPacket(outboxQMessageNumber);

	// copy messageType and counter
	outboxQ[outboxQMessageNumber].packet.messageType = inboxQ[inboxQMessageNumber].packet.messageType;
	outboxQ[outboxQMessageNumber].packet.counter = inboxQ[inboxQMessageNumber].packet.counter;

	// copy data from incoming packet to outgoing packet
	memcpy(&outboxQ[outboxQMessageNumber].packet.data, &inMessage.data, inboxQ[inboxQMessageNumber].packet.dataLength);
	outboxQ[outboxQMessageNumber].packet.dataLength = inboxQ[inboxQMessageNumber].packet.dataLength;

  	prefillDucoPacket(&outboxQ[outboxQMessageNumber].packet, inboxQ[inboxQMessageNumber].packet.originalDestinationAddress, inboxQ[inboxQMessageNumber].packet.originalSourceAddress, inboxQ[inboxQMessageNumber].packet.originalDestinationAddress); 
	ducoToCC1101Packet(&outboxQ[outboxQMessageNumber].packet, &outMessage);

	// packet 9 = RSSI value for networkpackages and repeater messaages
	// each node repeats the packet with RSSI value in byte 
	uint8_t ducoRssi;
	ducoRssi = getRssi(inboxQMessageNumber);
	outMessage.data[9] = ducoRssi;

	sendDataToDuco(&outMessage,outboxQMessageNumber);
	setLogMessage(F("SEND repeatMessage() done!"));
}







void DucoCC1101::sendNodeParameterValue(uint8_t outboxQMessageNumber, uint8_t inboxQMessageNumber, uint8_t commandNumber, uint8_t startByteCommand){
	//device serial: RS1521002390 (ascii -> hex)
	//const uint8_t deviceSerial[12] = {0x52, 0x53, 0x31, 0x35, 0x32, 0x31, 0x30, 0x30, 0x32, 0x33, 0x39, 0x31};
	//device serial: VGATEWAY0082 (ascii -> hex)
	const uint8_t deviceSerial[12] = {0x56, 0x47, 0x41, 0x54, 0x45, 0x57, 0x41, 0x59, 0x30, 0x30, 0x38, 0x32};

	uint8_t parameterValue1 = 0x00;
	uint8_t parameterValue2 = 0x00;

	uint8_t parameter = inboxQ[inboxQMessageNumber].packet.data[(startByteCommand+2)];

	switch(parameter){

		case 0x00:{  // 0x00 = product id, value=12030 (0x2E 0xFE)
			parameterValue1 = 0x2E; 
			parameterValue2 = 0xFE; 
			break; 
		}

		case 0x01:{ // 0x01 = softwareversion >X<.x.x, value= 1 (0x00 0x01)
			parameterValue1 = 0x00; 
			parameterValue2 = 0x01; 
			break; 
		}

		case 0x02:{  // 0x02 = softwareversion x.>X<.x, value= 2 (0x00 0x02)
			parameterValue1 = 0x00; 
			parameterValue2 = 0x02; 
			break; 
		}

		case 0x03:{ // 0x03 = softwareversion x.x.>X<, value= 0 (0x00 0x00)
			parameterValue1 = 0x00; 
			parameterValue2 = 0x00; 
			break; 
		}

		case 0x05:{ // parameter 5 = ?????, 2-byte response always 0x00 0x11
			parameterValue1 = 0x00; 
			parameterValue2 = 0x11; 
		break;
		}

		// deviceserial
		case 0x08:{ parameterValue1 = deviceSerial[1]; 	parameterValue2= deviceSerial[0]; 	break; }
		case 0x09:{ parameterValue1 = deviceSerial[3]; 	parameterValue2= deviceSerial[2]; 	break; }
		case 0x0A:{ parameterValue1 = deviceSerial[5]; 	parameterValue2= deviceSerial[4]; 	break; }
		case 0x0B:{ parameterValue1 = deviceSerial[7]; 	parameterValue2= deviceSerial[6]; 	break; }
		case 0x0C:{ parameterValue1 = deviceSerial[9]; 	parameterValue2= deviceSerial[8]; 	break; }
		case 0x0D:{ parameterValue1 = deviceSerial[11]; parameterValue2= deviceSerial[10]; 	break; }

		case 0x16:{ // parameter 22 = NodeParaSetRegPower one byte response HIGH=0xC0 DEFAULT = 0xC1, LOW = 0xC5
			parameterValue1 = 0x00; 
			parameterValue2 = 0xC1; 	
		break;
		}

		case 0x18:{ // parameter 24 =  WeekCnt, how many weeks device is in use.
			parameterValue1 = 0x00; 
			parameterValue2 = 0x03;  // 3 weeks	
		break;
		}

		case 0x19:{ // parameter 25 = BootCnt, how often a device is turned on.
			parameterValue1 = 0x00; 
			parameterValue2 = 0x7B; // 123 times	
		break;
		}

		case 0x49:{ // parameter 73 = temp, 2-byte response 210 = 21.0C
			int currentTemp = 210;
			parameterValue1 = ((currentTemp >> 8) & 0xff);
			parameterValue2 =  (currentTemp & 0xff); 
		break;
		}

		case 0x4A:{ // parameter 74 = CO2 value (ppm), 2-byte response 500 = 500 ppm
			int currentCO2 = 500;
			parameterValue1 = ((currentCO2 >> 8) & 0xff);
			parameterValue2 =  (currentCO2 & 0xff);
		break;
		}

		case 0x80:{  // parameter 128 = ?????, 2-byte response always 0x00 0x00
			parameterValue1 = 0x00; 
			parameterValue2 = 0x00; 
		break;
		}

		//parameter 137 = CO2 Setpoint (ppm) (2-byte response)
		case 0x89:{ 
			int CO2Setpoint = 850;
			parameterValue1 = ((CO2Setpoint >> 8) & 0xff);
			parameterValue2 = (CO2Setpoint & 0xff);			
		break;
		}

		
		case 0xA4:{  // parameter 164 = ?????, 
			parameterValue1 = 0x00; 
			parameterValue2 = 0x01; 
		break;
		}

		
		case 0xA6:{  // parameter 166 = ?????, 
			parameterValue1 = 0x00; 
			parameterValue2 = 0x00; 
		break;
		}

		default:{
		//parameter does not exist response: 0x40	0x00	0x42	0x00 + parameterbyte + 0x02
		// need to reverse engineer the response of a non existing parameter!
			setLogMessage(F("sendNodeParameterValue(); Requested parameter does not exist!"));
		break;
		}
	}

	setCommandLength(&outboxQ[outboxQMessageNumber].packet, commandNumber, 5); // set commandlength first command, command (1byte) + parameter (2 bytes) + 2 value bytes = 5 bytes.

	outboxQ[outboxQMessageNumber].packet.data[outboxQ[outboxQMessageNumber].packet.dataLength++] = 0x41; 			// byte 1: commando "response opvragen parameter"
	outboxQ[outboxQMessageNumber].packet.data[outboxQ[outboxQMessageNumber].packet.dataLength++] = 0x00; 			// byte 2+3: parameter (2 bytes)
	outboxQ[outboxQMessageNumber].packet.data[outboxQ[outboxQMessageNumber].packet.dataLength++] = parameter;	
	outboxQ[outboxQMessageNumber].packet.data[outboxQ[outboxQMessageNumber].packet.dataLength++] = parameterValue1;	// byte 4: parametervalue (2 bytes)
	outboxQ[outboxQMessageNumber].packet.data[outboxQ[outboxQMessageNumber].packet.dataLength++] = parameterValue2;	// byte 5: 
	
	char logBuf[30];
	snprintf(logBuf, sizeof(logBuf), "SEND parameter %u done", parameter);
	setLogMessage(logBuf);
	
}



void DucoCC1101::sendPing(uint8_t outboxQMessageNumber, uint8_t commandNumber, uint8_t startByteCommand){
	
	outDucoPacket.messageType = ducomsg_message;

	setCommandLength(&outDucoPacket, commandNumber, 1); // set commandlength first command, 1 byte.

	outDucoPacket.data[ ++outDucoPacket.dataLength  ] = (inDucoPacket.data[startByteCommand] +1); // get first data byte of ping command, add 1, 
	setLogMessage(F("SEND PING done!"));
}




void DucoCC1101::sendAck(uint8_t inboxQMessageNumber){
	//setLogMessage("Send Ack...");

	// get a free spot in OutboxQ
	uint8_t outboxQMessageNumber = getOutboxQFreeSpot();
	if(outboxQMessageNumber == 255){
		setLogMessage(F("No free space in outboxQ, dropping message;"));
		return;
	}
	resetOutDucoPacket(outboxQMessageNumber);

	outboxQ[outboxQMessageNumber].packet.messageType = ducomsg_ack;
	outboxQ[outboxQMessageNumber].packet.dataLength = 0;

	prefillDucoPacket(&outboxQ[outboxQMessageNumber].packet, inboxQ[inboxQMessageNumber].packet.sourceAddress, inboxQ[inboxQMessageNumber].packet.originalDestinationAddress, inboxQ[inboxQMessageNumber].packet.originalSourceAddress); // address duco
	outboxQ[outboxQMessageNumber].packet.counter = inboxQ[inboxQMessageNumber].packet.counter;
	ducoToCC1101Packet(&outboxQ[outboxQMessageNumber].packet, &outMessage);

	sendDataToDuco(&outMessage,outboxQMessageNumber);
	setLogMessage(F("SEND ACK done!"));
}

void DucoCC1101::prefillDucoPacket(DucoPacket *ducoOutPacket, uint8_t destinationAddress){
	prefillDucoPacket(ducoOutPacket, destinationAddress, this->deviceAddress, destinationAddress);
}


void DucoCC1101::prefillDucoPacket(DucoPacket *ducoOutPacket, uint8_t destinationAddress, uint8_t originalSourceAddress, uint8_t originalDestinationAddress ){
	ducoOutPacket->sourceAddress =  this->deviceAddress;
	ducoOutPacket->destinationAddress = destinationAddress;
	ducoOutPacket->originalSourceAddress =  originalSourceAddress;
	ducoOutPacket->originalDestinationAddress = originalDestinationAddress;
	memcpy(ducoOutPacket->networkId,networkId,4 );
}


// set commandLength in commandsLength
// commandlength is het command + response bytes
// commandsLength bestaat uit 2 bytes met daarin het aantal bytes van 4 commando's
// commandNumbers = 0 - 3 
// commandlength = 0 - 15 dec 
void DucoCC1101::setCommandLength(DucoPacket *ducoOutPacket, uint8_t commandNumber, uint8_t commandLength){
	uint8_t commandByte  = 0;
	uint8_t bitmask = 0;

	if(commandNumber > 1){ 
		commandByte = 1; 
		commandNumber = commandNumber - 2 ;
	}

	if(commandNumber == 0){
		bitmask = 0b11110000; // = 0xF0
		commandLength = commandLength << 4; // move length 4 bytes to left
	}else if(commandNumber == 1){
		bitmask = 0b00001111; // = 0x0F
	}

		ducoOutPacket->data[commandByte] = ~(~ducoOutPacket->data[commandByte] | bitmask); // remove 4 bits from current value
		ducoOutPacket->data[commandByte] |= commandLength & bitmask;  // Put  4 bits from original in result.
}


// pass two bytes (commandLengthByte1 & 2 ) and the command number (1-4) to this function and received the length in bytes
uint8_t DucoCC1101::getCommandLength(uint8_t commandLengthByte1, uint8_t commandLengthByte2, uint8_t commandNumber){

	if(commandNumber > 1){ 
		commandLengthByte1 = commandLengthByte2; // temporarily move commandLengthByte2 to commandLengthByte1  (saves a byte of ram :) 
		commandNumber = commandNumber - 2 ;
	}

	if(commandNumber == 0){
		return (commandLengthByte1 >> 4);
	}else if(commandNumber == 1){
		return (commandLengthByte1 & 0b00001111);
	}
}



void DucoCC1101::ducoToCC1101Packet(DucoPacket *duco, CC1101Packet *packet)
{
	packet->data[0] = 0;
	packet->data[1] = duco->messageType;
	packet->data[2] = duco->networkId[0];	
	packet->data[3] = duco->networkId[1];	
	packet->data[4] = duco->networkId[2];	
	packet->data[5] = duco->networkId[3];	

	//address
	packet->data[6] = (duco->sourceAddress << 3) | (duco->destinationAddress >> 2);
	packet->data[7] = (duco->destinationAddress << 6) | (duco->originalSourceAddress << 1) | (duco->originalDestinationAddress >> 4);
	packet->data[8] = (duco->originalDestinationAddress << 4) | duco->counter;

	// RSSI value for network packets and repeated packets
	packet->data[9] = duco->rssi; 

	for(uint8_t i=0; i < duco->dataLength; i++){
		packet->data[10+i] = duco->data[i];
	}

	packet->length = 10 + duco->dataLength;
}



// Command from DUCOBOX to node to change ventilationmode: 0x20 0x00 0x12
// Byte 4:
//				bit 0-3: 0000 (always 0000)
//				bit 4  : PERMANENTE STAND (1) of tijdelijk (0) in combinatie met bit 5-7
//				bit 5-7: 000 (0) = auto, 100 (4) = LOW, 101 (5) = MIDDLE, 110 (6) = HIGH, 111 (7) = not home
//
bool DucoCC1101::processNewVentilationMode(uint8_t inboxQMessageNumber, uint8_t commandNumber, uint8_t startByteCommand){
	setLogMessage(F("processNewVentilationMode();"));

	uint8_t newVentilationMode = 0x00;
	bool newPermanentVentilationMode; 
 
	// check for valid ventilationmode (0-4)
	if( (inboxQ[inboxQMessageNumber].packet.data[ (startByteCommand +1) ] & 0b00000111) <= 7){
		newVentilationMode = (inboxQ[inboxQMessageNumber].packet.data[ (startByteCommand +1) ] & 0b00000111);
		newPermanentVentilationMode = (inboxQ[inboxQMessageNumber].packet.data[ (startByteCommand +1) ] & 0b00001000);
		if(this->currentVentilationMode != newVentilationMode || this->permanentVentilationMode != newPermanentVentilationMode){
			this->currentVentilationMode = newVentilationMode;
			this->permanentVentilationMode = permanentVentilationMode;
			return true;
		}
	}
	return false;
}



void DucoCC1101::requestVentilationMode(uint8_t ventilationMode, bool setPermanentVentilationMode, uint8_t percentage, uint8_t buttonPresses){
	setLogMessage(F("requestVentilationMode();"));

	if(ventilationMode <= 7){
		// TODO: store percentage in memory till ventilationmode changes?

		sendVentilationModeMessage(setPermanentVentilationMode, true, ventilationMode, percentage, this->temperature, false, buttonPresses);
	}else{
			setLogMessage(F("Invalid ventilationmode requested."));
	}
}


// Command change ventilationMode: 0x60 0x00 0x22
// recente bedieningsschakelaars gebruiken 0x60 0x00 0x22
// Byte 3: Sensor request percentage  (0-100%)
// Byte 4:
//				bit 0-1: 00 (always 00)
//				bit 2  : PERMANENTE STAND (1) of tijdelijk (0) in combinatie met bit 5-7 (LOW, MIDDLE, HIGH) - niet voor afwezigheidsstand!
//				bit 3  : installation mode on (1) 
//				bit 4  : Ventilatiestand instellen (1) of bevestigen (0)
//				bit 5-7: 000 (0) = auto, 100 (4) = LOW, 101 (5) = MIDDLE, 110 (6) = HIGH, 111 (7) = afwezigheidsstand 

// Byte 5: temperature (-100)
// Byte 6: unknown (default: 0x00)
// Byte 7: 
//				bit 0-2	: 000 (always 000)
//				bit 3-4	: aantal keer knop ingedrukt (01 = 1x; 10 = 2x; 11 = 3x ingedrukt);
//				bit 5	: knop lang ingedrukt (0 = kort ingedrukt, 1 = lang ingedrukt) - geldt alleen voor permanent low,middle,high
//				bit 6-7	: knopnummer (01 = knop 1, 10 = knop 2, 11 = knop 3)

void DucoCC1101::sendVentilationModeMessage(bool setPermanent, bool setVentilationMode, uint8_t ventilationMode, uint8_t percentage, uint8_t temp, bool activateInstallerMode, uint8_t buttonPresses){
	setLogMessage(F("sendVentilationModeMessage();"));

	// get a free spot in OutboxQ
	uint8_t outboxQMessageNumber = getOutboxQFreeSpot();
	if(outboxQMessageNumber == 255){
		setLogMessage(F("No free space in outboxQ, dropping message;"));
		return;
	}
	resetOutDucoPacket(outboxQMessageNumber);

	prepareVentilationModeMessage(outboxQMessageNumber, 0, setPermanent, setVentilationMode, ventilationMode, percentage, temp, activateInstallerMode, buttonPresses);

	prefillDucoPacket(&outboxQ[outboxQMessageNumber].packet, 0x01); // address duco

	outboxQ[outboxQMessageNumber].packet.counter = updateMessageCounter();
	ducoToCC1101Packet(&outboxQ[outboxQMessageNumber].packet, &outMessage);

	sendDataToDuco(&outMessage,outboxQMessageNumber);
	waitForAck(outboxQMessageNumber);

}



void DucoCC1101::prepareVentilationModeMessage(uint8_t outboxQMessageNumber, uint8_t commandNumber, bool setPermanent, bool setVentilationMode, uint8_t ventilationMode, uint8_t percentage, uint8_t temp, bool activateInstallerMode, uint8_t buttonPresses){
	uint8_t permanentModeBit = setPermanent ? 0x20 : 0x00; // 0x20 = 0010.0000
	uint8_t installerModeBit = activateInstallerMode ? 0x10 : 0x00; // 0x10 = 0001.0000
	uint8_t setVentilationModeBit = setVentilationMode ? 0x08 : 0x00; // 0x08 = 0000.1000


	// define which button is pressed. 00 = auto, 01 = button 1 (low), 02 = button 2 (middle), 03 = button 3 (high)
	uint8_t pressedButton = 0x00;
	switch (ventilationMode){
		case 0x00: pressedButton = 0x00; break;
		case 0x04: pressedButton = 0x01; break;
		case 0x05: pressedButton = 0x02; break;
		case 0x06: pressedButton = 0x03; break;
		case 0x07: pressedButton = 0x01; break; // NOTHOME
	}

	outboxQ[outboxQMessageNumber].packet.messageType = ducomsg_message;

	setCommandLength(&outboxQ[outboxQMessageNumber].packet, commandNumber, 6); // set commandlength of command, 6 byte.

	outboxQ[outboxQMessageNumber].packet.data[outboxQ[outboxQMessageNumber].packet.dataLength++] = 0x22; // commando: ventilatiestand wijzigen/bevestigen
	outboxQ[outboxQMessageNumber].packet.data[outboxQ[outboxQMessageNumber].packet.dataLength++] = percentage;
	outboxQ[outboxQMessageNumber].packet.data[outboxQ[outboxQMessageNumber].packet.dataLength++] = (permanentModeBit | installerModeBit | setVentilationModeBit | ventilationMode); // 0x08 = 0000.1000
	outboxQ[outboxQMessageNumber].packet.data[outboxQ[outboxQMessageNumber].packet.dataLength++] = (temp -100); // temp = 21.0c = 210 - 100 = 110 (dec) = 0x6E
	outboxQ[outboxQMessageNumber].packet.data[outboxQ[outboxQMessageNumber].packet.dataLength++] = 0x00; // unknown
	if(setVentilationMode){
		outboxQ[outboxQMessageNumber].packet.data[outboxQ[outboxQMessageNumber].packet.dataLength++] =  ( (buttonPresses << 3) | (setPermanent ? 0b00000100 : 0x00) | pressedButton);
	}else{
		 // if we confirm the ventilationmode, byte 18 is 0x00.
		outboxQ[outboxQMessageNumber].packet.data[outboxQ[outboxQMessageNumber].packet.dataLength++] =  0x00;
	}

	return;
}





void DucoCC1101::setTemperature(int newTemperature) {  
	if(newTemperature >= 100 && newTemperature <= 355){
		this->temperature = newTemperature;
	}else{
		setLogMessage(F("Can't set temperature because temperature isnt in accepted range (100 - 355)."));
	}
}


void DucoCC1101::enableInstallerMode(){
	sendVentilationModeMessage(this->permanentVentilationMode, false, this->currentVentilationMode, 0, this->temperature, true, 0);
	setLogMessage(F("Send enableInstallerMode done!"));
}


void DucoCC1101::disableInstallerMode(){

	// get a free spot in OutboxQ
	uint8_t outboxQMessageNumber = getOutboxQFreeSpot();
	if(outboxQMessageNumber == 255){
		setLogMessage(F("No free space in outboxQ, dropping message;"));
		return;
	}
	resetOutDucoPacket(outboxQMessageNumber);

	outboxQ[outboxQMessageNumber].packet.messageType = ducomsg_network;

	outboxQ[outboxQMessageNumber].packet.data[0] = 0x00; // 0x00 = disable installer mode, 0x01 = installermode is enabled
	outboxQ[outboxQMessageNumber].packet.dataLength = 1;
 
	prefillDucoPacket(&outboxQ[outboxQMessageNumber].packet, 0x01); // address duco
	outboxQ[outboxQMessageNumber].packet.counter = updateMessageCounter();
	ducoToCC1101Packet(&outboxQ[outboxQMessageNumber].packet, &outMessage);

	sendDataToDuco(&outMessage,outboxQMessageNumber);
	setLogMessage(F("SEND disableInstallerMode done!"));

	waitForAck(outboxQMessageNumber);
}



/* TEST FUNCTIONS! */

uint8_t DucoCC1101::TEST_getVersion(){
	uint8_t version = readRegister(0xF1);
	return version;
}

uint8_t DucoCC1101::TEST_getPartnumber(){
	uint8_t version = readRegister(0xF0);
	return version;
}

void DucoCC1101::TEST_GDOTest(){
	writeRegister(CC1101_IOCFG0 ,0x2E);	//High impedance (3-state)
	writeRegister(CC1101_IOCFG1 ,0x2E); //High impedance (3-state)
	writeRegister(CC1101_IOCFG2 ,0x3F);	// 135-141 kHz clock output (XOSC frequency divided by 192).
}



void DucoCC1101::sendTestMessage(){

	// get a free spot in OutboxQ
	uint8_t outboxQMessageNumber = getOutboxQFreeSpot();
	if(outboxQMessageNumber == 255){
		setLogMessage(F("No free space in outboxQ, dropping message;"));
		return;
	}
	resetOutDucoPacket(outboxQMessageNumber);

	outboxQ[outboxQMessageNumber].packet.messageType = ducomsg_message;

	outboxQ[outboxQMessageNumber].packet.data[0] = 0x47; // G
	outboxQ[outboxQMessageNumber].packet.data[1] = 0x57; // W
	outboxQ[outboxQMessageNumber].packet.data[2] = 0x54; // T
	outboxQ[outboxQMessageNumber].packet.data[3] = 0x45; // E
	outboxQ[outboxQMessageNumber].packet.data[4] = 0x53; // S
	outboxQ[outboxQMessageNumber].packet.data[5] = 0x54; // T

	outboxQ[outboxQMessageNumber].packet.dataLength = 6;
 
	prefillDucoPacket(&outboxQ[outboxQMessageNumber].packet, 0x01); // address duco

	outboxQ[outboxQMessageNumber].packet.counter = 0x08;
	ducoToCC1101Packet(&outboxQ[outboxQMessageNumber].packet, &outMessage);

	sendDataToDuco(&outMessage,outboxQMessageNumber);
	return;
}


void DucoCC1101::sendRawPacket(uint8_t messageType, uint8_t sourceAddress, uint8_t destinationAddress, uint8_t originalSourceAddress, uint8_t originalDestinationAddress, uint8_t *data, uint8_t length){

	// get a free spot in OutboxQ
	uint8_t outboxQMessageNumber = getOutboxQFreeSpot();
	if(outboxQMessageNumber == 255){
		setLogMessage(F("No free space in outboxQ, dropping message;"));
		return;
	}
	resetOutDucoPacket(outboxQMessageNumber);

	outboxQ[outboxQMessageNumber].packet.sourceAddress =  sourceAddress;
	outboxQ[outboxQMessageNumber].packet.destinationAddress = destinationAddress;
	outboxQ[outboxQMessageNumber].packet.originalSourceAddress =  originalSourceAddress;
	outboxQ[outboxQMessageNumber].packet.originalDestinationAddress = originalDestinationAddress;
	outboxQ[outboxQMessageNumber].packet.messageType = messageType;

	for(uint8_t i=0; i < length;i++){
		outboxQ[outboxQMessageNumber].packet.data[i] = data[i];
	}

	outboxQ[outboxQMessageNumber].packet.dataLength = length;

	memcpy(outboxQ[outboxQMessageNumber].packet.networkId,this->networkId,4);
	
	updateMessageCounter();
	outboxQ[outboxQMessageNumber].packet.counter = this->messageCounter;

	ducoToCC1101Packet(&outboxQ[outboxQMessageNumber].packet, &outMessage);

	sendDataToDuco(&outMessage,outboxQMessageNumber);
	setLogMessage(F("Send raw Duco packet done!"));
}


