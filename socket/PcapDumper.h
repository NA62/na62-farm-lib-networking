#include <pcap.h>
extern "C" {
  pcap_t* pcap_open_dead(int, int);
  pcap_dumper_t* pcap_dump_open(pcap_t*, const char*);
}
//extern "C" {
//  #include <pcap.h>
//  pcap_t* pcap_open_dead(int, int);
//  pcap_dumper_t* pcap_dump_open(pcap_t*, const char*);
  //pcap_t* pcap_open_dead(int, int){}
  //pcap_dumper_t* pcap_dump_open(pcap_t*, const char*){}

//}

//extern "C" pcap_t* pcap_open_dead(int, int);
//extern "C" pcap_dumper_t* pcap_dump_open(pcap_t*, const char*);

namespace na62 {



class PcapDumper {

private:
  static pcap_t *handle_;
  static pcap_dumper_t *dumper_;
public:


  PcapDumper();
  virtual ~PcapDumper();

  //set the name of the file
  static inline void startDump() {
      static pcap_t *handle_ = pcap_open_dead(DLT_EN10MB, 1 << 16);
      static pcap_dumper_t *dumper_ = pcap_dump_open(handle_, "cap.pcap");
  }

  static inline void stopDump() {
      pcap_dump_close(dumper_);
  }
  static void dumpPacket(char* packet, uint packet_leght);
};

}
