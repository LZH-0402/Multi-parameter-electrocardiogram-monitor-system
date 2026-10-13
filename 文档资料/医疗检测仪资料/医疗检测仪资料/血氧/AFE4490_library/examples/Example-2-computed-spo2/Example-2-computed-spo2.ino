/**
 * @file Example-2-computed-spo2.ino
 * @brief  计算血氧饱和度
 * 
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
int32_t heart_rate_prev=0;
int32_t spo2_prev=0;

void setup()
{
  Serial.begin(115200);
  Serial.println("Intilaziting AFE44xx.. ");
  
  SPI.begin();
  afe44xx.afe44xx_init();
  Serial.println("Inited...");
}

void loop()
{
    delay(8);
    
    afe44xx.get_AFE44XX_Data(&afe44xx_raw_data);
        
    if (afe44xx_raw_data.buffer_count_overflow)
    {
      if(afe44xx_raw_data.spo2 == -999)
      {
        Serial.println("Probe error!!!!");
      }
      else if ((heart_rate_prev != afe44xx_raw_data.heart_rate) || (spo2_prev != afe44xx_raw_data.spo2))
      {
        heart_rate_prev = afe44xx_raw_data.heart_rate;
        spo2_prev = afe44xx_raw_data.spo2;

        Serial.print("calculating sp02...");
        Serial.print(" Sp02 : ");
        Serial.print(afe44xx_raw_data.spo2);
        Serial.print("% ,");
        Serial.print("Pulse rate :");
        Serial.print(afe44xx_raw_data.heart_rate);
        Serial.println(" bpm");
      } 
    }          
}
