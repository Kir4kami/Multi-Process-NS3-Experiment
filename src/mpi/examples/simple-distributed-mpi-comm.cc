/*
 *  Copyright 2018. Lawrence Livermore National Security, LLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Steven Smith <smith84@llnl.gov>
 */

/**
 * \file
 * \ingroup mpi
 *
 * This test is equivalent to simple-distributed with the addition of
 * initialization of MPI by user code (this script) and providing
 * a communicator to ns-3.  The ns-3 communicator is smaller than
 * MPI Comm World as might be the case if ns-3 is run in parallel
 * with another simulator.
 *
 * TestDistributed creates a dumbbell topology and logically splits it in
 * half.  The left half is placed on logical processor 0 and the right half
 * is placed on logical processor 1.
 * OnOff clients are placed on each left leaf node. Each right leaf node
 * is a packet sink for a left leaf node.  As a packet travels from one
 * logical processor to another (the link between n4 and n5), MPI messages
 * are passed containing the serialized packet. The message is then
 * deserialized into a new packet and sent on as normal.
 *
 * One packet is sent from each left leaf node.  The packet sinks on the
 * right leaf nodes output logging information when they receive the packet.
 */

#include "mpi-test-fixtures.h"

#include "ns3/core-module.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/mpi-interface.h"
#include "ns3/network-module.h"
#include "ns3/nix-vector-helper.h"
#include "ns3/on-off-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/packet-sink.h"
#include "ns3/point-to-point-helper.h"

#include <mpi.h>

#include <chrono>
#include <vector>
#include <string>
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SimpleDistributedMpiComm");


const int NS_COLOR = 1;
const int NOT_NS_COLOR = NS_COLOR + 1;
void
ReportRank(int color, MPI_Comm splitComm)
{
    int otherId = 0;
    int otherSize = 1;

    MPI_Comm_rank(splitComm, &otherId);
    MPI_Comm_size(splitComm, &otherSize);

    if (color == NS_COLOR)
        RANK0COUT("ns-3 rank:  ");
    else
        RANK0COUT("Other rank: ");

    RANK0COUTAPPEND("in MPI_COMM_WORLD: " << SinkTracer::GetWorldRank() << ":"
                                          << SinkTracer::GetWorldSize() << ", in splitComm: "
                                          << otherId << ":" << otherSize << std::endl);

} // ReportRank()

const uint16_t topo[7][3]={ {2,4,8},  //32
                            {4,8,8},  //64
                            {4,16,8}, //128
                            {4,16,16},//256
                            {4,32,16},//512
                            {4,32,32},//1024
                            {4,64,32},//2048
};
uint16_t SPINE=2;
uint16_t LEAF=4;
uint16_t SERVER=8;
uint16_t DST=2; //进程数
std::vector<NodeContainer> serverNodes;
std::vector<Ipv4InterfaceContainer> serverInterfaces;
uint32_t packets=0;
struct FlowInfo{//流量信息结构体
    char type[32];
    uint32_t srcNodeId;
    uint16_t srcPort;
    uint32_t dstNodeId; 
    uint16_t dstPort;
    uint8_t priority;
    uint64_t msgLen;
};
std::vector<std::vector<FlowInfo>> flowInfos;
u_int16_t BatchCur=0;
u_int32_t flowCom=0;
void flowinput_cb(const ns3::Ptr<const ns3::Packet> packet,
                    const ns3::Address& srcAddress,
                    const ns3::Address& destAddress);

void LoadWait();

void CreateFlow(const FlowInfo& flow, double startTime){//跨进程流量创建(单条)
    ApplicationContainer apps;
    // 获取系统进程ID
    uint32_t systemId = MpiInterface::GetSystemId();
    // 源节点和目标节点所在的进程
    uint32_t srcSystemId = flow.srcNodeId / (SERVER*LEAF/DST);
    uint32_t dstSystemId = flow.dstNodeId / (SERVER*LEAF/DST);
    uint16_t srcLeaf = flow.srcNodeId / SERVER;
    uint16_t dstLeaf = flow.dstNodeId / SERVER;
    uint16_t srcServer = flow.srcNodeId % SERVER;
    uint16_t dstServer = flow.dstNodeId % SERVER;
    bool send = false;
    bool recv = false;
    // 发送端配置（仅在源节点所在进程创建）
    if (systemId == srcSystemId) {
        OnOffHelper clientHelper("ns3::UdpSocketFactory", Address());
        clientHelper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        clientHelper.SetAttribute("OffTime",StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        clientHelper.SetAttribute("MaxBytes", UintegerValue(flow.msgLen));
        AddressValue remoteAddress(InetSocketAddress(
            serverInterfaces[dstLeaf].GetAddress(dstServer), flow.dstPort));
        clientHelper.SetAttribute("Remote", remoteAddress);
        apps.Add(clientHelper.Install(serverNodes[srcLeaf].Get(srcServer)));
        send=true;
    }
    // 接收端配置（仅在目标节点所在进程创建）
    if (systemId == dstSystemId) {
        // 创建PacketSink
        PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                             InetSocketAddress(Ipv4Address::GetAny(), flow.dstPort));
        auto apps = sinkHelper.Install(serverNodes[dstLeaf].Get(dstServer));
        auto sink = DynamicCast<PacketSink>(apps.Get(0));
        NS_ASSERT_MSG(sink, "Couldn't get PacketSink application.");
        sink->TraceConnectWithoutContext("RxWithAddresses",
                                            MakeCallback(&SinkTracer::SinkTrace));
        sink->TraceConnectWithoutContext("RxWithAddresses",
                                            MakeCallback(flowinput_cb));
        recv=true;
    }
    apps.Start(Seconds(0));
    apps.Stop(Seconds(100000));
    if(send&&recv)
        std::cout << " from " << flow.srcNodeId << " to " << flow.dstNodeId <<
                " fromportNumber " << 1 <<
                " destportNumder " << 1 <<
                " time " << Simulator::Now().GetSeconds() << " flowsize "<< flow.msgLen << std::endl;
}


void LoadFlow(double startTime = 0){//加载当前的一个phase
    for(FlowInfo flow:flowInfos[BatchCur]){
        CreateFlow(flow,startTime);
        if(MpiInterface::GetSystemId()==(flow.dstNodeId / (SERVER*LEAF/DST)))
            packets+=(flow.msgLen/1448+((flow.msgLen%1448)>0?1:0));
    }
    if(packets==0)
        LoadWait();
}

void LoadWait(){
    uint32_t localDone = 1;
    // MPI全局归约操作检测所有进程完成状态
    uint32_t globalDone = 0;
    MPI_Allreduce(&localDone, &globalDone, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    // 只有所有进程都完成时才进入下一阶段
    if(globalDone == MpiInterface::GetSize()){
        RANK0COUT("All flows completed in phase " << BatchCur << std::endl);
        // 停止当前仿真
        Simulator::Stop();
        // 准备下一阶段
        BatchCur++;
        if(BatchCur < flowInfos.size()){
            // 重置计数器
            flowCom = 0;
            packets = 0;
            // 加载新流量（需同步）
            MPI_Barrier(MPI_COMM_WORLD);
            if(MpiInterface::GetSystemId() == 0)
                RANK0COUT("Loading phase " << BatchCur << std::endl);
            // 各进程并行加载
            LoadFlow();
            // 全局同步确保所有进程加载完成
            MPI_Barrier(MPI_COMM_WORLD);
            // 重启仿真
            Simulator::Run();
        }
        else
            RANK0COUT("All phases completed" << std::endl);
    }
}
void flowinput_cb(const ns3::Ptr<const ns3::Packet> packet,
                    const ns3::Address& srcAddress,
                    const ns3::Address& destAddress){
    flowCom++;
    std::cout<<"rank "<<MpiInterface::GetSystemId() <<" phase "<<BatchCur<<" flow "<< flowCom<<" "<<Simulator::Now().GetSeconds()<<std::endl;
    if(flowCom >= packets)
        LoadWait();
}

void workLoad (){
    std::ifstream flowInput;
    flowInput.open("src/mpi/examples/flow.txt");
    if (!flowInput.is_open())
        std::cout << "unable to open flowInputFile!" << std::endl;
    RANK0COUT("Reading flow info"<< std::endl);
    std::string line;
    double startTime = 0;
    int batch = -1;
    while (std::getline(flowInput, line)) {
        if (line.empty() || line[0] == '#' || line.find("stat")!=std::string::npos) continue;
        std::stringstream ss(line);
        std::string  type_str;
        if (line.find("phase")!=std::string::npos){//phase
            double phase;
            ss >> type_str >> phase;
            if(batch < 0)
                startTime += phase/1e6;
            batch ++;
            flowInfos.emplace_back(std::vector<FlowInfo> {});
            continue;//to be changed
        }
        FlowInfo flow;
        ss >> type_str >> flow.type;
        ss >> type_str >> flow.srcNodeId;
        ss >> type_str >> flow.srcPort;
        ss >> type_str >> flow.dstNodeId;
        ss >> type_str >> flow.dstPort;
        ss >> type_str >> flow.priority;
        ss >> type_str >> flow.msgLen;
        flow.dstPort = batch + 1;
        flowInfos[batch].emplace_back(flow);
    }
    LoadFlow(startTime);
    flowInput.close();
}

int main(int argc, char* argv[]){
    bool nix = true;
    bool nullmsg = false;
    bool tracing = false;
    bool init = false;
    bool verbose = false;
    bool testing = true;
    
    uint8_t topo_select=1;
    
    // Parse command line
    CommandLine cmd(__FILE__);
    cmd.AddValue("nix", "Enable the use of nix-vector or global routing", nix);
    cmd.AddValue("nullmsg",
                 "Enable the use of null-message synchronization (instead of granted time window)",
                 nullmsg);
    cmd.AddValue("tracing", "Enable pcap tracing", tracing);
    cmd.AddValue("init", "ns-3 should initialize MPI by calling MPI_Init", init);
    cmd.AddValue("verbose", "verbose output", verbose);
    cmd.AddValue("test", "Enable regression test output", testing);
    cmd.AddValue("topo", "topo select", topo_select);
    cmd.AddValue("dst", "number of process", DST);
    cmd.Parse(argc, argv);

    SPINE=topo[topo_select][0];
    LEAF=topo[topo_select][1];
    SERVER=topo[topo_select][2];
    // Defer reporting the configuration until we know the communicator

    // Distributed simulation setup; by default use granted time window algorithm.
    if (nullmsg)
        GlobalValue::Bind("SimulatorImplementationType",
                          StringValue("ns3::NullMessageSimulatorImpl"));
    else
        GlobalValue::Bind("SimulatorImplementationType",
                          StringValue("ns3::DistributedSimulatorImpl"));

    // MPI_Init
    if (init)
        // Initialize MPI directly
        MPI_Init(&argc, &argv);
    else
        // Let ns-3 call MPI_Init and MPI_Finalize
        MpiInterface::Enable(&argc, &argv);
    SinkTracer::Init();

    auto worldSize = SinkTracer::GetWorldSize();
    auto worldRank = SinkTracer::GetWorldRank();
    DST=worldSize;
    // if ((!init) && (worldSize != 2))
    // {
    //     RANK0COUT("This simulation requires exactly 2 logical processors if --init is not set."
    //               << std::endl);
    //     return 1;
    // }

    // if (worldSize < 2)
    // {
    //     RANK0COUT("This simulation requires 2  or more logical processors." << std::endl);
    //     return 1;
    // }
    // Flag to record that we created a communicator so we can free it at the end.
    bool freeComm = false;
    // The new communicator, if we create one
    MPI_Comm splitComm = MPI_COMM_WORLD;
    // The list of ranks assigned to ns-3
    std::string ns3Ranks;
    // Tag for whether this rank should go into a new communicator
    int color = MPI_UNDEFINED;

    if (worldSize == DST){
        std::stringstream ss;
        color = NS_COLOR;
        ss << "MPI_COMM_WORLD (" << worldSize << " ranks)";
        ns3Ranks = ss.str();
        splitComm = MPI_COMM_WORLD;
        freeComm = false;
    }
    else{
        //  worldSize > 2    communicator of ranks 1-2

        // Put ranks 1-2 in the new communicator
        if (worldRank < DST )
            color = NS_COLOR;
        else
            color = NOT_NS_COLOR;
        std::stringstream ss;
        ss << "Split [1-2] (out of " << worldSize << " ranks) from MPI_COMM_WORLD";
        ns3Ranks = ss.str();
        // Now create the new communicator
        MPI_Comm_split(MPI_COMM_WORLD, color, worldRank, &splitComm);
        freeComm = true;
    }

    if (init)
        MpiInterface::Enable(splitComm);

    // Report the configuration from rank 0 only
    RANK0COUT(cmd.GetName() << "\n");
    RANK0COUT("\n");
    RANK0COUT("Configuration:\n");
    RANK0COUT("Routing:           " << (nix ? "nix-vector" : "global") << "\n");
    RANK0COUT("Synchronization:   " << (nullmsg ? "null-message" : "granted time window (YAWNS)")
                                    << "\n");
    RANK0COUT("MPI_Init called:   "
              << (init ? "explicitly by this program" : "implicitly by ns3::MpiInterface::Enable()")
              << "\n");
    RANK0COUT("ns-3 Communicator: " << ns3Ranks << "\n");
    RANK0COUT("PCAP tracing:      " << (tracing ? "" : "not") << " enabled\n");
    RANK0COUT("\n");
    RANK0COUT("Rank assignments:" << std::endl);

    if (worldRank == 0){
        ReportRank(color, splitComm);
    }

    if (verbose){
        // Circulate a token to have each rank report in turn
        int token;

        if (worldRank == 0)
        {
            token = 1;
        }
        else
        {
            MPI_Recv(&token, 1, MPI_INT, worldRank - 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            ReportRank(color, splitComm);
        }

        MPI_Send(&token, 1, MPI_INT, (worldRank + 1) % worldSize, 0, MPI_COMM_WORLD);

        if (worldRank == 0)
        {
            MPI_Recv(&token, 1, MPI_INT, worldSize - 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
    } // circulate token to report rank

    RANK0COUT(std::endl);

    if (color != NS_COLOR)
    {
        // Do other work outside the ns-3 communicator

        // In real use of a separate communicator from ns-3
        // the other tasks would be running another simulator
        // or other desired work here..

        // Our work is done, just wait for everyone else to finish.

        MpiInterface::Disable();

        if (init)
            MPI_Finalize();
        return 0;
    }

    if (verbose)
    {
        LogComponentEnable("PacketSink", LOG_LEVEL_INFO);
    }

    uint32_t systemId = MpiInterface::GetSystemId();
    //uint32_t systemCount = MpiInterface::GetSize();

    // Check for valid distributed parameters.
    // Both this script and simple-distributed.cc will work
    // with arbitrary numbers of ranks, as long as there are at least 2.

    // if (systemCount < 2)
    // {
    //     RANK0COUT("This simulation requires at least 2 logical processors." << std::endl);
    //     return 1;
    // }

    // 默认UDP流量设置
    Config::SetDefault("ns3::OnOffApplication::PacketSize", UintegerValue(1448));
    Config::SetDefault("ns3::OnOffApplication::DataRate", StringValue("2Mbps"));
    Config::SetDefault("ns3::OnOffApplication::MaxBytes", UintegerValue(1448));

    //接下来要在不同进程下根据拓扑配置创建节点

    //那么首先要分配节点给不同的进程
    uint16_t leafP=LEAF/DST;//一个进程有几个leaf
    double spineP=(double)SPINE/DST;//一个进程有几个spine
    //首先创建服务器节点
    serverNodes.resize(LEAF);
    for(uint16_t i=0;i<LEAF;i++){
        serverNodes[i].Create(SERVER, i/leafP);
        if(systemId==i/leafP)
            std::cout<<"process:" << systemId << " Create server nodes:" << serverNodes[i].GetN() << std::endl;
    }

    NodeContainer routerNodes;//记录所有交换机节点 spine + leaf
    //然后创建leaf节点
    std::vector<Ptr<Node>> leafNodes(LEAF);
    for(uint16_t i=0;i<LEAF;i++){
        leafNodes[i]=CreateObject<Node>(i/leafP);
        if(systemId==i/leafP)
            std::cout<<"process:" << systemId << " Create a leaf node id:" << leafNodes[i]->GetId() << std::endl;
        routerNodes.Add(leafNodes[i]);
    }
    //然后创建spine节点
    std::vector<Ptr<Node>> spineNodes(SPINE);
    for(uint16_t i=0;i<SPINE;i++){
        spineNodes[i]=CreateObject<Node>((uint16_t)(i/spineP));
        if(systemId==(uint16_t)(i/spineP))
            std::cout<<"process:" << systemId << " Create a spine node id:" << spineNodes[i]->GetId() << std::endl;
        routerNodes.Add(spineNodes[i]);
    }

    //那么接下来要创建链路了
    //首先创建server到leaf的链路
    PointToPointHelper leafLink;
    leafLink.SetDeviceAttribute("DataRate", StringValue("25Mbps"));
    leafLink.SetChannelAttribute("Delay", StringValue("2us"));
    std::vector<NetDeviceContainer> leafDevices(LEAF);
    std::vector<NetDeviceContainer> serverDevices(LEAF);
    for(int i=0;i<LEAF;i++){
        for(int j=0;j<SERVER;j++){
            NetDeviceContainer temp = leafLink.Install(leafNodes[i],serverNodes[i].Get(j));
            leafDevices[i].Add(temp.Get(0));
            serverDevices[i].Add(temp.Get(1));
        }
    }
    
    //然后创建leaf到spine的链路
    PointToPointHelper spineLink;
    spineLink.SetDeviceAttribute("DataRate", StringValue("25Mbps"));
    spineLink.SetChannelAttribute("Delay", StringValue("2us"));
    std::vector<NetDeviceContainer> spineToLeaf(SPINE*LEAF);
    for(int i=0;i<SPINE;i++){
        for(int j=0;j<LEAF;j++)
            spineToLeaf[i*LEAF+j]=spineLink.Install(spineNodes[i],leafNodes[j]);
    }

    InternetStackHelper stack;
    Ipv4NixVectorHelper nixRouting;
    Ipv4StaticRoutingHelper staticRouting;

    Ipv4ListRoutingHelper list;
    list.Add(staticRouting, 0);
    list.Add(nixRouting, 10);

    if (nix)
        stack.SetRoutingHelper(list); // has effect on the next Install ()

    stack.InstallAll();


    Ipv4InterfaceContainer routerInterfaces;
     serverInterfaces.resize(LEAF);
    std::vector<Ipv4InterfaceContainer> leafInterfaces(LEAF);
    std::vector<Ipv4AddressHelper> serverAddresses(LEAF);
    Ipv4AddressHelper routerAddress;
    int i=0;
    for(i=0;i<LEAF;i++){
        std::string address = "10." + std::to_string(i + 1) + ".1.0";
        serverAddresses[i].SetBase(address.c_str(), "255.255.255.0");
    }
    routerAddress.SetBase("10.0.1.0", "255.255.255.0");
    //交换机链路 interfaces
    std::vector<Ipv4InterfaceContainer> switchInterfaces(LEAF*SPINE);
    // for(int i=0;i<LEAF*SPINE;i++){
    //     switchInterfaces[i] = routerAddress.Assign(spineToLeaf[i]);
    // }
    for(int i=0; i<LEAF*SPINE; i++){
        uint16_t spineId = i / LEAF;
        uint16_t leafId = i % LEAF;
        Ipv4AddressHelper linkAddress;
        linkAddress.SetBase(
            ("172.16." + std::to_string(spineId) + "." + std::to_string(leafId*4)).c_str(),
            "255.255.255.252"
        );
        switchInterfaces[i] = linkAddress.Assign(spineToLeaf[i]);
    }


    //服务器链路interfaces
    for(uint16_t i=0;i<LEAF;i++){
        for(uint16_t j=0;j<SERVER;j++){
            NetDeviceContainer ndc;
            ndc.Add(serverDevices[i].Get(j));
            ndc.Add(leafDevices[i].Get(j));
            Ipv4InterfaceContainer ifc = serverAddresses[i].Assign(ndc);
            serverInterfaces[i].Add(ifc.Get(0));
            leafInterfaces[i].Add(ifc.Get(1));
        }
        serverAddresses[i].NewNetwork();
    }

    if (!nix)
    {
        Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    }

    RANK0COUT("topo Created"<<std::endl);
    workLoad();
    RANK0COUT("workload Created"<<std::endl);
    MPI_Barrier(MPI_COMM_WORLD);
    Simulator::Stop(Seconds(100000));
    auto start = std::chrono::high_resolution_clock::now();
    Simulator::Run();
    Simulator::Destroy();

    // --------------------------------------------------------------------
    // Conditional cleanup based on whether we built a communicator
    // and called MPI_Init

    if (freeComm)
        MPI_Comm_free(&splitComm);

    if (testing)
        SinkTracer::Verify(24);
    MpiInterface::Disable();

    if (init)
        MPI_Finalize();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    RANK0COUT("耗时: " << duration.count() << " 微秒" << std::endl);
    return 0;
}
