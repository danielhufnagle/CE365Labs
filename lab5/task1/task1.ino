#include "ESP_I2S.h"
#include "FS.h"
#include "SD.h"

void setup() {
  // Create an instance of the I2SClass
  I2SClass i2s;

  // Create variables to store the audio data
  uint8_t *wav_buffer;
  size_t wav_size;

  // Initialize the serial port
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("Initializing I2S bus...");

  // Set up the pins used for audio input
  i2s.setPinsPdmRx(42, 41);

  // start I2S at 16 kHz with 16-bits per sample
  if (!i2s.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("Failed to initialize I2S!");
    while (1); // do nothing
  }

  Serial.println("I2S bus initialized.");
  Serial.println("Initializing SD card...");

  // Set up the pins used for SD card access
  if(!SD.begin(21)){
    Serial.println("Failed to mount SD Card!");
    while (1) ;
  }

  for (int i = 0; i <= 4; i++) {
    Serial.println("SD card initialized.");
    Serial.println("Beginning recording in 3");
    delay(1000);
    Serial.println("2");
    delay(1000);
    Serial.println("1");
    Serial.println("Recording 3 seconds of audio data...");

    // Record 20 seconds of audio data
    wav_buffer = i2s.recordWAV(3, &wav_size);

    String filename = "/" + String(i) + ".wav";
    Serial.println(filename);
    // Create a file on the SD card
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file for writing!");
      return;
    }

    Serial.println("Writing audio data to file...");

    // Write the audio data to the file
    if (file.write(wav_buffer, wav_size) != wav_size) {
      Serial.println("Failed to write audio data to file!");
      return;
    }

    // Close the file
    file.close();
  }
  Serial.println("Application complete.");
}

void loop() {
  delay(1000);
  Serial.printf(".");
}