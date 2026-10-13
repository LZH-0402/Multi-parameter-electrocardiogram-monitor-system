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
  unsigned char data[64]={0},data2[64]={0};
  float to64[64];
  iic_read(0X0B,data,64);
 Serial.print("[");
    for(int i=1; i<=64; i++){
      Serial.print(data[i-1]);
      Serial.print(", ");
      if( i%8 == 0 ) Serial.println();
    }
    Serial.println("]");
    Serial.println();
    iic_read(0X4B,data2,64);
     Serial.print("[");
    for(int i=1; i<=64; i++){
      to64[i-1]=data[i-1]+(float)data2[i-1]*0.25;
      
      Serial.print(to64[i-1]);
      Serial.print(", ");
      if( i%8 == 0 ) Serial.println();
    }
    Serial.println("]");
    //delay a second
 
 
   delay(1000); 
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

