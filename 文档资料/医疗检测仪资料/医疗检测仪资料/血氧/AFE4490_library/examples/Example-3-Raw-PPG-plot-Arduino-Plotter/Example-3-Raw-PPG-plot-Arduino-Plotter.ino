/**
 * @file Example-3-Raw-PPG-plot-Arduino-Plotter.ino
 * @brief  Arduino Plotter显示原始脉搏波形
 * @version 0.1
 * @date 2023-07-03
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#include <SPI.h>
#include "S_afe44xx.h"

#define AFE44XX_CS_PIN   7
#define AFE44XX_PWDN_PIN 4
#define AFE44XX_INTNUM   0

AFE44XX afe44xx(AFE44XX_CS_PIN, AFE44XX_PWDN_PIN);

afe44xx_data afe44xx_raw_data;

void setup()
{
  Serial.begin(57600);
  Serial.println("Intilaziting AFE44xx.. ");
  
  SPI.begin();
  afe44xx.afe44xx_init();
  Serial.println("Inited...");
}

void loop()
{
  afe44xx.get_AFE44XX_Data(&afe44xx_raw_data);
    
  Serial.println(afe44xx_raw_data.RED_data);  
  delay(8); 

}
