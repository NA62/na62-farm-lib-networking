#include <pcap/pcap.h>
#include <string>

namespace na62 {

class PcapDumper {

private:
  static const int n_file_ = 10;
  static pcap_t* handle_[n_file_];
  static pcap_dumper_t* dumper_[n_file_];

public:
  PcapDumper();
  virtual ~PcapDumper();

  //set the path and the base of filename
  static inline void startDump(std::string pathbasefilename) {
  std::string name;
    for (int i= 0; i < n_file_; ++i ) {
      name = pathbasefilename;
      name.append("-");
      name.append(std::to_string(i));
      name.append(".pcap");

      handle_[i] = pcap_open_dead(DLT_EN10MB, 1 << 16);
      dumper_[i] = pcap_dump_open(handle_[i], name.c_str());
    }
  }

  static inline void stopDump() {
    for (int i= 0; i < n_file_; ++i ) {
      pcap_dump_close(dumper_[i]);
    }
  }
  static void dumpPacket(int file_n, char* packet, uint packet_leght);
};

}
