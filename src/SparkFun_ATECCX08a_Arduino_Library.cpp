/*
  This is a library written for the ATECCX08A Criptographic Co-Processor (QWIIC).

  Written by Pete Lewis @ SparkFun Electronics, August 5th, 2019

  The IC uses I2C and 1-wire to communicat. This library only supports I2C.

  https://github.com/sparkfun/SparkFun_ATECCX08A_Arduino_Library

  Do you like this library? Help support SparkFun. Buy a board!

  Development environment specifics:
  Arduino IDE 1.8.1

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "SparkFun_ATECCX08A_Arduino_Library.h"

//Returns false if IC does not respond

#if defined(__MK64FX512__) || defined(__MK66FX1M0__)
	//Teensy 3.6
boolean ATECCX08A::begin(uint8_t i2caddr, i2c_t3 &wirePort)
{
  //Bring in the user's choices
  _i2cPort = &wirePort; //Grab which port the user wants us to use

  _i2caddr = i2caddr;

  return ( wakeUp() ); // see if the IC wakes up properly, return responce.
}
#else

boolean ATECCX08A::begin(uint8_t i2caddr, TwoWire &wirePort)
{
  //Bring in the user's choices
  _i2cPort = &wirePort; //Grab which port the user wants us to use

  _i2caddr = i2caddr;

  return ( wakeUp() ); // see if the IC wakes up properly, return responce.
}
#endif


/** \brief 

	wakeUp()
	
	This function wakes up the ATECCX08a IC
	Returns TRUE if the IC responds with correct verification
	Message (0x04, 0x11, 0x33, 0x44) 
	The actual status byte we are looking for is the 0x11.
	The complete message is as follows:
	COUNT, DATA, CRC[0], CRC[1].
	0x11 means that it received the wake condition and is goat to goo.
	
	Note, in most SparkFun Arduino Libraries, we would use a different
	function called isConnected(), but because this IC will ACK and
	respond with a status, we are gonna use wakeUp() for the same purpose.
*/

boolean ATECCX08A::wakeUp()
{
  _i2cPort->beginTransmission(0x00); // set up to write to address "0x00",
  // This creates a "wake condition" where SDA is held low for at least tWLO
  // tWLO means "wake low duration" and must be at least 60 uSeconds (which is acheived by writing 0x00 at 100KHz I2C)
  _i2cPort->endTransmission(); // actually send it

  delayMicroseconds(1500); // required for the IC to actually wake up.
  // 1500 uSeconds is minimum and known as "Wake High Delay to Data Comm." tWHI, and SDA must be high during this time.

  // Now let's read back from the IC and see if it reports back good things.
  countGlobal = 0; 
  if(receiveResponseData(4) == false) return false;
  if(checkCount() == false) return false;
  if(checkCrc() == false) return false;
  if(inputBuffer[1] == 0x11) return true;   // If we hear a "0x11", that means it had a successful wake up.
  else return false;
}

/** \brief

	idleMode()
	
	The ATECCX08A goes into the idle mode and ignores all subsequent I/O transitions
	until the next wake flag. The contents of TempKey and RNG Seed registers are retained.
	Idle Power Supply Current: 800uA.
	Note, it will automatically go into sleep mode after watchdog timer has been reached (1.3-1.7sec).
*/

void ATECCX08A::idleMode()
{
  _i2cPort->beginTransmission(_i2caddr); // set up to write to address
  _i2cPort->write(WORD_ADDRESS_VALUE_IDLE); // enter idle command (aka word address - the first part of every communication to the IC)
  _i2cPort->endTransmission(); // actually send it  
}

/** \brief

	getInfo()
	
	This function sends the INFO Command and listens for the correct version (0x50) within the response.
	The Info command has a mode parameter, and in this function we are using the "Revision" mode (0x00)
	At the time of data sheet creation the Info command will return 0x00 0x00 0x50 0x00. For
	all versions of the ECC508A the 3rd byte will always be 0x50. The fourth byte will indicate the
	silicon revision.
*/

boolean ATECCX08A::getInfo()
{
  // build packet array to complete a communication to IC
  // It expects to see word address, count, command, param1, param2a, param2b, CRC[0], CRC[1].
  uint8_t count = 0x07;
  uint8_t command = COMMAND_OPCODE_INFO;
  uint8_t param1 = 0x00; // "Revision mode"
  uint8_t param2a = 0x00;
  uint8_t param2b = 0x00;

  // update CRCs
  uint8_t packet_to_CRC[] = {count, command, param1, param2a, param2b};
  atca_calculate_crc((count - 2), packet_to_CRC); // count includes crc[0] and crc[1], so we must subtract 2 before creating crc
  //Serial.println(crc[0], HEX);
  //Serial.println(crc[1], HEX);

  // create complete message using newly created/updated crc values
  byte complete_message[9] = {WORD_ADDRESS_VALUE_COMMAND, count, command, param1, param2a, param2b, crc[0], crc[1]};

  wakeUp();
 
  _i2cPort->beginTransmission(_i2caddr);
  _i2cPort->write(complete_message, 8);
  _i2cPort->endTransmission();

  delay(1); // time for IC to process command and exectute
  
    // Now let's read back from the IC and see if it reports back good things.
  countGlobal = 0; 
  if(receiveResponseData(7, true) == false) return false;
  idleMode();
  if(checkCount() == false) return false;
  if(checkCrc() == false) return false;
  if(inputBuffer[3] == 0x50) return true;   // If we hear a "0x50", that means it had a successful version response.
  else return false;
}


/** \brief

	updateRandom32Bytes(boolean debug)
	
    This function pulls a complete random number (all 32 bytes)
    It stores it in a global array called random32Bytes[]
    If you wish to access this global variable and use as a 256 bit random number,
    then you will need to access this array and combine it's elements as you wish.
    In order to keep compatibility with ATmega328 based arduinos,
    We have offered some other functions that return variables more usable (i.e. byte, int, long)
    They are getRandomByte(), getRandomInt(), and getRandomLong().
*/

boolean ATECCX08A::updateRandom32Bytes(boolean debug)
{
  // build packet array to complete a communication to IC
  // It expects to see word address, count, command, param1, param2, CRC1, CRC2
  uint8_t count = 0x07;
  uint8_t command = COMMAND_OPCODE_RANDOM;
  uint8_t param1 = 0x00;
  uint8_t param2a = 0x00;
  uint8_t param2b = 0x00;

  // update CRCs
  uint8_t packet_to_CRC[] = {count, command, param1, param2a, param2b};
  atca_calculate_crc((count - 2), packet_to_CRC); // count includes crc[0] and crc[1], so we must subtract 2 before creating crc
  //Serial.println(crc[0], HEX);
  //Serial.println(crc[1], HEX);

  // create complete message using newly created/updated crc values
  byte complete_message[9] = {WORD_ADDRESS_VALUE_COMMAND, count, command, param1, param2a, param2b, crc[0], crc[1]};

  wakeUp();
  
  _i2cPort->beginTransmission(_i2caddr);
  _i2cPort->write(complete_message, 8);
  _i2cPort->endTransmission();

  delay(23); // time for IC to process command and exectute

  // Now let's read back from the IC. This will be 35 bytes of data (count + 32_data_bytes + crc[0] + crc[1])

  if(receiveResponseData(35, debug) == false) return false;
  idleMode();
  if(checkCount(debug) == false) return false;
  if(checkCrc(debug) == false) return false;
  
  
  // update random32Bytes[] array
  // we don't need the count value (which is currently the first byte of the inputBuffer)
  for (int i = 0 ; i < 32 ; i++) // for loop through to grab all but the first position (which is "count" of the message)
  {
    random32Bytes[i] = inputBuffer[i + 1];
  }

  if(debug)
  {
    Serial.print("random32Bytes: ");
    for (int i = 0; i < sizeof(random32Bytes) ; i++)
    {
      Serial.print(random32Bytes[i], HEX);
      Serial.print(",");
    }
    Serial.println();
  }
  
  return true;
}

/** \brief

	getRandomByte(boolean debug)
	
    This function returns a random byte.
	It calls updateRandom32Bytes(), then uses the first byte in that array for a return value.
*/

byte ATECCX08A::getRandomByte(boolean debug)
{
  updateRandom32Bytes(debug);
  return random32Bytes[0];
}

/** \brief

	getRandomInt(boolean debug)
	
    This function returns a random Int.
	It calls updateRandom32Bytes(), then uses the first 2 bytes in that array for a return value.
	It bitwize ORS the first two bytes of the array into the return value.
*/

int ATECCX08A::getRandomInt(boolean debug)
{
  updateRandom32Bytes(debug);
  int return_val;
  return_val = random32Bytes[0]; // store first randome byte into return_val
  return_val <<= 8; // shift it over, to make room for the next byte
  return_val |= random32Bytes[1]; // "or in" the next byte in the array
  return return_val;
}

/** \brief

	getRandomLong(boolean debug)
	
    This function returns a random Long.
	It calls updateRandom32Bytes(), then uses the first 4 bytes in that array for a return value.
	It bitwize ORS the first 4 bytes of the array into the return value.
*/

long ATECCX08A::getRandomLong(boolean debug)
{
  updateRandom32Bytes(debug);
  long return_val;
  return_val = random32Bytes[0]; // store first randome byte into return_val
  return_val <<= 8; // shift it over, to make room for the next byte
  return_val |= random32Bytes[1]; // "or in" the next byte in the array
  return_val <<= 8; // shift it over, to make room for the next byte
  return_val |= random32Bytes[2]; // "or in" the next byte in the array
  return_val <<= 8; // shift it over, to make room for the next byte
  return_val |= random32Bytes[3]; // "or in" the next byte in the array
  return return_val;
}



/** \brief

	receiveResponseData(uint8_t length, boolean debug)
	
	This function receives messages from the ATECCX08a IC (up to 32 Bytes)
	It will return true if it receives the correct amount of data and good CRCs.
	What we hear back from the IC is always formatted with the following series of bytes:
	COUNT, DATA, CRC[0], CRC[1]
	Note, the count number includes itself, the num of data bytes, and the two CRC bytes in the total, 
	so a simple response message from the IC that indicates that it heard the wake 
	condition properly is like so:
	EXAMPLE Wake success response: 0x04, 0x11, 0x33, 0x44
	It needs length argument:
	length: length of data to receive (includes count + DATA + 2 crc bytes)
*/

boolean ATECCX08A::receiveResponseData(uint8_t length, boolean debug)
{	

  // pull in data 32 bytes at at time. (necessary to avoid overflow on atmega328)
  // if length is less than or equal to 32, then just pull it in.
  // if length is greater than 32, then we must first pull in 32, then pull in remainder.
  // lets use length as our tracker and we will subtract from it as we pull in data.
  
  countGlobal = 0; // reset for each new message (most important, like wensleydale at a cheese party)
  cleanInputBuffer();
  
  while(length)
  {
    byte requestAmount; // amount of bytes to request, needed to pull in data 32 bytes at a time (for AVR atmega328s)  
	if(length > 32) requestAmount = 32; // as we have more than 32 to pull in, keep pulling in 32 byte chunks
	else requestAmount = length; // now we're ready to pull in the last chunk.
	_i2cPort->requestFrom(_i2caddr, requestAmount);    // request bytes from slave

	while (_i2cPort->available())   // slave may send less than requested
	{
	  inputBuffer[countGlobal] = _i2cPort->read();    // receive a byte as character
	  length--; // keep this while loop active until we've pulled in everything
	  countGlobal++; // keep track of the count of the entire message.
	}  
  }

  if(debug)
  {
    Serial.print("inputBuffer: ");
	for (int i = 0; i < countGlobal ; i++)
	{
	  Serial.print(inputBuffer[i], HEX);
	  Serial.print(",");
	}
	Serial.println();	  
  }
  return true;
}

/** \brief

	checkCount(boolean debug)
	
	This function checks that the count byte received in the most recent message equals countGlobal
	Use it after you call receiveResponseData as many times as you need,
	and then finally you can check the count of the complete message.
*/

boolean ATECCX08A::checkCount(boolean debug)
{
  if(debug)
  {
    Serial.print("countGlobal: 0x");
	Serial.println(countGlobal, HEX);
	Serial.print("count heard from IC (inpuBuffer[0]): 0x");
    Serial.println(inputBuffer[0], HEX);
  }
  // Check count; the first byte sent from IC is count, and it should be equal to the actual message count
  if(inputBuffer[0] != countGlobal) 
  {
	Serial.println("Message Count Error");
	return false;
  }  
  return true;
}

/** \brief

	checkCrc(boolean debug)
	
	This function checks that the CRC bytes received in the most recent message equals a calculated CRCs
	Use it after you call receiveResponseData as many times as you need,
	and then finally you can check the CRCs of the complete message.
*/

boolean ATECCX08A::checkCrc(boolean debug)
{
  // Check CRC[0] and CRC[1] are good to go.
  
  atca_calculate_crc(countGlobal-2, inputBuffer);   // first calculate it
  
  if(debug)
  {
    Serial.print("CRC[0] Calc: 0x");
	Serial.println(crc[0], HEX);
	Serial.print("CRC[1] Calc: 0x");
    Serial.println(crc[1], HEX);
  }
  
  if( (inputBuffer[countGlobal-1] != crc[1]) || (inputBuffer[countGlobal-2] != crc[0]) )   // then check the CRCs.
  {
	Serial.println("Message CRC Error");
	return false;
  }
  
  return true;
}

/** \brief

	atca_calculate_crc(uint8_t length, uint8_t *data)
	
    This function calculates CRC.
    It was copied directly from the App Note provided from Microchip.
    Note, it seems to be their own unique type of CRC cacluation.
    View the entire app note here:
    http://ww1.microchip.com/downloads/en/AppNotes/Atmel-8936-CryptoAuth-Data-Zone-CRC-Calculation-ApplicationNote.pdf
    \param[in] length number of bytes in buffer
    \param[in] data pointer to data for which CRC should be calculated
*/

void ATECCX08A::atca_calculate_crc(uint8_t length, uint8_t *data)
{
  uint8_t counter;
  uint16_t crc_register = 0;
  uint16_t polynom = 0x8005;
  uint8_t shift_register;
  uint8_t data_bit, crc_bit;
  for (counter = 0; counter < length; counter++) {
    for (shift_register = 0x01; shift_register > 0x00; shift_register <<= 1) {
      data_bit = (data[counter] & shift_register) ? 1 : 0;
      crc_bit = crc_register >> 15;
      crc_register <<= 1;
      if (data_bit != crc_bit)
        crc_register ^= polynom;
    }
  }
  crc[0] = (uint8_t) (crc_register & 0x00FF);
  crc[1] = (uint8_t) (crc_register >> 8);
}


/** \brief

	cleanInputBuffer()
	
    This function sets the entire inputBuffer to zeros.
	It is helpful for debugging message/count/CRCs errors.
*/

void ATECCX08A::cleanInputBuffer()
{
  for (int i = 0; i < sizeof(inputBuffer) ; i++)
  {
    inputBuffer[i] = 0;
  }
}