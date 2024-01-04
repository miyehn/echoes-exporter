//
// Created by miyehn on 1/1/2024.
//
#include "Log.h"
#include "Utils.h"

#include "../Psd/Psd.h"
#include "../Psd/PsdMallocAllocator.h"
#include "../Psd/PsdLayerMaskSection.h"
#include "../Psd/PsdNativeFile.h"
#include "../Psd/PsdDocument.h"
#include "../Psd/PsdParseDocument.h"
#include "../Psd/PsdColorMode.h"
#include "../Psd/PsdLayer.h"
#include "../Psd/PsdParseLayerMaskSection.h"
#include "../Psd/PsdLayerCanvasCopy.h"
#include "../Psd/PsdChannel.h"
#include "../Psd/PsdInterleave.h"
#include "../Psd/PsdChannelType.h"
#include "../Psd/PsdLayerType.h"
#include "../Psd/PsdBlendMode.h"

#include "cxxopts.hpp"
#include "../External/stb_image.h"
#include "json/json.h"

namespace {
PSD_USING_NAMESPACE;
}

int main(int argc, const char* argv[]) {

	cxxopts::Options options("EchoesMakeBlueprint", "Documentation: TODO\n");
	options.allow_unrecognised_options();

#if 1
	options.add_options()
		("f,file", "input .psd file, absolute path or relative to executable", cxxopts::value<std::string>()/*->default_value("output")*/)
		("i,index", "input .index file, absolute path or relative to executable", cxxopts::value<std::string>()/*->default_value("output")*/)
		("o,output", "output filename without extension, absolute path or relative to executable", cxxopts::value<std::string>()/*->default_value("output")*/)
		;
	auto result = options.parse(argc, argv);

	std::string inPsd, inIndex, outBlueprint;
	try {
		inPsd = result["file"].as<std::string>();
		inIndex = result["index"].as<std::string>();
		outBlueprint = result["output"].as<std::string>() + ".islandblueprint";
		LOG("inPsd: %s, inIndex: %s, outBlueprint: %s", inPsd.c_str(), inIndex.c_str(), outBlueprint.c_str())
	} catch (std::exception &e) {
		std::cout << options.help() << std::endl;
		return 0;
	}

	psd::Document* document;
	psd::LayerMaskSection* section;
	psd::MallocAllocator allocator;
	psd::NativeFile file(&allocator);
	if (!ReadPsdLayers(inPsd, document, section, allocator, file)) {
		// early out
		return 0;
	}

	// build pivots map from index file
	std::string indexStr = ReadFileAsString(inIndex);
	Json::Reader reader;
	Json::Value indexContent;
	std::map<std::string, vec2> pivotsMap;
	if (reader.parse(indexStr, indexContent)) {
		for (const auto &sprite: indexContent) {
			std::string spritePath = sprite["spritePath"].asString();
			float x = sprite["pivot"]["x"].asFloat();
			float y = sprite["pivot"]["y"].asFloat();
			pivotsMap[spritePath] = vec2(x, y);
		}
		LOG("loaded library index containing %zu sprites", pivotsMap.size())
	} else {
		ERR("failed to parse index file")
		return 0;
	}

	// find origin's unit position
	vec2 originUnitPos;
	for (int i = 0; i < section->layerCount; i++) {
		Layer *layer = &section->layers[i];
		if (IsOrUnderLayer(layer, "meta")) {
			if (GetName(layer) == "origin") {
				vec2 pixelPos = vec2(
					layer->left + float(layer->right - layer->left) * 0.5f,
					layer->top + float(layer->bottom - layer->top) * 0.5f
				);
				originUnitPos = PixelPosToUnitPos(pixelPos);
			}
		}
	}

	LOG("origin at: %.3f, %.3f", originUnitPos.x, originUnitPos.y);

	// parse layout folder in psd, and add to output json
	Json::Value bpRoot;
	int spriteIdx = 0;
	for (int i = 0; i < section->layerCount; i++) {
		Layer* layer = &section->layers[i];
		if (!IsOrUnderLayer(layer, "layout") || (layer->type != layerType::SECTION_DIVIDER && !VisibleInHierarchy(layer))) continue;

		// temp: for now, also exclude all groups and section dividers
		if (GetName(layer)=="layout" && layer->type==layerType::OPEN_FOLDER || layer->type==layerType::CLOSED_FOLDER) continue;
		if (layer->type == layerType::SECTION_DIVIDER) continue;

		std::string spritePath = SplitTokens(GetName(layer), '#')[0];
		trim(spritePath);

		vec2 pivot = pivotsMap[spritePath];
		float x2d = layer->left + (layer->right - layer->left) * pivot.x;
		float y2d = layer->bottom - (layer->bottom - layer->top) * pivot.y;
		vec2 unitPos = PixelPosToUnitPos({x2d, y2d}) - originUnitPos;

		// add to json
		Json::Value spriteJson;
		spriteJson["spritePath"] = spritePath;
		spriteJson["position"] = unitPos.serialized();
		bpRoot[spriteIdx] = spriteJson;
		spriteIdx++;

		LOG("exported '%s'", spritePath.c_str())
	}

	file.Close();

	// write file
	std::stringstream stream;
	stream << bpRoot;
	if (!WriteStringToFile(outBlueprint, stream.str())) {
		return 0;
	}

#endif
	LOG("done.")
	return 0;
}
