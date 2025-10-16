#include <SPI.h>
#include "MPU6050.h"

#define CS_PIN 10

// Create MPU6050 object in SPI mode
MPU6050 mpu(CS_PIN);  

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Initialize SPI
  SPI.begin();

  Serial.println("Initializing MPU-6500 over SPI...");
  mpu.initialize_SPI(); // Use SPI init version

  // Check connection
  if (mpu.testConnection()) {
    Serial.println("MPU-6500 connection successful!");
  } else {
    Serial.println("MPU-6500 connection failed!");
    while (1);
  }
}

void loop() {
  int16_t ax, ay, az, gx, gy, gz;
  
  // Read raw accel and gyro
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  Serial.print("a/g:\t");
  Serial.print(ax); Serial.print("\t");
  Serial.print(ay); Serial.print("\t");
  Serial.print(az); Serial.print("\t");
  Serial.print(gx); Serial.print("\t");
  Serial.print(gy); Serial.print("\t");
  Serial.println(gz);

  delay(200);
}

