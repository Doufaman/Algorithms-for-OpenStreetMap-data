#include "data_extraction_storage.h"
#include "config.h"
#include <iostream>
#include <fstream>
#include <osmium/io/pbf_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/osm/relation.hpp>

namespace DES {

    struct InfoHandler : public osmium::handler::Handler {
        std::size_t node_count = 0;
        std::size_t way_count = 0;
        std::size_t relation_count = 0;

        // 各类型输出文件
        std::ofstream node_file;
        std::ofstream way_file;
        std::ofstream relation_file;

        // 最多保存多少条（仅用于理解数据）
        static constexpr std::size_t MAX_PRINT = 100;

        InfoHandler() {
            // 打开输出文件，写入 JSON 数组开头
            node_file.open(Config::SAMPLE_DATA_DIR + "nodes_sample.json");
            way_file.open(Config::SAMPLE_DATA_DIR + "ways_sample.json");
            relation_file.open(Config::SAMPLE_DATA_DIR + "relations_sample.json");
            node_file << "[\n";
            way_file << "[\n";
            relation_file << "[\n";
        }

        ~InfoHandler() {
            // 写入 JSON 数组结尾
            node_file << "\n]\n";
            way_file << "\n]\n";
            relation_file << "\n]\n";
        }

        // 将标签列表转为 JSON 对象片段
        // 例如："highway": "residential", "name": "Hauptstraße"
        static std::string tagsToJson(const osmium::TagList& tags) {
            std::string result;
            for (const auto& tag : tags) {
                // 简单转义双引号
                std::string key = tag.key();
                std::string value = tag.value();
                for (auto& c : value) if (c == '"') c = '\'';
                result += "      \"" + key + "\": \"" + value + "\",\n";
            }
            if (!result.empty()) result.pop_back(); // 去掉最后的逗号
            if (!result.empty()) result.pop_back(); // 去掉最后的换行
            return result;
        }

        void node(const osmium::Node& node) {
            ++node_count;
            if (node_count > MAX_PRINT) return;

            if (node_count > 1) node_file << ",\n";
            node_file << "  {\n";
            node_file << "    \"id\": " << node.id() << ",\n";
            node_file << "    \"lat\": " << node.location().lat() << ",\n";
            node_file << "    \"lon\": " << node.location().lon() << ",\n";
            node_file << "    \"tags\": {\n" << tagsToJson(node.tags()) << "\n    }\n";
            node_file << "  }";
        }

        void way(const osmium::Way& way) {
            ++way_count;
            if (way_count > MAX_PRINT) return;

            // 收集 node ID 列表
            std::string node_ids = "[";
            for (const auto& nr : way.nodes()) {
                node_ids += std::to_string(nr.ref()) + ",";
            }
            if (node_ids.back() == ',') node_ids.pop_back();
            node_ids += "]";

            if (way_count > 1) way_file << ",\n";
            way_file << "  {\n";
            way_file << "    \"id\": " << way.id() << ",\n";
            way_file << "    \"node_count\": " << way.nodes().size() << ",\n";
            way_file << "    \"node_ids\": " << node_ids << ",\n";
            way_file << "    \"tags\": {\n" << tagsToJson(way.tags()) << "\n    }\n";
            way_file << "  }";
        }

        void relation(const osmium::Relation& relation) {
            ++relation_count;
            if (relation_count > MAX_PRINT) return;

            // 收集成员列表
            std::string members = "[";
            for (const auto& m : relation.members()) {
                members += "{\"type\":\"" + std::string(1, osmium::item_type_to_char(m.type()))
                    + "\",\"ref\":" + std::to_string(m.ref())
                    + ",\"role\":\"" + m.role() + "\"},";
            }
            if (members.back() == ',') members.pop_back();
            members += "]";

            if (relation_count > 1) relation_file << ",\n";
            relation_file << "  {\n";
            relation_file << "    \"id\": " << relation.id() << ",\n";
            relation_file << "    \"members\": " << members << ",\n";
            relation_file << "    \"tags\": {\n" << tagsToJson(relation.tags()) << "\n    }\n";
            relation_file << "  }";
        }
    };

    void extractFromPBF(const std::string& pbf_path) {
        std::cout << "Reading: " << pbf_path << "\n\n";

        try {
            osmium::io::File   input_file{ pbf_path };
            osmium::io::Reader reader{ input_file };

            InfoHandler handler;
            osmium::apply(reader, handler);
            reader.close();

            std::cout << "\n========== Stats ==========\n";
            std::cout << "Nodes:     " << handler.node_count << "\n";
            std::cout << "Ways:      " << handler.way_count << "\n";
            std::cout << "Relations: " << handler.relation_count << "\n";
            std::cout << "============================\n";
            std::cout << "Saved to data/nodes_sample.json\n";
            std::cout << "Saved to data/ways_sample.json\n";
            std::cout << "Saved to data/relations_sample.json\n";

        }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
    }

} // namespace DES