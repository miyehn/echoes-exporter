#include <fstream>
#include <filesystem>

#include "../Psd/Psd.h"
#include "../Psd/PsdMallocAllocator.h"
#include "../Psd/PsdLayerMaskSection.h"
#include "../Psd/PsdNativeFile.h"
#include "../Psd/PsdDocument.h"
#include "../Psd/PsdParseDocument.h"
#include "../Psd/PsdColorMode.h"
#include "../Psd/PsdLayer.h"
#include "../Psd/PsdParseLayerMaskSection.h"
#include "../Psd/PsdChannel.h"

#include "Log.h"
#include "json/json.h"

#include "Utils.h"

std::string ReadFileAsString(const std::string& path) {
	std::ifstream file(path, std::ios::ate);
	EXPECT_M(file.is_open(), true, "failed to open file '%s'", path.c_str())
	size_t size = file.tellg();
	std::string outStr;
	outStr.reserve(size);
	file.seekg(0);
	outStr.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	file.close();
	return outStr;
}

bool WriteStringToFile(const std::string& path, const std::string& content) {
	std::ofstream file;
	file.open(path);
	if (file.is_open()) {
		file << content;
		file.close();
		return true;
	} else {
		return false;
	}
}
std::vector<std::string> SplitLines(const std::string& str) {
	std::vector<std::string> result;
	int start = 0, end;
	do {
		end = str.find('\n', start);
		std::string s = str.substr(start, end - start);
		trim(s);
		result.push_back(s);
		start = end + 1;
	} while (end != -1);

	return result;
}
bool ReadPsdLayers(
	const std::string& inFile,
	psd::Document*& pDocument,
	psd::LayerMaskSection*& pSection,
	psd::MallocAllocator& allocator,
	psd::NativeFile& file
){
	const std::wstring fullPath(inFile.c_str(), inFile.c_str() + inFile.length());

	// open file
	if (!file.OpenRead(fullPath.c_str())) {
#if ECHOES_EXPORTER_WINDOWED
		AppendToGUILog({LT_ERROR, "ERROR: Failed to open file!"});
#endif
		ERR("failed to open file")
		return false;
	}

	// create document
	psd::Document* document = psd::CreateDocument(&file, &allocator);
	if (!document) {
#if ECHOES_EXPORTER_WINDOWED
		AppendToGUILog({LT_ERROR, "ERROR: Failed to open file! Was that a PSD document?"});
#endif
		WARN("failed to create psd document")
		file.Close();
		return false;
	}

	// check color mode
	if (document->colorMode != psd::colorMode::RGB) {
#if ECHOES_EXPORTER_WINDOWED
		AppendToGUILog({LT_ERROR, "ERROR: This PSD is not in RGB color mode"});
#endif
		WARN("psd is not in RGB color mode")
		file.Close();
		return false;
	}

	// check bits per channel
	if (document->bitsPerChannel != 8) {
#if ECHOES_EXPORTER_WINDOWED
		AppendToGUILog({LT_ERROR, "ERROR: This PSD doesn't have 8 bits per channel"});
#endif
		WARN("this psd doesn't have 8 bits per channel")
		file.Close();
		return false;
	}

	// read section
	auto section = psd::ParseLayerMaskSection(document, &file, &allocator);
	if (!section) {
#if ECHOES_EXPORTER_WINDOWED
		AppendToGUILog({LT_ERROR, "ERROR: failed to parse layer mask section"});
#endif
		WARN("failed to parse layer mask section")
		file.Close();
		return false;
	}

	pDocument = document;
	pSection = section;
	return true;
}
std::string GetName(const psd::Layer* layer) {
	std::string name(layer->name.c_str());
	return name;
}

bool IsOrUnderLayer(const psd::Layer* layer, const std::string& ancestorName) {
	const psd::Layer* itr = layer;
	while (itr) {
		if (GetName(itr) == ancestorName) return true;
		itr = itr->parent;
	}
	return false;
}
bool VisibleInHierarchy(const psd::Layer* layer) {
	const psd::Layer* itr = layer;
	while (itr) {
		if (!itr->isVisible) return false;
		itr = itr->parent;
	}
	return true;
}

Json::Value vec2::serialized() const {
	Json::Value result;
	result["x"] = x;
	result["y"] = y;
	return result;
}
vec2 ToIsometric(vec2 p) {
	vec2 v0 = ISO_X;
	vec2 v1 = ISO_Y;
	return {
		v0.x * p.x + v1.x * p.y,
		v0.y * p.x + v1.y * p.y
	};
}

vec2 FromIsometric(vec2 p) {
	const float det = ISO_X.x * ISO_Y.y - ISO_Y.x * ISO_X.y;
	vec2 v0 = {
		ISO_Y.y / det,
		-ISO_X.y / det
	};
	vec2 v1 = {
		-ISO_Y.x / det,
		ISO_X.x / det
	};
	return {
		v0.x * p.x + v1.x * p.y,
		v0.y * p.x + v1.y * p.y
	};
}

vec2 PixelPosToUnitPos(vec2 pixelPos, float workingPPDU) {
	vec2 unitPos = FromIsometric(pixelPos);
	return unitPos / (workingPPDU / sqrt2);
}

vec2 UnitPosToPixelPos(vec2 unitPos, float workingPPDU) {
	vec2 pixelPos = ToIsometric(unitPos);
	return pixelPos * (workingPPDU / sqrt2);
}

std::vector<std::string> SplitTokens(const std::string& s, char separator) {
	std::vector<std::string> tokens;
	std::string cur;
	for (char c : s) {
		if (c == separator) {
			if (cur.length() > 0) tokens.push_back(cur);
			cur = "";
		} else {
			cur += c;
		}
	}
	if (cur.length() > 0) tokens.push_back(cur);
	return tokens;
}

std::string JoinTokens(const std::vector<std::string> &tokens) {
	std::string result;
	for (auto token : tokens) {
		ASSERT(token.length() > 0)
		token[0] = (char)toupper(token[0]);
		result += token;
	}
	return result;
}
