#include "data_extraction_storage.h"
#include "config.h"
#include <iostream>
#include <osmium/io/pbf_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/osm/relation.hpp>

namespace DES {

    // 统计和打印 OSM 数据的 Handler
    struct InfoHandler : public osmium::handler::Handler {
        std::size_t node_count = 0;
        std::size_t way_count = 0;
        std::size_t relation_count = 0;

        void node(const osmium::Node& node) {
            ++node_count;
            // 打印前5个节点作为示例
            if (node_count <= 5) {
                std::cout << "[Node] id=" << node.id()
                    << " lat=" << node.location().lat()
                    << " lon=" << node.location().lon();
                // 打印标签
                for (const auto& tag : node.tags()) {
                    std::cout << " " << tag.key() << "=" << tag.value();
                }
                std::cout << "\n";
            }
        }

        void way(const osmium::Way& way) {
            ++way_count;
            if (way_count <= 5) {
                std::cout << "[Way] id=" << way.id()
                    << " nodes=" << way.nodes().size();
                for (const auto& tag : way.tags()) {
                    std::cout << " " << tag.key() << "=" << tag.value();
                }
                std::cout << "\n";
            }
        }

        void relation(const osmium::Relation& relation) {
            ++relation_count;
            if (relation_count <= 5) {
                std::cout << "[Relation] id=" << relation.id()
                    << " members=" << relation.members().size();
                for (const auto& tag : relation.tags()) {
                    std::cout << " " << tag.key() << "=" << tag.value();
                }
                std::cout << "\n";
            }
        }
    };

    void extractFromPBF(const std::string& pbf_path) {
        std::cout << "Reading: " << pbf_path << "\n\n";

        try {
            osmium::io::File input_file{ pbf_path };
            osmium::io::Reader reader{ input_file };

            InfoHandler handler;
            osmium::apply(reader, handler);
            reader.close();

            // 打印统计信息
            std::cout << "\n========== Stats ==========\n";
            std::cout << "Nodes:     " << handler.node_count << "\n";
            std::cout << "Ways:      " << handler.way_count << "\n";
            std::cout << "Relations: " << handler.relation_count << "\n";
            std::cout << "============================\n";

        }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
    }

}