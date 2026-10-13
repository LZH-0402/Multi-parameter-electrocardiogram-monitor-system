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
         case 1:delay_t=110;break;
       }
 
        delay(delay_t); 
}
void loop() {
  unsigned char data[140];
  
  uint8_t sum=0,i=0;
  iic_read(0X09,&data[4],130);
 data[0]=0xa4;
 data[1]=0x03;
 data[2]=0x09;
 data[3]=130;
   for( i=0;i<130+4;i++)
   {
     Serial.write(data[i]);
      sum+=data[i];
   }
     Serial.write(sum);
    //delay a second
 
 
   delay(200); 
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

