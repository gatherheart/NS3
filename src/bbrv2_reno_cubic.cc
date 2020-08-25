/** Network topology
 *
 *    40Mb/s, 1ms                           40Mb/s, 1ms
 * n0--------------|                    |---------------n4
 * BBRv2   L0      |   10Mbps/s, 18ms   |      L2
 *                 n2------------------n3
 *    40Mb/s, 1ms  |         L1         |    40Mb/s, 1ms
 * n1--------------|                    |---------------n5
 * CUBIC   L3      |			|      L4
 *                 |                    |    
 *    40Mb/s  1ms  |  			|    40Mbs, 1ms
 * n6--------------|			|---------------n7
 * Reno    L5					L6
 * 
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/dqc-module.h"
#include "ns3/log.h"
#include<stdio.h>
#include<iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <memory>
#include <chrono>
using namespace ns3;
using namespace dqc;
using namespace std;
NS_LOG_COMPONENT_DEFINE ("bbr-rtt");

uint32_t checkTimes;
double avgQueueSize;

// The times
double global_start_time;
double global_stop_time;
double sink_start_time;
double sink_stop_time;
double client_start_time;
double client_stop_time;

NodeContainer n0n2;
NodeContainer n2n3;
NodeContainer n3n4;
NodeContainer n1n2;
NodeContainer n3n5;

// Added Nodes
NodeContainer n6n2;
NodeContainer n3n7;

Ipv4InterfaceContainer i0i2;
Ipv4InterfaceContainer i1i2;
Ipv4InterfaceContainer i2i3;
Ipv4InterfaceContainer i3i4;
Ipv4InterfaceContainer i3i5;

// Added Ipv4
Ipv4InterfaceContainer i6i2;
Ipv4InterfaceContainer i3i7;

typedef struct
{
uint64_t bps;
uint32_t msDelay;
uint32_t msQdelay;	
}link_config_t;
//unrelated topology
/*
   L3      L1      L4
configuration same as the above dumbbell topology
n0--L0--n2--L1--n3--L2--n4
n1--L3--n2--L1--n3--L4--n5
*/
link_config_t p4p[]={
[0]={40*1000000,1,150},
[1]={10*1000000,18,150},
[2]={40*1000000,1,150},
[3]={40*1000000,1,150},
[4]={40*1000000,1,150},
[5]={40*1000000,1,150},
[6]={40*1000000,1,150},
};
const uint32_t TOPO_DEFAULT_BW     = 10 * 1000 * 1000;    // in bps: 10Mbps
const uint32_t TOPO_DEFAULT_PDELAY =      18;    // in ms:   100ms
const uint32_t TOPO_DEFAULT_QDELAY =     150;    // in ms:  300ms
static void InstallDqc( dqc::CongestionControlType cc_type,
                        Ptr<Node> sender,Ptr<Node> receiver,
                        uint16_t send_port,uint16_t recv_port,
                        float startTime,float stopTime,
                        DqcTrace *trace, DqcTraceState *stat,
                        uint32_t max_bps=0,uint32_t cid=0,bool ecn=false,uint32_t emucons=1)
{
    Ptr<DqcSender> sendApp = CreateObject<DqcSender> (cc_type,ecn);
    Ptr<DqcReceiver> recvApp = CreateObject<DqcReceiver>();
    sender->AddApplication (sendApp);
    receiver->AddApplication (recvApp);
    sendApp->SetNumEmulatedConnections(emucons);
    Ptr<Ipv4> ipv4 = receiver->GetObject<Ipv4> ();
    Ipv4Address receiverIp = ipv4->GetAddress (1, 0).GetLocal ();
    recvApp->Bind(recv_port);
    sendApp->Bind(send_port);
    sendApp->ConfigurePeer(receiverIp,recv_port);
    sendApp->SetStartTime (Seconds (startTime));
    sendApp->SetStopTime (Seconds (stopTime));
    recvApp->SetStartTime (Seconds (startTime));
    recvApp->SetStopTime (Seconds (stopTime));

    if(max_bps>0){
        sendApp->SetMaxBandwidth(max_bps);
    }
    if(cid){
       sendApp->SetSenderId(cid);
        sendApp->SetCongestionId(cid);
    }
    if(trace){
	sendApp->SetBwTraceFuc(MakeCallback(&DqcTrace::OnBw,trace));
	sendApp->SetTraceOwdAtSender(MakeCallback(&DqcTrace::OnRtt,trace));

        recvApp->SetOwdTraceFuc(MakeCallback(&DqcTrace::OnOwd,trace));
        recvApp->SetGoodputTraceFuc(MakeCallback(&DqcTrace::OnGoodput,trace));
        recvApp->SetStatsTraceFuc(MakeCallback(&DqcTrace::OnStats,trace));
        trace->SetStatsTraceFuc(MakeCallback(&DqcTraceState::OnStats,stat));
    }
}
void ns3_rtt(int ins,std::string algo,DqcTraceState *stat,int sim_time=60,int loss_integer=0){
    std::string instance=std::to_string(ins);
    uint64_t linkBw   = TOPO_DEFAULT_BW;
    uint32_t msDelay  = TOPO_DEFAULT_PDELAY;
    uint16_t sendPort=1000;
    uint16_t recvPort=5000;

    double sim_dur=sim_time;
    int start_time=0;
    int end_time=sim_time;
    float appStart=start_time;
    float appStop=end_time;
    p4p[1].bps=linkBw;
    p4p[1].msDelay=msDelay;
    uint32_t owd1=p4p[0].msDelay+p4p[1].msDelay+p4p[2].msDelay;
    uint32_t owd2=p4p[3].msDelay+p4p[1].msDelay+p4p[4].msDelay;
    uint32_t owd3=p4p[5].msDelay+p4p[1].msDelay+p4p[6].msDelay;

    uint32_t owd=std::max(owd1,owd2);
    uint32_t msQdelay=owd*10;
    for(size_t i=0;i<sizeof(p4p)/sizeof(p4p[0]);i++){
        p4p[i].msQdelay=msQdelay;
    }
    NodeContainer c;
    c.Create (8);
    n0n2 = NodeContainer (c.Get (0), c.Get (2));
    n1n2 = NodeContainer (c.Get (1), c.Get (2));
    n2n3 = NodeContainer (c.Get (2), c.Get (3));
    n3n4 = NodeContainer (c.Get (3), c.Get (4));
    n3n5 = NodeContainer (c.Get (3), c.Get (5));

    //Added NodeContainer
    n6n2 = NodeContainer (c.Get (6), c.Get (2));
    n3n7 = NodeContainer (c.Get (3), c.Get (7));

    uint32_t meanPktSize = 1500;
    link_config_t *config=p4p;
    uint32_t bufSize=0;	
    
    InternetStackHelper internet;
    internet.Install (c);
    
    NS_LOG_INFO ("Create channels");
    PointToPointHelper p2p;
    TrafficControlHelper tch;

    //L0
    bufSize =config[0].bps * config[0].msQdelay/8000;
    p2p.SetQueue ("ns3::DropTailQueue",
                "Mode", StringValue ("QUEUE_MODE_BYTES"),
                "MaxBytes", UintegerValue (bufSize));
    p2p.SetDeviceAttribute ("DataRate", DataRateValue(DataRate (config[0].bps)));
    p2p.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (config[0].msDelay)));
    NetDeviceContainer devn0n2 = p2p.Install (n0n2);
    //L3
    bufSize =config[3].bps * config[3].msQdelay/8000;
    p2p.SetQueue ("ns3::DropTailQueue",
                "Mode", StringValue ("QUEUE_MODE_BYTES"),
                "MaxBytes", UintegerValue (bufSize));
    p2p.SetDeviceAttribute ("DataRate", DataRateValue(DataRate (config[3].bps)));
    p2p.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (config[3].msDelay)));
    NetDeviceContainer devn1n2 = p2p.Install (n1n2);
    //L1
    bufSize =config[1].bps * config[1].msQdelay/8000;
    p2p.SetQueue ("ns3::DropTailQueue",
                "Mode", StringValue ("QUEUE_MODE_BYTES"),
                "MaxBytes", UintegerValue (bufSize)); 
    p2p.SetDeviceAttribute ("DataRate", DataRateValue(DataRate (config[1].bps)));
    p2p.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (config[1].msDelay)));
    NetDeviceContainer devn2n3 = p2p.Install (n2n3);
    //L2
    bufSize =config[2].bps * config[2].msQdelay/8000;
    p2p.SetQueue ("ns3::DropTailQueue",
                "Mode", StringValue ("QUEUE_MODE_BYTES"),
                "MaxBytes", UintegerValue (bufSize));
    p2p.SetDeviceAttribute ("DataRate", DataRateValue(DataRate (config[2].bps)));
    p2p.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (config[2].msDelay)));
    NetDeviceContainer devn3n4 = p2p.Install (n3n4);
    //L4
    bufSize =config[4].bps * config[4].msQdelay/8000;
    p2p.SetQueue ("ns3::DropTailQueue",
                "Mode", StringValue ("QUEUE_MODE_BYTES"),
                "MaxBytes", UintegerValue (bufSize));
    p2p.SetDeviceAttribute ("DataRate", DataRateValue(DataRate (config[4].bps)));
    p2p.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (config[4].msDelay)));
    NetDeviceContainer devn3n5 = p2p.Install (n3n5);
    

    // Added Lines  
    //L5
    bufSize =config[5].bps * config[5].msQdelay/8000;
    p2p.SetQueue ("ns3::DropTailQueue",
                "Mode", StringValue ("QUEUE_MODE_BYTES"),
                "MaxBytes", UintegerValue (bufSize));
    p2p.SetDeviceAttribute ("DataRate", DataRateValue(DataRate (config[5].bps)));
    p2p.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (config[5].msDelay)));
    NetDeviceContainer devn6n2 = p2p.Install (n6n2);
    //L6
    bufSize =config[6].bps * config[6].msQdelay/8000;
    p2p.SetQueue ("ns3::DropTailQueue",
                "Mode", StringValue ("QUEUE_MODE_BYTES"),
                "MaxBytes", UintegerValue (bufSize));
    p2p.SetDeviceAttribute ("DataRate", DataRateValue(DataRate (config[6].bps)));
    p2p.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (config[6].msDelay)));
    NetDeviceContainer devn3n7 = p2p.Install (n3n7);
 
    Ipv4AddressHelper ipv4;
    
    ipv4.SetBase ("10.1.1.0", "255.255.255.0");
    i0i2 = ipv4.Assign (devn0n2);
    tch.Uninstall (devn0n2);
    
    ipv4.SetBase ("10.1.2.0", "255.255.255.0");
    i1i2 = ipv4.Assign (devn1n2);
    tch.Uninstall (devn1n2);
    
    ipv4.SetBase ("10.1.3.0", "255.255.255.0");
    i2i3 = ipv4.Assign (devn2n3);
    tch.Uninstall (devn2n3);
    
    ipv4.SetBase ("10.1.4.0", "255.255.255.0");
    i3i4 = ipv4.Assign (devn3n4);
    tch.Uninstall (devn3n4);
    
    ipv4.SetBase ("10.1.5.0", "255.255.255.0");
    i3i5 = ipv4.Assign (devn3n5);
    tch.Uninstall (devn3n5);

    // Added Ipv4
    ipv4.SetBase ("10.1.6.0", "255.255.255.0");
    i6i2 = ipv4.Assign (devn6n2);
    tch.Uninstall (devn6n2);
    
    ipv4.SetBase ("10.1.7.0", "255.255.255.0");
    i3i7 = ipv4.Assign (devn3n7);
    tch.Uninstall (devn3n7);


    // Set up the routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    dqc::CongestionControlType cc1=kBBRv2;

    // Added cc
    dqc::CongestionControlType cc2=kBBRv2;
    dqc::CongestionControlType cc3=kBBRv2;

    //no use
    CongestionControlManager cong_ops_manager;
    RegisterCCManager(&cong_ops_manager);
    
    uint32_t max_bps=0;
    int test_pair=1;
    uint32_t sender_id=1;

    std::vector<std::unique_ptr<DqcTrace>> traces;
    std::string log;
    std::string delimiter="_";
    std::string prefix=instance+delimiter+algo+delimiter+"rtt"+delimiter;

    log=prefix+std::to_string(test_pair);
    DqcTrace trace1;
    trace1.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_RTT|DqcTraceEnable::E_DQC_BW);
    InstallDqc(cc1,c.Get(0),c.Get(4),sendPort,recvPort,appStart+0.01,appStop,&trace1,stat,max_bps,sender_id);
    test_pair++;
    sender_id++;
    sendPort++;
    recvPort++;
    
    log=prefix+std::to_string(test_pair);
    DqcTrace trace2;
    trace2.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_RTT|DqcTraceEnable::E_DQC_BW);
    InstallDqc(cc2,c.Get(1),c.Get(5),sendPort,recvPort,appStart+2.5,appStop,&trace2,stat,max_bps,sender_id);
    test_pair++;
    sender_id++;
    sendPort++;
    recvPort++;

    log=prefix+std::to_string(test_pair);
    DqcTrace trace3;
    trace3.Log(log,DqcTraceEnable::E_DQC_OWD|DqcTraceEnable::E_DQC_RTT|DqcTraceEnable::E_DQC_BW);
    InstallDqc(cc3,c.Get(6),c.Get(7),sendPort,recvPort,appStart+2.5,appStop,&trace3,stat,max_bps,sender_id);
    test_pair++;
    sender_id++;
    sendPort++;
    recvPort++;

    Simulator::Stop (Seconds(sim_dur));
    Simulator::Run ();
    Simulator::Destroy();  
    stat->Flush(linkBw,sim_dur);    
}
int main (int argc, char *argv[]){
    int sim_time=60;
    int ins[]={1};
    char *algos[]={"bbr"};
    for(int c=0;c<(int)sizeof(algos)/sizeof(algos[0]);c++){
        std::string cong=std::string(algos[c]);
        std::string name=cong;
        std::unique_ptr<DqcTraceState> stat;
        stat.reset(new DqcTraceState(name));
        auto inner_start = std::chrono::high_resolution_clock::now();
        for(int i=0;i<sizeof(ins)/sizeof(ins[0]);i++){
            ns3_rtt(ins[i],cong,stat.get(),sim_time);
        }
        auto inner_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> tm = inner_end - inner_start;
        std::chrono::duration<double, std::ratio<60>> minutes =inner_end- inner_start;
        stat->RecordRuningTime(tm.count(),minutes.count());     
    }
    return 0;
}
