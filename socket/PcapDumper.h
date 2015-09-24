#include <pcap/pcap.h>

namespace na62 {

class PcapDumper {

private:
  static pcap_t* handle_;
  static pcap_dumper_t* dumper_;
public:
  PcapDumper();
  virtual ~PcapDumper();

  //set the name of the file
  static inline void startDump() {      
      handle_ = pcap_open_dead(DLT_EN10MB, 1 << 16);
      dumper_ = pcap_dump_open(handle_, "cap.pcap");
  }

  static inline void stopDump() {
      pcap_dump_close(dumper_);
  }
  static void dumpPacket(char* packet, uint packet_leght);
};

}
