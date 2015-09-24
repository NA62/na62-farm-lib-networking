#include "PcapDumper.h"
#include <time.h>

namespace na62 {
    pcap_t* PcapDumper::handle_;
    pcap_dumper_t* PcapDumper::dumper_;

    void PcapDumper::dumpPacket(char* packet, uint packet_leght) {

      pcap_pkthdr pcap_hdr;    
      pcap_hdr.caplen = pcap_hdr.len = packet_leght;
      gettimeofday(&pcap_hdr.ts, NULL);

      pcap_dump((u_char *)dumper_, &pcap_hdr, (u_char *) packet);
    }

    PcapDumper::PcapDumper() {
    }
    PcapDumper::~PcapDumper() {
    }
}
