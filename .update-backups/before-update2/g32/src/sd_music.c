/* TFT35 GD32F205 microSD -> ESP32 compressed MP3 transport.
 * FAT16/FAT32, root folder, 8.3 names; SD SPI0 PA4-7.
 * UART commands from ESP32: A:OPEN A:NEXT A:PREV A:READ A:STOP A:STATUS
 * UART replies: A:FILE:<size>:<filename>, A:DATA:<hex>, A:EOF, A:NOSD
 * Transport streams MP3 bytes (not PCM); ESP32 performs MP3 decoding.
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "gd32f20x.h"
#include "gd32f20x_gpio.h"
#include "gd32f20x_rcu.h"
#include "gd32f20x_spi.h"
#include "hal.h"

#define MAX_TRACKS 64
#define CHUNK 384
#define CS_HIGH() gpio_bit_set(GPIOA,GPIO_PIN_4)
#define CS_LOW() gpio_bit_reset(GPIOA,GPIO_PIN_4)
static uint8_t sector[512];
static uint8_t payload[CHUNK];
static uint8_t sd_ok, sd_block_address;
static uint32_t part_lba, fat_lba, data_lba, root_lba, root_cluster;
static uint32_t cluster_count, sectors_per_fat, root_sectors;
static uint16_t root_entries;
static uint8_t sectors_per_cluster, fat_type;
static struct { uint32_t cluster,size; char name[13]; } tracks[MAX_TRACKS];
static unsigned track_count, selected;
static uint32_t file_cluster, file_pos, file_size;
static uint8_t file_sector_offset;
static uint16_t file_byte_offset;
static uint8_t sector_valid;
static const char hexdigits[]="0123456789ABCDEF";

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)le16(p) | ((uint32_t)le16(p+2)<<16); }
static uint8_t spi_xfer(uint8_t b) {
  while (!(SPI_STAT(SPI0)&SPI_STAT_TBE)) {};
  SPI_DATA(SPI0)=b;
  while (!(SPI_STAT(SPI0)&SPI_STAT_RBNE)) {};
  return (uint8_t)SPI_DATA(SPI0);
}
static void sd_deselect(void) { CS_HIGH(); spi_xfer(255); }
static uint8_t sd_cmd(uint8_t c,uint32_t a,uint8_t crc) {
  sd_deselect(); CS_LOW();
  for (int i=0;i<20000;i++) if(spi_xfer(255)==255) break;
  spi_xfer(0x40|c);spi_xfer(a>>24);spi_xfer(a>>16);spi_xfer(a>>8);spi_xfer(a);spi_xfer(crc);
  for (int i=0;i<20;i++) { uint8_t r=spi_xfer(255); if(!(r&128)) return r; }
  return 255;
}
static int read_sector(uint32_t lba) {
  if(sd_cmd(17,sd_block_address?lba:lba*512u,1)!=0) {sd_deselect();return 0;}
  uint8_t token=255;
  for(unsigned i=0;i<180000;i++) { token=spi_xfer(255); if(token!=255) break; }
  if(token!=0xFE){sd_deselect();return 0;}
  for(unsigned i=0;i<512;i++)sector[i]=spi_xfer(255);
  spi_xfer(255);spi_xfer(255);sd_deselect();return 1;
}
static uint32_t cluster_lba(uint32_t cl) {return data_lba+(cl-2u)*sectors_per_cluster;}
static uint32_t next_cluster(uint32_t cl) {
  uint32_t byte_offset=cl*(fat_type==32?4u:2u);
  if(!read_sector(fat_lba+byte_offset/512u))return 0;
  uint32_t offset=byte_offset%512u;
  uint32_t n=fat_type==32?(le32(sector+offset)&0x0FFFFFFFu):le16(sector+offset);
  return (fat_type==32?(n>=0x0FFFFFF8u):(n>=0xFFF8u))?0:n;
}
static void spi_speed(int fast) {
  SPI_CTL0(SPI0)&=~SPI_CTL0_SPIEN;
  SPI_CTL0(SPI0)=(SPI_CTL0(SPI0)&~SPI_CTL0_PSC)|((fast?3u:7u)<<3); /* /16 after init */
  SPI_CTL0(SPI0)|=SPI_CTL0_SPIEN;
}
static int sd_init(void) {
  rcu_periph_clock_enable(RCU_GPIOA);rcu_periph_clock_enable(RCU_SPI0);
  gpio_init(GPIOA,GPIO_MODE_OUT_PP,GPIO_OSPEED_50MHZ,GPIO_PIN_4);CS_HIGH();
  gpio_init(GPIOA,GPIO_MODE_AF_PP,GPIO_OSPEED_50MHZ,GPIO_PIN_5|GPIO_PIN_7);
  gpio_init(GPIOA,GPIO_MODE_IN_FLOATING,GPIO_OSPEED_50MHZ,GPIO_PIN_6);
  SPI_CTL0(SPI0)=0;SPI_CTL1(SPI0)=0;
  SPI_CTL0(SPI0)=SPI_CTL0_MSTMOD|SPI_CTL0_SWNSS|SPI_CTL0_SWNSSEN|(7u<<3);
  SPI_CTL0(SPI0)|=SPI_CTL0_SPIEN;
  for(int i=0;i<12;i++)spi_xfer(255);
  if(sd_cmd(0,0,0x95)!=1){sd_deselect();return 0;}sd_deselect();
  uint8_t r=sd_cmd(8,0x1AA,0x87),v2=(r==1);
  if(v2){uint8_t a=spi_xfer(255),b=spi_xfer(255),c=spi_xfer(255),d=spi_xfer(255);(void)a;(void)b;if(c!=1||d!=0xAA){sd_deselect();return 0;}}
  else if(r!=5){sd_deselect();return 0;}
  sd_deselect();int ready=0;
  for(int i=0;i<10000;i++){sd_cmd(55,0,1);sd_deselect();r=sd_cmd(41,v2?0x40000000u:0,1);sd_deselect();if(r==0){ready=1;break;}}
  if(!ready)return 0;
  sd_block_address=0;
  if(v2){r=sd_cmd(58,0,1);if(r==0){uint8_t ocr=spi_xfer(255);sd_block_address=(ocr&0x40)!=0;for(int i=0;i<3;i++)spi_xfer(255);}sd_deselect();}
  if(!sd_block_address){r=sd_cmd(16,512,1);sd_deselect();if(r!=0)return 0;}
  spi_speed(1);
  return 1;
}
static int fat_mount(void) {
  if(!read_sector(0))return 0;
  part_lba=0;
  if(sector[510]!=0x55||sector[511]!=0xAA)return 0;
  if(memcmp(sector+82,"FAT32",5)!=0&&memcmp(sector+54,"FAT16",5)!=0&&memcmp(sector+54,"FAT12",5)!=0){
    part_lba=le32(sector+454);if(!part_lba||!read_sector(part_lba))return 0;
  }
  uint16_t bps=le16(sector+11),reserved=le16(sector+14);
  sectors_per_cluster=sector[13];uint8_t fats=sector[16];root_entries=le16(sector+17);
  uint32_t total=le16(sector+19)?le16(sector+19):le32(sector+32);
  sectors_per_fat=le16(sector+22)?le16(sector+22):le32(sector+36);
  if(bps!=512||!sectors_per_cluster||(sectors_per_cluster&(sectors_per_cluster-1))||!reserved||!fats||!sectors_per_fat)return 0;
  root_sectors=((uint32_t)root_entries*32u+511u)/512u;
  fat_lba=part_lba+reserved;root_lba=fat_lba+fats*sectors_per_fat;
  data_lba=root_lba+root_sectors;
  if(total<=reserved+fats*sectors_per_fat+root_sectors)return 0;
  cluster_count=(total-reserved-fats*sectors_per_fat-root_sectors)/sectors_per_cluster;
  fat_type=cluster_count>=65525?32:(cluster_count>=4085?16:12);
  if(fat_type==12)return 0;
  root_cluster=fat_type==32?le32(sector+44):0;
  return fat_type!=32||(root_cluster>=2&&root_cluster<cluster_count+2);
}
static void name83(char dst[13],const uint8_t *p){
  unsigned j=0;
  for(int i=0;i<8&&p[i]!=' ';i++)dst[j++]=(p[i]==5)?(char)0xE5:(char)p[i];
  if(p[8]!=' '){dst[j++]='.';for(int i=8;i<11&&p[i]!=' ';i++)dst[j++]=(char)p[i];}
  dst[j]=0;
}
static int is_mp3(const uint8_t *p){return p[8]=='M'&&p[9]=='P'&&p[10]=='3';}
static void scan_sector(void){
  for(int i=0;i<512;i+=32){uint8_t *d=sector+i;
    if(d[0]==0)return;
    if(d[0]==0xE5||d[11]==0x0F||(d[11]&0x18)||!is_mp3(d))continue;
    if(track_count>=MAX_TRACKS)return;
    unsigned idx=track_count++;
    tracks[idx].cluster=((uint32_t)le16(d+20)<<16)|le16(d+26);
    tracks[idx].size=le32(d+28);name83(tracks[idx].name,d);
    if(tracks[idx].cluster<2||tracks[idx].size==0)track_count--;
  }
}
static void scan_root(void){
  track_count=0;
  if(fat_type==16){for(uint32_t i=0;i<root_sectors&&track_count<MAX_TRACKS;i++){if(!read_sector(root_lba+i))break;scan_sector();}}
  else {uint32_t cl=root_cluster;unsigned hops=0;while(cl>=2&&hops++<512&&track_count<MAX_TRACKS){
    for(unsigned s=0;s<sectors_per_cluster&&track_count<MAX_TRACKS;s++){if(read_sector(cluster_lba(cl)+s))scan_sector();}
    cl=next_cluster(cl);
  }}
  for(unsigned i=1;i<track_count;i++){typeof(tracks[0]) tmp=tracks[i];int j=(int)i-1;
    while(j>=0&&strcmp(tracks[j].name,tmp.name)>0){tracks[j+1]=tracks[j];j--;}
    tracks[j+1]=tmp;
  }
}
static void open_track(unsigned idx) {
  if(!sd_ok||!track_count){hal_uart_puts("A:NOSD");return;}
  selected=idx%track_count;file_cluster=tracks[selected].cluster;file_size=tracks[selected].size;
  file_pos=0;file_sector_offset=0;file_byte_offset=0;sector_valid=0;
  char msg[80];snprintf(msg,sizeof(msg),"A:FILE:%lu:%s",(unsigned long)file_size,tracks[selected].name);
  hal_uart_puts(msg);
}
static unsigned read_file(uint8_t *dst,unsigned max){
  unsigned n=0;
  while(n<max&&file_pos<file_size&&file_cluster>=2){
    if(!sector_valid){if(!read_sector(cluster_lba(file_cluster)+file_sector_offset))break;sector_valid=1;}
    dst[n++]=sector[file_byte_offset++];file_pos++;
    if(file_byte_offset==512){file_byte_offset=0;sector_valid=0;file_sector_offset++;
      if(file_sector_offset==sectors_per_cluster){file_sector_offset=0;file_cluster=next_cluster(file_cluster);}}
  }
  return n;
}
static void send_data(void){
  if(!sd_ok||!track_count){hal_uart_puts("A:NOSD");return;}
  if(file_pos>=file_size){hal_uart_puts("A:EOF");return;}
  unsigned n=read_file(payload,CHUNK);
  if(!n){hal_uart_puts("A:EOF");return;}
  /* One line, no intermediate heap. */
  /* Receiver expects prefix A:DATA: */
  static char frame[7+CHUNK*2+1];memcpy(frame,"A:DATA:",7);
  for(unsigned i=0;i<n;i++){frame[7+2*i]=hexdigits[payload[i]>>4];frame[8+2*i]=hexdigits[payload[i]&15];}
  frame[7+2*n]=0;
  hal_uart_puts(frame);
}
void sd_music_init(void){
  sd_ok=sd_init()&&fat_mount();if(sd_ok)scan_root();
}
int sd_music_command(const char *s){
  if(strncmp(s,"A:",2))return 0;
  if(!strcmp(s,"A:OPEN"))open_track(selected);
  else if(!strcmp(s,"A:NEXT"))open_track(selected+1u);
  else if(!strcmp(s,"A:PREV"))open_track(selected+(track_count?track_count-1u:0));
  else if(!strcmp(s,"A:READ"))send_data();
  else if(!strcmp(s,"A:STATUS")){if(!sd_ok||!track_count)hal_uart_puts("A:NOSD");else open_track(selected);}
  return 1;
}
