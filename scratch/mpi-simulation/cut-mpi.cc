#include "mpi-fixtures.h"
#include "mpi-log.h"

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
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <ctime>

using namespace ns3;

std::map<uint16_t, double> phaseStartTimes;

const int NS_COLOR = 1;
const int NOT_NS_COLOR = NS_COLOR + 1;

void ReportRank(int color, MPI_Comm splitComm){
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

const uint16_t topo[7][3]={ {4,8,8},  //64
                            {4,16,8}, //128
                            {8,32,8},//256
                            {16,64,8},//512
                            {32,128,8},//1024
                            {64,256,8},//2048
};
uint16_t SPINE=2;
uint16_t LEAF=4;
uint16_t SERVER=8;
uint16_t DST=2; //进程数
std::vector<NodeContainer> serverNodes;
std::vector<Ipv4InterfaceContainer> serverInterfaces;

struct FlowInfo{//流量信息结构体
    char type[32];
    uint32_t srcNodeId;
    uint16_t srcPort;
    uint32_t dstNodeId; 
    uint16_t dstPort;
    uint8_t priority;
    uint64_t msgLen;
};

struct OperateState {
    std::vector<std::vector<FlowInfo>> phases;
    int curPhase = 0;
    uint32_t flowCom = 0;
    uint32_t packets = 0;
    bool finished = false;
    double startTime = 0;
    double mpiSyncTime = 0; // 累计MPI同步耗时
};
std::vector<OperateState> allOperates;

void flowRx_cb(int fileIdx, const ns3::Ptr<const ns3::Packet> packet,
               const ns3::Address& srcAddress, const ns3::Address& destAddress);

// 多个operate文件自动加载
void workLoad(int operateStart, int operateEnd) {
    allOperates.clear();
    for (int idx = operateStart; idx <= operateEnd; ++idx) {
        std::string fileName = "scratch/rdma_operate" + std::to_string(idx) + ".txt";
        OperateState op;
        std::ifstream flowInput(fileName);
        if (!flowInput.is_open()) {
            std::cout << "unable to open flowInputFile: " << fileName << std::endl;
            continue;
        }
        std::string line;
        int batch = -1;
        double startTime = 0;
        while (std::getline(flowInput, line)) {
            if (line.empty() || line[0] == '#' || line.find("stat")!=std::string::npos) continue;
            std::stringstream ss(line);
            std::string type_str;
            if (line.find("phase")!=std::string::npos) {
                double phase;
                ss >> type_str >> phase;
                if(batch < 0)
                    startTime += phase/1e6;
                batch ++;
                op.phases.emplace_back(std::vector<FlowInfo> {});
                continue;
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
            op.phases[batch].emplace_back(flow);
        }
        op.startTime = startTime;
        flowInput.close();
        allOperates.emplace_back(op);
    }
}

// 独立加载每个operate的当前phase
void LoadPhase(int fileIdx) {
    OperateState& op = allOperates[fileIdx];
    op.flowCom = 0;
    op.packets = 0;
    if (op.curPhase >= static_cast<int>(op.phases.size())) {
        op.finished = true;
        return;
    }
    phaseStartTimes[fileIdx*1000 + op.curPhase] = Simulator::Now().GetSeconds();
    for (const auto& flow : op.phases[op.curPhase]) {
        // 这里可以加fileIdx参数区分流量归属
        uint32_t systemId = MpiInterface::GetSystemId();
        uint32_t srcSystemId = flow.srcNodeId / (SERVER*LEAF/DST);
        uint32_t dstSystemId = flow.dstNodeId / (SERVER*LEAF/DST);
        uint16_t srcLeaf = flow.srcNodeId / SERVER;
        uint16_t dstLeaf = flow.dstNodeId / SERVER;
        uint16_t srcServer = flow.srcNodeId % SERVER;
        uint16_t dstServer = flow.dstNodeId % SERVER;
        if (systemId == srcSystemId) {
            OnOffHelper clientHelper("ns3::UdpSocketFactory", Address());
            clientHelper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
            clientHelper.SetAttribute("OffTime",StringValue("ns3::ConstantRandomVariable[Constant=0]"));
            clientHelper.SetAttribute("MaxBytes", UintegerValue(flow.msgLen));
            AddressValue remoteAddress(InetSocketAddress(
                serverInterfaces[dstLeaf].GetAddress(dstServer), flow.dstPort));
            clientHelper.SetAttribute("Remote", remoteAddress);
            ApplicationContainer apps = clientHelper.Install(serverNodes[srcLeaf].Get(srcServer));
            apps.Start(Seconds(0));
            apps.Stop(Seconds(100000));
        }
        if (systemId == dstSystemId) {
            PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                 InetSocketAddress(Ipv4Address::GetAny(), flow.dstPort));
            auto apps = sinkHelper.Install(serverNodes[dstLeaf].Get(dstServer));
            auto sink = DynamicCast<PacketSink>(apps.Get(0));
            NS_ASSERT_MSG(sink, "Couldn't get PacketSink application.");
            // 这里用MakeBoundCallback传递fileIdx
            sink->TraceConnectWithoutContext("RxWithAddresses",
                MakeBoundCallback(&flowRx_cb, fileIdx));
            apps.Start(Seconds(0));
            apps.Stop(Seconds(100000));
        }
        if (systemId == dstSystemId)
            op.packets += (flow.msgLen/1448 + ((flow.msgLen%1448)>0?1:0));
    }
    if(op.packets == 0) {
        op.curPhase++;
        LoadPhase(fileIdx);
    }
}

// 回调函数，带fileIdx参数
void flowRx_cb(int fileIdx, const ns3::Ptr<const ns3::Packet> packet,
               const ns3::Address& srcAddress, const ns3::Address& destAddress) {
    OperateState& op = allOperates[fileIdx];
    op.flowCom++;
    if (op.flowCom >= op.packets) {
        uint32_t localDone = 1, globalDone = 0;
        // 真实世界时间统计
        auto syncStart = std::chrono::high_resolution_clock::now();
        MPI_Allreduce(&localDone, &globalDone, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
        auto syncEnd = std::chrono::high_resolution_clock::now();
        op.mpiSyncTime += std::chrono::duration<double>(syncEnd - syncStart).count(); // 累加真实同步耗时
        if (globalDone == MpiInterface::GetSize()) {
            double phaseEndTime = Simulator::Now().GetSeconds();
            double phaseStartTime = phaseStartTimes[fileIdx*1000 + op.curPhase];
            double phaseDuration = phaseEndTime - phaseStartTime;
            logMessage("operate"+std::to_string(fileIdx)+" phase "+std::to_string(op.curPhase)+
                " FCT: "+std::to_string(phaseDuration)+" 秒, MPI同步累计: "+std::to_string(op.mpiSyncTime)+" 秒");
            RANK0COUT("operate " << fileIdx << " phase " << op.curPhase << " completed. MPI同步累计: " << op.mpiSyncTime << " 秒" << std::endl);

            op.curPhase++;
            if(op.curPhase < static_cast<int>(op.phases.size())){
                op.flowCom = 0;
                op.packets = 0;
                // Barrier真实时间统计
                auto barrierStart = std::chrono::high_resolution_clock::now();
                MPI_Barrier(MPI_COMM_WORLD);
                auto barrierEnd = std::chrono::high_resolution_clock::now();
                op.mpiSyncTime += std::chrono::duration<double>(barrierEnd - barrierStart).count();
                if(MpiInterface::GetSystemId() == 0)
                    RANK0COUT("Loading operate " << fileIdx << " phase " << op.curPhase << std::endl);
                LoadPhase(fileIdx);
                barrierStart = std::chrono::high_resolution_clock::now();
                MPI_Barrier(MPI_COMM_WORLD);
                barrierEnd = std::chrono::high_resolution_clock::now();
                op.mpiSyncTime += std::chrono::duration<double>(barrierEnd - barrierStart).count();
                Simulator::Run();
            } else {
                op.finished = true;
                RANK0COUT("operate " << fileIdx << " all phases completed. MPI同步累计: " << op.mpiSyncTime << " 秒" << std::endl);
                // 检查是否所有operate都完成，只有全部完成才停止仿真
                bool allFinished = true;
                for (const auto& operate : allOperates) {
                    if (!operate.finished) {
                        allFinished = false;
                        break;
                    }
                }
                if (allFinished) {
                    Simulator::Stop();
                }
            }
        }
    }
}

int main(int argc, char* argv[]){
    bool nix = true;
    bool tracing = false;
    uint8_t topo_select=1;  // 128拓扑
    int operateStart = 0;
    int operateEnd = 7;
    CommandLine cmd(__FILE__);
    cmd.AddValue("nix", "Enable the use of nix-vector or global routing", nix);
    cmd.AddValue("tracing", "Enable pcap tracing", tracing);
    cmd.AddValue("topo", "topo select", topo_select);
    cmd.AddValue("operateStart", "Start index of operate files", operateStart);
    cmd.AddValue("operateEnd", "End index of operate files", operateEnd);
    cmd.Parse(argc, argv);
    
    // int operateNum = operateEnd - operateStart + 1; // 不再需要，已移除

    SPINE=topo[topo_select][0];
    LEAF=topo[topo_select][1];
    SERVER=topo[topo_select][2];
    GlobalValue::Bind("SimulatorImplementationType",StringValue("ns3::DistributedSimulatorImpl"));

    MpiInterface::Enable(&argc, &argv);
    SinkTracer::Init();

    auto worldSize = SinkTracer::GetWorldSize();
    auto worldRank = SinkTracer::GetWorldRank();
    g_worldRank = worldRank;
    DST=worldSize;
    // 根据operate范围生成独立的日志文件名
    std::string logFileName = "scratch/LOG_cut-mpi_" + std::to_string(operateStart) + "-" + std::to_string(operateEnd) + ".log";
    g_logFile.open(logFileName, std::ios::app);
    if (!g_logFile.is_open()) {
        std::cerr << "无法打开日志文件: " << logFileName << std::endl;
        return 1;
    }
    rank0log("log start");

    bool freeComm = false;
    MPI_Comm splitComm = MPI_COMM_WORLD;
    std::string ns3Ranks;
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
        if (worldRank < DST )
            color = NS_COLOR;
        else
            color = NOT_NS_COLOR;
        std::stringstream ss;
        ss << "Split [1-2] (out of " << worldSize << " ranks) from MPI_COMM_WORLD";
        ns3Ranks = ss.str();
        MPI_Comm_split(MPI_COMM_WORLD, color, worldRank, &splitComm);
        freeComm = true;
    }

    RANK0COUT(cmd.GetName() << "\n");
    RANK0COUT("\n");
    RANK0COUT("Configuration:\n");
    RANK0COUT("Routing:           " << (nix ? "nix-vector" : "global") << "\n");
    RANK0COUT("ns-3 Communicator: " << ns3Ranks << "\n");
    RANK0COUT("PCAP tracing:      " << (tracing ? "" : "not") << " enabled\n");
    RANK0COUT("\n");
    RANK0COUT("Rank assignments:" << std::endl);

    if (worldRank == 0){
        ReportRank(color, splitComm);
    }
    RANK0COUT(std::endl);

    if (color != NS_COLOR)
    {
        MpiInterface::Disable();
        return 0;
    }
    uint32_t systemId = MpiInterface::GetSystemId();
    Config::SetDefault("ns3::OnOffApplication::PacketSize", UintegerValue(1448));
    Config::SetDefault("ns3::OnOffApplication::DataRate", StringValue("2Mbps"));
    Config::SetDefault("ns3::OnOffApplication::MaxBytes", UintegerValue(1448));

    uint16_t leafP=LEAF/DST;
    double spineP=(double)SPINE/DST;
    serverNodes.resize(LEAF);
    for(uint16_t i=0;i<LEAF;i++){
        serverNodes[i].Create(SERVER, i/leafP);
        if(systemId==i/leafP)
            std::cout<<"process:" << systemId << " Create server nodes:" << serverNodes[i].GetN() << std::endl;
    }
    NodeContainer routerNodes;
    std::vector<Ptr<Node>> leafNodes(LEAF);
    for(uint16_t i=0;i<LEAF;i++){
        leafNodes[i]=CreateObject<Node>(i/leafP);
        if(systemId==i/leafP)
            std::cout<<"process:" << systemId << " Create a leaf node id:" << leafNodes[i]->GetId() << std::endl;
        routerNodes.Add(leafNodes[i]);
    }
    std::vector<Ptr<Node>> spineNodes(SPINE);
    for(uint16_t i=0;i<SPINE;i++){
        spineNodes[i]=CreateObject<Node>((uint16_t)(i/spineP));
        if(systemId==(uint16_t)(i/spineP))
            std::cout<<"process:" << systemId << " Create a spine node id:" << spineNodes[i]->GetId() << std::endl;
        routerNodes.Add(spineNodes[i]);
    }

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
        stack.SetRoutingHelper(list);

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
    std::vector<Ipv4InterfaceContainer> switchInterfaces(LEAF*SPINE);
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
    rank0log("拓扑创建完毕 拓扑规模:"+ std::to_string(LEAF*SERVER)+" 进程分配:"+std::to_string(DST));
    MPI_Barrier(MPI_COMM_WORLD);
    workLoad(operateStart, operateEnd);
    RANK0COUT("workload Created"<<std::endl);
    MPI_Barrier(MPI_COMM_WORLD);
    rank0log("流量加载完毕");

    Simulator::Stop(Seconds(100000));
    auto start = std::chrono::high_resolution_clock::now();

    // 启动所有operate的第一个phase
    for (size_t i = 0; i < allOperates.size(); ++i) {
        if (!allOperates[i].finished) {
            LoadPhase(i);
        }
    }

    Simulator::Run();
    Simulator::Destroy();

    if (freeComm)
        MPI_Comm_free(&splitComm);
    //SinkTracer::Verify();
    MpiInterface::Disable();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    double totalSimTime = duration.count() / 1000000.0;
    double totalSyncTime = 0;
    for (const auto& op : allOperates) {
        totalSyncTime += op.mpiSyncTime;
    }
    double syncRatio = totalSyncTime / totalSimTime;
    rank0log("耗时: " + std::to_string(totalSimTime) + " 秒, MPI同步总耗时: " + std::to_string(totalSyncTime) + " 秒, 占比: " + std::to_string(syncRatio * 100) + "%");
    g_logFile.close();
    return 0;
}