/* QMI8658 acceleration only. Register facts: Waveshare components010c5fb3,
 * sensor/qmi8658 (CTRL1,CTRL2,CTRL7,WHO_AM_I,AX_L). No gyro/IRQ/tap/wake,
 * PMIC or reset ownership. Output is signed mg in the sensor's native axes. */
#include "DriverSupport.h"
#include "PortableMotionTap.h"
static const risc_i2c_bus_api_v1*bus;
static const risc_platform_clock_api_v1*clock_api;
static const tw_hw_i2c_device_v1*config;
static uint64_t token;
static bool running,closing,dirty;
static uint8_t original[3];
static bool release_session(void){if(token){if(!bus||!bus->release_device(bus->context,token)){closing=true;return false;}token=0;}return true;}
static bool take(void){if(!bus||!config||token||closing)return false;bool ok=bus->claim_device(bus->context,config->address,&token);if(!ok||!token){if(token)closing=true;return false;}return true;}
static bool finish(bool ok){return release_session()&&ok;}
static bool read_reg(uint8_t reg,uint8_t*out,size_t n){return bus->transact(bus->context,token,&reg,1,out,n,20);}
static bool update(uint8_t reg,uint8_t mask,uint8_t bits){
 uint8_t before=0,after=0;if(!read_reg(reg,&before,1)||(reg==2&&(before&0x80)))return false;
 uint8_t wanted=(before&~mask)|(bits&mask),tx[2]={reg,wanted};
 if(before!=wanted&&!bus->transact(bus->context,token,tx,2,NULL,0,20))return false;
 return read_reg(reg,&after,1)&&(after&mask)==(wanted&mask);
}
static bool quiesce(void){
 running=false;
 if(!release_session())return false;closing=false;
 if(dirty){
  if(!take())return false;
  bool ok=update(8,1,0)&&update(3,0x7f,original[1])&&update(2,0x60,original[0])&&update(8,1,original[2]);
  if(!finish(ok))return false;dirty=false;
 }
 bus=NULL;clock_api=NULL;config=NULL;return true;
}
static void stop(void){(void)quiesce();}
static bool start(const risc_provider_dependency_v1*d,size_t n){
 if(running||closing||dirty||token)return false;
 config=tw_config(d,n,"qst,qmi8658","peripheral.i2c",sizeof(*config));
 if(!tw_i2c_config_valid(config)||(config->address!=0x6a&&config->address!=0x6b)||config->chip_id!=5||config->irq!=-1)return false;
 bus=tw_dep(d,n,"i2c.bus",sizeof(*bus));clock_api=tw_dep(d,n,"platform.clock",sizeof(*clock_api));
 if(!tw_i2c_valid(bus)||!tw_clock_valid(clock_api)||!take())return false;
 uint8_t id=0;bool ok=read_reg(0,&id,1)&&id==5&&read_reg(2,&original[0],1)&&read_reg(3,&original[1],1)&&read_reg(8,&original[2],1);
 /* CTRL1.SensorDisable is outside our owned mask. Reject inherited power-down
  * rather than advertise samples with the oscillator disabled or change policy. */
 if(!ok||(original[0]&0x81)||(original[1]&0x80))return finish(false);
 dirty=true; // Any attempted write may have reached the device, even on false.
 ok=update(8,1,0)&&update(2,0x60,0x60)&&update(3,0x7f,0x16)&&update(8,1,1);
 if(!finish(ok))return false;
 uint64_t begin=clock_api->monotonic_ms(clock_api->context);
 for(unsigned i=0;i<32&&clock_api->monotonic_ms(clock_api->context)-begin<31;i++)clock_api->sleep_ms(clock_api->context,31);
 if(clock_api->monotonic_ms(clock_api->context)-begin<31)return false;
 running=true;return true;
}
static bool read_sample(void*c,twatch_accel_sample_v1*out){
 (void)c;if(!running||closing||!out||!take())return false;
 uint8_t ctrl=0,enabled=0,status=0,raw[6];bool ok=read_reg(3,&ctrl,1)&&(ctrl&0x7f)==0x16&&read_reg(8,&enabled,1)&&(enabled&1)&&read_reg(0x2e,&status,1)&&(status&1)&&read_reg(0x35,raw,6);
 twatch_accel_sample_v1 result={0};
 if(ok){result.x=(int32_t)(int16_t)((uint16_t)raw[1]<<8|raw[0])*1000/8192;result.y=(int32_t)(int16_t)((uint16_t)raw[3]<<8|raw[2])*1000/8192;result.z=(int32_t)(int16_t)((uint16_t)raw[5]<<8|raw[4])*1000/8192;}
 if(!finish(ok))return false;*out=result;return true;
}
static bool chip_id(void*c,uint8_t*out){(void)c;if(!running||closing||!out)return false;*out=5;return true;}
static const twatch_motion_api_v1 api={.api_version=1,.struct_size=TWATCH_MOTION_SAMPLE_SIZE,.read=read_sample,.chip_id=chip_id};
static const risc_driver_v2 driver={2,sizeof(driver),"qmi8658-accel",TWATCH_MOTION_CAPABILITY,1,&api,start,stop,quiesce};
__attribute__((visibility("default")))const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}
