#include <i2cmaster.h>

/////////////////////////////////
/*
GY-614-----mini
VCC----VCC
SCL----A5
SDA----A4
GND--GND
*/
/////////////////////////////////

//////////////////////////////////
#define uint16_t unsigned int
#define iic_add  0xa4
typedef struct
{
    float e;
    float to;
    float ta;
    float toir;
} gyir;
unsigned char Re_buf;
unsigned char sign=0;
gyir my_ir;
uint16_t delay_t=0;
byte color=0,rgb_data[3]={0};

void setup() {
      byte td=0;
       Serial.begin(115200);
       i2c_init();
       iic_read(0x02,&td,1);
       switch(td)
       {
         case 0:delay_t=1010;break;
         case 1:delay_t=510;break;
         case 2:delay_t=210;break;
         case 3:delay_t=110;break;
       }
        delay(1); 
        iic_write(0x07,95);
        delay(1); 
         iic_write(0x06,95);
        delay(delay_t); 
}
void loop() {
  unsigned char data[16]={0};
 if(!sign)
 {
   iic_read(0x07,data,7);
   my_ir.to=(data[1]<<8)|data[2];
   my_ir.ta=(data[3]<<8)|data[4];
   my_ir.toir=(data[5]<<8)|data[6];
   my_ir.e=data[0];
    Serial.print("e: ");
   Serial.print(my_ir.e/100);
   Serial.print(",to: ");
   Serial.print( my_ir.to/100);
    Serial.print(",ta:");
    Serial.print( my_ir.ta/100);
     Serial.print(",toir:");
    Serial.println(my_ir.toir/100);
   
 }
 
   delay(delay_t); 
}
void iic_read(unsigned char add,unsigned char *data,unsigned char len)
{
  i2c_start_wait(iic_add);
   i2c_write(add);
   i2c_start_wait(iic_add+1);
   while(len-1)
   {
    *data++=i2c_readAck();
    len--;
    }
    *data=i2c_readNak();
    i2c_stop();
}
void iic_write( char add,unsigned char data)
{
  i2c_start_wait(iic_add);
   i2c_write(add);
   i2c_write(data);
    i2c_stop();
}

