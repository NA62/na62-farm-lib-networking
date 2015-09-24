//#include <libpcap/pcap.h>
//g++ -o pcap pcap.cpp PcapDumper.cpp -I/usr/include/ -lpcap -lrt

#include "PcapDumper.h"
#include <time.h>

//#include <iostream>

namespace na62 {
    pcap_t* PcapDumper::handle_;
    pcap_dumper_t* PcapDumper::dumper_;

    void PcapDumper::dumpPacket(char* packet, uint packet_leght) {
      //std::cout<<"Pacchetto: "<<&packet<<"lenght"<<packet_leght<<std::endl;
      pcap_pkthdr pcap_hdr;
      //pcap_hdr.caplen = sizeof(pkt1);
      //pcap_hdr.len = pcap_hdr.caplen;
      pcap_hdr.caplen = pcap_hdr.len = packet_leght;
      gettimeofday(&pcap_hdr.ts, NULL);

      pcap_dump((u_char *)dumper_, &pcap_hdr, (u_char *) &packet);
    }

    PcapDumper::PcapDumper() {
    }
    PcapDumper::~PcapDumper() {
    }
}
