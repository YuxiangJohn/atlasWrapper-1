#ifndef DYNAMICGRAPH_H
#define DYNAMICGRAPH_H

#include "hiaiengine/api.h"
#include "hiaiengine/graph.h"
#include "proto/graph_config.pb.h"
#include <fstream>
#include <google/protobuf/text_format.h>
#include <memory>
#include <stdint.h>
#include <string>
#include <tuple>
#include <vector>

using std::make_shared;
using std::shared_ptr;
using std::string;
using std::vector;

namespace dg {

struct AIConfigItem {
    AIConfigItem(){}
    AIConfigItem(const AIConfigItem &it)
    {
        name = it.name;
        value = it.value;
    }
    AIConfigItem(AIConfigItem &&it)
    {
        name = std::move(it.name);
        value = std::move(it.value);
    }
    AIConfigItem(const string &n, const string &v)
    {
        name = n;
        value = v;
    }
    AIConfigItem(string &&n, string &&v)
    {
        name = std::move(n);
        value = std::move(v);
    }
    string name;
    string value;
    // vector<AIConfigItem> sub_items;
};

struct AIConfig {
    vector<AIConfigItem> items;
    void addAIConfigItem(const AIConfigItem &it)
    {
        items.push_back(it);
    }
    void addAIConfigItem(AIConfigItem &&it)
    {
        items.push_back(std::move(it));
    }
    void addAIConfigItem(const string &n, const string &v)
    {
        items.emplace_back(n, v);
    }
    void addAIConfigItem(string &&n, string &&v)
    {
        items.emplace_back(n, v);
    }
};

struct engine {
    enum RunSide {
        DEVICE = static_cast<int>(hiai::EngineConfig_RunSide_DEVICE),
        HOST = static_cast<int>(hiai::EngineConfig_RunSide_HOST)
    };
    engine(){}
    engine(const string &engineName, uint32_t engineId, uint32_t threadNum, RunSide runSide)
    {
        engine_name = engineName;
        id = engineId;
        thread_num = threadNum;
        side = runSide;
    }
    uint32_t id;
    string engine_name;
    vector<string> so_name;
    RunSide side;
    uint32_t thread_num;
    uint32_t thread_priority;
    uint32_t queue_size = 0;
    AIConfig ai_config;
    vector<string> internal_so_name;
    uint32_t wait_inputdata_max_time;
    uint32_t holdModelFileFlag;
    bool is_repeat_timeout_flag;
};

struct connection {
    connection(){}
    connection(uint32_t src_id, uint32_t src_port, uint32_t dst_id, uint32_t dst_port)
    {
        src_engine_id = src_id;
        src_port_id = src_port;
        target_engine_id = dst_id;
        target_port_id = dst_port;
    }
    connection(const engine &src_engine, uint32_t src_port, const engine &dst_engine, uint32_t dst_port)
    {
        src_engine_id = src_engine.id;
        src_port_id = src_port;
        target_engine_id = dst_engine.id;
        target_port_id = dst_port;
    }
    uint32_t src_engine_id;
    uint32_t src_port_id;
    uint32_t target_graph_id;
    uint32_t target_engine_id;
    uint32_t target_port_id;
};

struct graph {
    graph(uint32_t id, uint32_t device, int32_t prior = 0)
    {
        graph_id = id;
        device_id = device;
        priority = prior;
    }
    uint32_t graph_id;
    int32_t priority;
    uint32_t device_id = 0;
    vector<engine> engines;
    vector<connection> connects;
    void addEngine(const engine &e)
    {
        engines.push_back(e);
    }
    void addEngine(engine &&e)
    {
        engines.push_back(std::move(e));
    }
    void addConnection(const connection &c)
    {
        connects.push_back(c);
    }
    void addConnection(connection &&c)
    {
        connects.push_back(std::move(c));
    }
};

typedef std::tuple<graph, engine, uint32_t> NodeInfo;

class DynamicGraph {
public:
    void addGraph(const graph &g)
    {
        graphs.push_back(g);
    }
    void addGraph(graph &&g)
    {
        graphs.push_back(std::move(g));
    }
    uint32_t getGraphId(int i = 0)
    {
        return graphs[i].graph_id;
    }
    HIAI_StatusT createGraph()
    {
        if (graphs.size() > 0) {
            initDevices();
            maintainGraph();
        } else {
            printf("Error, no graphs to create\n");
            return HIAI_ERROR;
        }
        std::string str;
        google::protobuf::TextFormat::PrintToString(*graphConfigList, &str);
        //std::ofstream out("out_graph.config");
        //out << str;
        //out.close();
        return hiai::Graph::CreateGraph(*graphConfigList);
    }
    HIAI_StatusT destroyGraph()
    {
        HIAI_StatusT ret = HIAI_OK;
        for (auto &g : graphs) {
            HIAI_StatusT rtmp = hiai::Graph::DestroyGraph(g.graph_id);
            if (HIAI_OK != rtmp) {
                ret = rtmp;
            }
        }
        return ret;
    }
    HIAI_StatusT setDataRecvFunctor(const NodeInfo &node, const std::shared_ptr<hiai::DataRecvInterface> &dataRecv)
    {
        std::shared_ptr<hiai::Graph> graph = hiai::Graph::GetInstance(std::get<0>(node).graph_id);
        if (nullptr == graph) {
            printf("[Error] Fail to get the graph-%u\n", std::get<0>(node).graph_id);
            return HIAI_ERROR;
        }
        hiai::EnginePortID config;
        config.graph_id = std::get<0>(node).graph_id;
        config.engine_id = std::get<1>(node).id;
        config.port_id = std::get<2>(node);
        return graph->SetDataRecvFunctor(config, dataRecv);
    }
    HIAI_StatusT sendData(const NodeInfo &node, const std::string &messageName, const shared_ptr<void> &message)
    {
        std::shared_ptr<hiai::Graph> graph = hiai::Graph::GetInstance(std::get<0>(node).graph_id);
        if (nullptr == graph) {
            printf("[Error] Fail to get the graph-%u\n", std::get<0>(node).graph_id);
            return HIAI_ERROR;
        }
        hiai::EnginePortID config;
        config.graph_id = std::get<0>(node).graph_id;
        config.engine_id = std::get<1>(node).id;
        config.port_id = std::get<2>(node);
        return graph->SendData(config, messageName, message);
    }

private:
    HIAI_StatusT initDevices()
    {
        HIAI_StatusT ret = HIAI_OK;
        for (auto &g : graphs) {
            ret = HIAI_Init(g.device_id);
            if (ret != HIAI_OK) {
                return ret;
            }
        }
        return ret;
    }
    void maintainGraph()
    {
        graphConfigList = make_shared<hiai::GraphConfigList>();
        graphConfigList->clear_graphs();
        for (auto &graph : graphs) {
            auto hiaiGraph = graphConfigList->add_graphs();
            hiaiGraph->set_graph_id(graph.graph_id);
            hiaiGraph->set_device_id(std::to_string(graph.device_id));
            hiaiGraph->set_priority(graph.priority);
            hiaiGraph->clear_engines();
            for (auto &e : graph.engines) {
                auto hiaiEngine = hiaiGraph->add_engines();
                hiaiEngine->set_id(e.id);
                hiaiEngine->set_engine_name(e.engine_name);
                hiaiEngine->set_side(static_cast<hiai::EngineConfig_RunSide>(static_cast<int>(e.side)));
                hiaiEngine->set_thread_num(e.thread_num);
                hiaiEngine->set_queue_size(e.queue_size);
                for (auto &sn : e.so_name) {
                    hiaiEngine->add_so_name(sn);
                }
                auto ai_config = hiaiEngine->mutable_ai_config();
                ai_config->clear_items();
                for (auto &it : e.ai_config.items) {
                    auto hiaiItem = ai_config->add_items();
                    hiaiItem->set_name(it.name);
                    hiaiItem->set_value(it.value);
                }
            }
            hiaiGraph->clear_connects();
            for (auto &c : graph.connects) {
                auto hiaiConnect = hiaiGraph->add_connects();
                hiaiConnect->set_src_engine_id(c.src_engine_id);
                hiaiConnect->set_src_port_id(c.src_port_id);
                hiaiConnect->set_target_engine_id(c.target_engine_id);
                hiaiConnect->set_target_port_id(c.target_port_id);
            }
        }
    }
    shared_ptr<hiai::GraphConfigList> graphConfigList;
    vector<graph> graphs;
};
}  // namespace dg

#endif  // ATLASFACEDEMO_DYNAMICGRAPH_H
