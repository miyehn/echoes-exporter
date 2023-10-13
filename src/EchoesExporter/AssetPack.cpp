#include <Windows.h>
#include <filesystem>
#include <sstream>
#include <fstream>
#include "stb_image_resize.h"
#include "stb_image_write.h"
#include "AssetPack.h"
#include "Log.h"
#include "json/json.h"

#define EXPORT_PPU 100.0f

const float sqrt2 = std::sqrt(2.0f);
const float sqrt3 = std::sqrt(3.0f);
const vec2 ISO_X = {sqrt2 / 2, sqrt2 / sqrt3 / 2 };
const vec2 ISO_Y = {sqrt2 / 2, -sqrt2 / sqrt3 / 2 };

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
	return unitPos / (workingPPDU / sqrt(2.0f));
}

vec2 UnitPosToPixelPos(vec2 unitPos, float workingPPDU) {
	vec2 pixelPos = ToIsometric(unitPos);
	return pixelPos * (workingPPDU / sqrt(2.0f));
}

std::vector<std::string> SplitTokens(const std::string& s) {
	std::vector<std::string> tokens;
	std::string cur;
	for (char c : s) {
		if (c == ' ') {
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

void WritePngToDirectory(
	const std::vector<uint8_t>& data,
	uint32_t width, uint32_t height,
	const std::string& outDir,
	const std::string& fileName,
	float resizeRatio = 1.0f
) {
	std::string fullpath = outDir + "/" + fileName;

	// resize
	uint32_t newWidth = std::ceil(width * resizeRatio);
	uint32_t newHeight = std::ceil(height * resizeRatio);
	std::vector<uint8_t> resizedData(newWidth * newHeight * 4);
	stbir_resize_uint8(
		data.data(), width, height, width * 4,
		resizedData.data(), newWidth, newHeight, newWidth * 4, 4);

	// write
	EXPECT(stbi_write_png(fullpath.c_str(), newWidth, newHeight, 4, resizedData.data(), 4 * newWidth) != 0, true)
}

std::vector<uint8_t> Crop(const std::vector<uint8_t>& data, uint32_t srcStrideInBytes, ivec2 minPx, ivec2 sizePx) {
	std::vector<uint8_t> result(sizePx.x * sizePx.y * 4);
	uint32_t dstStrideInBytes = sizePx.x * 4;
	for (int i = 0; i < sizePx.y; i++) {
		uint32_t srcStart = (minPx.y + i) * srcStrideInBytes + minPx.x * 4;
		uint32_t dstStart = i * dstStrideInBytes;
		memcpy(result.data() + dstStart, data.data() + srcStart, dstStrideInBytes);
	}
	return result;
}

Json::Value vec2::serialized() const {
	Json::Value result;
	result["x"] = x;
	result["y"] = y;
	return result;
}

struct PivotInfo {
	std::string texPath;
	vec2 pivot;
	Json::Value serialized() const {
		Json::Value result;
		result["texPath"] = texPath;
		result["pivot"] = pivot.serialized();
		return result;
	}
};

struct MaterialInfo {
	std::string name;
	std::string mainTexPath;
	std::string light0TexPath;
	std::string light0Message = "(none)";
	std::string light1TexPath;
	std::string light1Message = "(none)";
	std::string light2TexPath;
	std::string light2Message = "(none)";
	std::string light3TexPath;
	std::string light3Message = "(none)";
	vec2 basePosition;
	vec2 size;
	Json::Value serialized() const {
		Json::Value result;
		result["name"] = name;
		result["mainTexPath"] = mainTexPath;
		result["light0TexPath"] = light0TexPath;
		result["light0Message"] = light0Message;
		result["light1TexPath"] = light1TexPath;
		result["light1Message"] = light1Message;
		result["light2TexPath"] = light2TexPath;
		result["light2Message"] = light2Message;
		result["light3TexPath"] = light3TexPath;
		result["light3Message"] = light3Message;
		result["basePosition"] = basePosition.serialized();
		result["size"] = size.serialized();
		return result;
	}
};

std::string SpriteSet::getBaseName() const {
	return JoinTokens(SplitTokens(name));
}

std::string SpriteSet::getBaseTexPath(int index) const {
	return getBaseName() + "_base"+std::to_string(index)+".png";
}

std::string SpriteSet::getLightTexPath(int index) const {
	std::string baseName = JoinTokens(SplitTokens(name));
	return getBaseName() + "_L" + std::to_string(index) +".png";
}

float ComputeResizeRatio(float pixelsPerDiagonalUnit) {
	const float standardPPDU = std::sqrt(2.0f) * EXPORT_PPU;
	const float resizeRatio = standardPPDU / pixelsPerDiagonalUnit;
	return resizeRatio;
}
// a list of all base textures to their anchor points

// also a list of materials, each:
// base tex + all the light textures
// base name

std::string SerializeAssetPack(const AssetPack& assetPack) {
	Json::Value root;

	// pivots
	Json::Value pivots;
	int pivotIdx = 0;
	for (auto& spritePair : assetPack.spriteSets) {
		auto& sprite = spritePair.second;
		vec2 anchorUnit = sprite.minUnit + sprite.sizeUnit / 2;
		vec2 relAnchorPx = UnitPosToPixelPos(anchorUnit, assetPack.pixelsPerDiagonalUnit);
		vec2 anchorPx = relAnchorPx + assetPack.docOriginPx - sprite.minPx;
		vec2 anchorNormalized = {
			anchorPx.x / sprite.sizePx.x,
			1.0f - anchorPx.y / sprite.sizePx.y // since unity wants this reversed
		};
		for (int baseLayerIdx = 0; baseLayerIdx < sprite.baseLayersData.size(); baseLayerIdx++) {
			PivotInfo pivot = {
				sprite.getBaseTexPath(baseLayerIdx),
				anchorNormalized
			};
			pivots[pivotIdx] = pivot.serialized();
			LOG("%s pivot at (%.3f, %.3f)", sprite.getBaseTexPath(baseLayerIdx).c_str(), pivot.pivot.x, pivot.pivot.y)
			pivotIdx++;
		}
	}
	root["pivots"] = pivots;

	// materials
	const float resizeRatio = ComputeResizeRatio(assetPack.pixelsPerDiagonalUnit);
	Json::Value materials;
	int matIdx = 0;
	for (auto& spritePair : assetPack.spriteSets) {
		auto &sprite = spritePair.second;
		for (int baseLayerIdx = 0; baseLayerIdx < sprite.baseLayersData.size(); baseLayerIdx++) {
			MaterialInfo mat;
			mat.name = sprite.getBaseName();
			LOG("exporting material '%s'..", mat.name.c_str())
			mat.mainTexPath = sprite.getBaseTexPath(baseLayerIdx);
			mat.basePosition = sprite.minUnit;
			mat.size = sprite.sizeUnit;
			for (int lightLayerIdx = 0; lightLayerIdx < sprite.lightLayersData.size(); lightLayerIdx++) {
				if (lightLayerIdx == 0) {
					mat.light0TexPath = sprite.getLightTexPath(0);
					mat.light0Message = sprite.lightLayerNames[0];
				}
				else if (lightLayerIdx == 1) {
					mat.light1TexPath = sprite.getLightTexPath(1);
					mat.light1Message = sprite.lightLayerNames[1];
				}
				else if (lightLayerIdx == 2) {
					mat.light2TexPath = sprite.getLightTexPath(2);
					mat.light2Message = sprite.lightLayerNames[2];
				}
				else if (lightLayerIdx == 3) {
					mat.light3TexPath = sprite.getLightTexPath(3);
					mat.light3Message = sprite.lightLayerNames[3];
				}
				else ASSERT(false)
			}
			materials[matIdx] = mat.serialized();
			matIdx++;
		}
	}
	root["materials"] = materials;

	std::stringstream stream;
	stream << root;
	return stream.str();
}

bool ExportAssetPack(const AssetPack& assetPack, const std::string& outDir) {

	// remove everything from previous export
	if (std::filesystem::exists(outDir)) {
		std::filesystem::remove_all(outDir);
	}

	// from: https://stackoverflow.com/questions/9235679/create-a-directory-if-it-doesnt-exist
	std::filesystem::create_directories(outDir);

	// compute resize ratio
	const float resizeRatio = ComputeResizeRatio(assetPack.pixelsPerDiagonalUnit);
	LOG("resize ratio: %.3f", resizeRatio)

	for (auto& pair : assetPack.spriteSets) {

		const SpriteSet& sprite = pair.second;
		for (int i = 0; i < sprite.baseLayersData.size(); i++) {
			auto baseRegionData = Crop(sprite.baseLayersData[i], assetPack.docWidth * 4, sprite.minPx, sprite.sizePx);
			WritePngToDirectory(
				baseRegionData, sprite.sizePx.x, sprite.sizePx.y, outDir,
				sprite.getBaseTexPath(i), resizeRatio);
		}
		for (int i = 0; i < sprite.lightLayersData.size(); i++) {
			auto lightTexRegionData = Crop(sprite.lightLayersData[i], assetPack.docWidth * 4, sprite.minPx, sprite.sizePx);
			WritePngToDirectory(
				lightTexRegionData, sprite.sizePx.x, sprite.sizePx.y, outDir,
				sprite.getLightTexPath(i), resizeRatio);
		}
	}

	/*
	std::string folderName = outDir.substr(outDir.find_last_of("/\\") + 1);
	if (folderName.length() == 0) folderName = outDir;
	LOG("finished saving png files, packing them into %s.zip..", folderName.c_str())

	std::string fullCommand = "tar -cf " + outDir + ".tar " + outDir + "/*";
	system(fullCommand.c_str());
	 */
#if 1
	// also export a json
	std::string outstr = SerializeAssetPack(assetPack);

	// write to file
	std::string folderName = outDir.substr(outDir.find_last_of("/\\") + 1);
	std::ofstream file;
	file.open(outDir + "/" + folderName + ".assetpack");
	if (file.is_open()) {
		file << outstr;
		file.close();
	} else {
		ASSERT(false)
		return false;
	}
	return true;
#else
	return true;
#endif
}
